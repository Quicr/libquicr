/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <quicr/moq_instance.h>
#include <quicr/moq_messages.h>

namespace quicr {

    using namespace quicr::messages;

    MoQInstance::MoQInstance(const MoQInstanceClientConfig& cfg,
                            std::shared_ptr<MoQInstanceDelegate> delegate,
                            const cantina::LoggerPointer& logger) :
        _client_mode(true)
        , _server_config({})
        , _client_config(cfg)
        , _delegate(std::move(delegate))
        , _logger(std::make_shared<cantina::Logger>("MIC", logger))
#ifndef LIBQUICR_WITHOUT_INFLUXDB
        , _mexport(logger)
#endif
        , _transport({})
    {
        _logger->info << "Created MoQ instance in client mode connecting to "
                      << cfg.server_host_ip << ":" << cfg.server_port
                      << std::flush;

        init();
    }

    MoQInstance::MoQInstance(const MoQInstanceServerConfig& cfg,
                             std::shared_ptr<MoQInstanceDelegate> delegate,
                             const cantina::LoggerPointer& logger)
      : _client_mode(false)
      , _server_config(cfg)
      , _client_config({})
      , _delegate(std::move(delegate))
      , _logger(std::make_shared<cantina::Logger>("MIS", logger))
#ifndef LIBQUICR_WITHOUT_INFLUXDB
        , _mexport(logger)
#endif
      , _transport({})
    {
        _logger->info << "Created MoQ instance in server mode listening on "
                      << cfg.server_bind_ip << ":" << cfg.server_port
                      << std::flush;

        init();
    }

    void MoQInstance::init()
    {
        _logger->info << "Starting metrics exporter" << std::flush;

#ifndef LIBQUICR_WITHOUT_INFLUXDB
        if (_mexport.init("http://metrics.m10x.ctgpoc.com:8086",
                         "Media10x",
                         "cisco-cto-media10x") !=
            MetricsExporter::MetricsExporterError::NoError) {
            throw std::runtime_error("Failed to connect to InfluxDB");
            }

        _mexport.run();
#endif
    }

    MoQInstance::Status MoQInstance::run_server()
    {
        TransportRemote server { .host_or_ip = _server_config.server_bind_ip,
                                 .port = _server_config.server_port,
                                 .proto =  _server_config.server_proto };

        _transport = ITransport::make_server_transport(server, _server_config.transport_config,
                                                       *this, _logger);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
        _transport->start(_mexport.metrics_conn_samples, _mexport.metrics_data_samples);
#else
        _transport->start(nullptr, nullptr);
#endif

        switch (_transport->status()) {
            case TransportStatus::Ready:
                return Status::READY;
            default:
                return Status::NOT_READY;
        }
    }

    MoQInstance::Status MoQInstance::run_client()
    {
        TransportRemote relay { .host_or_ip = _client_config.server_host_ip,
                                .port = _client_config.server_port,
                                .proto =  _client_config.server_proto };

        _transport = ITransport::make_client_transport(relay, _client_config.transport_config,
                                                       *this, _logger);

        _status = Status::CLIENT_CONNECTING;

#ifndef LIBQUICR_WITHOUT_INFLUXDB
        auto conn_id = _transport->start(_mexport.metrics_conn_samples, _mexport.metrics_data_samples);
#else
        auto conn_id = _transport->start(nullptr, nullptr);
#endif
        LOGGER_INFO(_logger, "Connecting session conn_id: " << conn_id << "...");
        auto [conn_ctx, _] = _connections.try_emplace(conn_id, ConnectionContext{});
        conn_ctx->second.conn_id = conn_id;

        return _status;
    }

    void MoQInstance::send_ctrl_msg(const ConnectionContext& conn_ctx, std::vector<uint8_t>&& data)
    {
        if (not conn_ctx.ctrl_data_ctx_id) {
            close_connection(conn_ctx.conn_id,
                             MoQTerminationReason::PROTOCOL_VIOLATION,
                             "Control bidir stream not created");
            return;
        }

        _transport->enqueue(conn_ctx.conn_id,
                            *conn_ctx.ctrl_data_ctx_id,
                            std::move(data),
                            { MethodTraceItem{} },
                            0,
                            2000,
                            0,
                            { true, false, false, false });

    }

    void MoQInstance::send_client_setup()
    {
        StreamBuffer<uint8_t> buffer;
        auto client_setup = MoqClientSetup{};

        client_setup.num_versions = 1;      // NOTE: Not used for encode, verison vector size is used
        client_setup.supported_versions = { MOQT_VERSION };
        client_setup.role_parameter.param_type = static_cast<uint64_t>(ParameterType::Role);
        client_setup.role_parameter.param_length = 0x1; // NOTE: not used for encode, size of value is used
        client_setup.role_parameter.param_value = { 0x03 };
        client_setup.endpoint_id_parameter.param_value.assign(_client_config.endpoint_id.begin(),
                                                              _client_config.endpoint_id.end());

        buffer << client_setup;

        auto &conn_ctx = _connections.begin()->second;

        send_ctrl_msg(conn_ctx, buffer.front(buffer.size()));
    }

    void MoQInstance::send_server_setup(ConnectionContext& conn_ctx)
    {
        StreamBuffer<uint8_t> buffer;
        auto server_setup = MoqServerSetup{};

        server_setup.selection_version = { conn_ctx.client_version };
        server_setup.role_parameter.param_type = static_cast<uint64_t>(ParameterType::Role);
        server_setup.role_parameter.param_length = 0x1; // NOTE: not used for encode, size of value is used
        server_setup.role_parameter.param_value = { 0x03 };
        server_setup.endpoint_id_parameter.param_value.assign(_server_config.endpoint_id.begin(),
                                                              _server_config.endpoint_id.end());


        buffer << server_setup;

        _logger->debug << "Sending SERVER_SETUP to conn_id: " << conn_ctx.conn_id << std::flush;

        send_ctrl_msg(conn_ctx, buffer.front(buffer.size()));
    }

    void MoQInstance::send_announce(ConnectionContext& conn_ctx, std::span<uint8_t const> track_namespace)
    {
        StreamBuffer<uint8_t> buffer;
        auto announce = MoqAnnounce{};

        announce.track_namespace.assign(track_namespace.begin(), track_namespace.end());
        announce.params = {};
        buffer <<  announce;

        std::vector<uint8_t> net_data = buffer.front(buffer.size());

        _logger->debug << "Sending ANNOUNCE to conn_id: " << conn_ctx.conn_id << std::flush;

        send_ctrl_msg(conn_ctx, buffer.front(buffer.size()));
    }

    void MoQInstance::send_announce_ok(ConnectionContext& conn_ctx, std::span<uint8_t const> track_namespace)
    {
        StreamBuffer<uint8_t> buffer;
        auto announce_ok = MoqAnnounceOk{};

        announce_ok.track_namespace.assign(track_namespace.begin(), track_namespace.end());
        buffer <<  announce_ok;

        std::vector<uint8_t> net_data = buffer.front(buffer.size());

        _logger->debug << "Sending ANNOUNCE OK to conn_id: " << conn_ctx.conn_id << std::flush;

        send_ctrl_msg(conn_ctx, buffer.front(buffer.size()));
    }

    void MoQInstance::send_subscribe(ConnectionContext& conn_ctx,
                                     uint64_t subscribe_id,
                                     TrackFullName& tfn,
                                     TrackHash th)
    {
        StreamBuffer<uint8_t> buffer;

        auto subscribe  = MoqSubscribe {};
        subscribe.subscribe_id = subscribe_id;
        subscribe.track_alias = th.track_fullname_hash;
        subscribe.track_namespace.assign(tfn.name_space.begin(), tfn.name_space.end());
        subscribe.track_name.assign(tfn.name.begin(), tfn.name.end());
        subscribe.filter_type = FilterType::LatestGroup;
        subscribe.num_params = 0;

        buffer << subscribe;

        std::vector<uint8_t> net_data = buffer.front(buffer.size());

        _logger->debug << "Sending SUBSCRIBE to conn_id: " << conn_ctx.conn_id
                       << " subscribe_id: " << subscribe_id
                       << " track namespace hash: " << th.track_namespace_hash
                       << " name hash: " << th.track_name_hash
                       << std::flush;

        send_ctrl_msg(conn_ctx, buffer.front(buffer.size()));
    }

    void MoQInstance::send_subscribe_ok(ConnectionContext& conn_ctx,
                                        uint64_t subscribe_id,
                                        uint64_t expires,
                                        bool content_exists)
    {
        StreamBuffer<uint8_t> buffer;

        auto subscribe_ok  = MoqSubscribeOk {};
        subscribe_ok.subscribe_id = subscribe_id;
        subscribe_ok.expires = expires;
        subscribe_ok.content_exists = content_exists;
        buffer << subscribe_ok;

        std::vector<uint8_t> net_data = buffer.front(buffer.size());

        _logger->debug << "Sending SUBSCRIBE OK to conn_id: " << conn_ctx.conn_id
                       << " subscribe_id: " << subscribe_id
                       << std::flush;

        send_ctrl_msg(conn_ctx, buffer.front(buffer.size()));
    }

    void MoQInstance::send_subscribe_error(ConnectionContext& conn_ctx,
                                           uint64_t subscribe_id,
                                           uint64_t track_alias,
                                           MoQSubscribeError error,
                                           const std::string& reason)
    {
        qtransport::StreamBuffer<uint8_t> buffer;

        auto subscribe_err  = MoqSubscribeError {};
        subscribe_err.subscribe_id = 0x1;
        subscribe_err.err_code = static_cast<uint64_t>(error);
        subscribe_err.track_alias = track_alias;
        subscribe_err.reason_phrase.assign(reason.begin(), reason.end());

        buffer << subscribe_err;

        std::vector<uint8_t> net_data = buffer.front(buffer.size());

        _logger->debug << "Sending SUBSCRIBE ERROR to conn_id: " << conn_ctx.conn_id
                       << " subscribe_id: " << subscribe_id
                       << " error code: " << static_cast<int>(error)
                       << " reason: " << reason
                       << std::flush;

        send_ctrl_msg(conn_ctx, buffer.front(buffer.size()));
    }

    MoQInstance::Status MoQInstance::status()
    {
        return _status;
    }

    bool MoQInstance::process_recv_message(ConnectionContext& conn_ctx,
                                           std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer)
    {
        if (stream_buffer->size() == 0) { // should never happen
            close_connection(conn_ctx.conn_id,
                             MoQTerminationReason::INTERNAL_ERROR,
                             "Stream buffer cannot be zero when parsing message type");
        }

        if (not conn_ctx.msg_type_received) { // should never happen
            close_connection(conn_ctx.conn_id,
                             MoQTerminationReason::INTERNAL_ERROR,
                             "Process recv message connection context is missing message type");
        }

        switch (*conn_ctx.msg_type_received) {
            case MoQMessageType::OBJECT_STREAM:
                break;
            case MoQMessageType::OBJECT_DATAGRAM:
                break;
            case MoQMessageType::SUBSCRIBE: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received subscribe, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqSubscribe>();
                }

                auto& msg = stream_buffer->getAny<MoqSubscribe>();
                if (*stream_buffer >> msg) {
                    auto tfn = TrackFullName{ msg.track_namespace, msg.track_name };
                    auto th = TrackHash(tfn);

                    // For client/publisher, notify track that there is a subscriber
                    if (_client_mode) {
                        auto ptd = getPubTrackDelegate(conn_ctx, th);
                        if (not ptd.has_value()) {
                            _logger->info << "Received subscribe unknown publish track"
                                          << " namespace hash: " << th.track_namespace_hash
                                          << " name hash: " << th.track_name_hash
                                          << std::flush;

                            send_subscribe_error(conn_ctx,
                                                 msg.subscribe_id,
                                                 msg.track_alias,
                                                 MoQSubscribeError::TRACK_NOT_EXIST,
                                                 "Published track not found");
                            return true;
                        }

                        // TODO(tievens): Set content exists
                        send_subscribe_ok(conn_ctx, msg.subscribe_id, MOQT_SUBSCRIBE_EXPIRES, false);

                        // Indicate send is ready upon subscribe
                        // TODO(tievens): Maybe needs a delay as subscriber may have not received ok before data is sent
                        ptd->lock().get()->cb_sendReady();

                    } else { // Server mode
                        if (msg.subscribe_id > conn_ctx._sub_id) {
                            conn_ctx._sub_id = msg.subscribe_id+1;
                        }

                        // TODO(tievens): add filter type when caching supports it
                        if (_delegate->cb_subscribe(conn_ctx.conn_id,
                                                    msg.subscribe_id,
                                                    tfn.name_space,
                                                    tfn.name)) {
                            send_subscribe_ok(conn_ctx, msg.subscribe_id, MOQT_SUBSCRIBE_EXPIRES, false);
                        }
                    }

                    stream_buffer->resetAny();
                    return true;
                }
                break;
            }
            case MoQMessageType::SUBSCRIBE_OK: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received subscribe ok, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqSubscribeOk>();
                }

                auto& msg = stream_buffer->getAny<MoqSubscribeOk>();
                if (*stream_buffer >> msg) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        _logger->info << "Received subsribe ok to unknown subscribe track"
                                      << " subscribe_id: " << msg.subscribe_id
                                      << " , ignored"
                                      << std::flush;

                        //TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race condition
                        stream_buffer->resetAny();
                        return true;
                    }

                    sub_it->second.get()->cb_readReady();

                    stream_buffer->resetAny();
                    return true;
                }
                break;
            }
            case MoQMessageType::SUBSCRIBE_ERROR: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received subscribe error, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqSubscribeError>();
                }

                auto& msg = stream_buffer->getAny<MoqSubscribeError>();
                if (*stream_buffer >> msg) {


                    stream_buffer->resetAny();
                    return true;
                }
                break;
            }
            case MoQMessageType::ANNOUNCE: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received announce, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqAnnounce>();
                }

                auto& msg = stream_buffer->getAny<MoqAnnounce>();
                if (*stream_buffer >> msg) {
                    auto tfn = TrackFullName{ msg.track_namespace, { } };
                    auto th = TrackHash(tfn);

                    if (_delegate->cb_announce(conn_ctx.conn_id, th.track_namespace_hash)) {
                        send_announce_ok(conn_ctx, msg.track_namespace);
                    }

                    stream_buffer->resetAny();
                    return true;
                }
                break;
            }
            case MoQMessageType::ANNOUNCE_OK: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received announce ok, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqAnnounceOk>();
                }

                auto& msg = stream_buffer->getAny<MoqAnnounceOk>();
                if (*stream_buffer >> msg) {
                    auto tfn = TrackFullName{ msg.track_namespace, { } };
                    auto th = TrackHash(tfn);
                    _logger->debug << "Received announce ok, namespace_hash: " << th.track_namespace_hash << std::flush;

                    // Update each track to indicate status is okay to publish
                    auto pub_it = conn_ctx.pub_tracks_by_name.find(th.track_namespace_hash);
                    for (const auto& td: pub_it->second) {
                        td.second.get()->setSendStatus(MoQTrackDelegate::TrackSendStatus::OK);
                        td.second.get()->cb_sendReady();
                    }

                    stream_buffer->resetAny();
                    return true;
                }
                break;
            }
            case MoQMessageType::ANNOUNCE_ERROR:
                break;
            case MoQMessageType::UNANNOUNCE:
                break;
            case MoQMessageType::UNSUBSCRIBE:
                break;
            case MoQMessageType::SUBSCRIBE_DONE:
                break;
            case MoQMessageType::ANNOUNCE_CANCEL:
                break;
            case MoQMessageType::TRACK_STATUS_REQUEST:
                break;
            case MoQMessageType::TRACK_STATUS:
                break;
            case MoQMessageType::GOWAY:
                break;
            case MoQMessageType::CLIENT_SETUP: {
                    if (not stream_buffer->anyHasValue()) {
                        _logger->debug << "Received client setup, init stream buffer" << std::flush;
                        stream_buffer->initAny<MoqClientSetup>();
                    }

                    auto& msg = stream_buffer->getAny<MoqClientSetup>();
                    if (*stream_buffer >> msg) {
                        if (!msg.supported_versions.size()) { // should never happen
                            close_connection(conn_ctx.conn_id,
                                             MoQTerminationReason::PROTOCOL_VIOLATION,
                                             "Client setup contained zero versions");

                        }

                        std::string client_endpoint_id(msg.endpoint_id_parameter.param_value.begin(),
                                                        msg.endpoint_id_parameter.param_value.end());

                        _delegate->cb_connectionStatus(conn_ctx.conn_id,
                                                       msg.endpoint_id_parameter.param_value,
                                                       TransportStatus::Ready);

                        _logger->info << "Client setup received from: " << client_endpoint_id
                                      << " num_versions: " << msg.num_versions
                                      << " role: " << static_cast<int>(msg.role_parameter.param_value.front())
                                      << " version: 0x" << std::hex << msg.supported_versions.front()
                                      << std::dec << std::flush;

                        conn_ctx.client_version = msg.supported_versions.front();
                        stream_buffer->resetAny();

#ifndef LIBQUICR_WITHOUT_INFLUXDB
                        _mexport.set_conn_ctx_info(conn_ctx.conn_id, {.endpoint_id = client_endpoint_id,
                                                                            .relay_id = _server_config.endpoint_id,
                                                                            .data_ctx_info = {}}, false);
                        _mexport.set_data_ctx_info(conn_ctx.conn_id, *conn_ctx.ctrl_data_ctx_id,
                                                   {.subscribe = false, .nspace = {}});
#endif

                        send_server_setup(conn_ctx);
                        conn_ctx.setup_complete = true;
                        return true;
                    }
                    break;
            }
            case MoQMessageType::SERVER_SETUP: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received server setup, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqServerSetup>();
                }

                auto& msg = stream_buffer->getAny<MoqServerSetup>();
                if (*stream_buffer >> msg) {
                    std::string server_endpoint_id(msg.endpoint_id_parameter.param_value.begin(),
                                msg.endpoint_id_parameter.param_value.end());

                    _delegate->cb_connectionStatus(conn_ctx.conn_id,
                               msg.endpoint_id_parameter.param_value,
                               TransportStatus::Ready);

                    _logger->info << "Server setup received"
                                  << " from: " << server_endpoint_id
                                  << " role: " << static_cast<int>(msg.role_parameter.param_value.front())
                                  << " selected_version: 0x" << std::hex << msg.selection_version
                                  << std::dec << std::flush;

#ifndef LIBQUICR_WITHOUT_INFLUXDB
                    _mexport.set_conn_ctx_info(conn_ctx.conn_id, {.endpoint_id = server_endpoint_id,
                                                                        .relay_id = _server_config.endpoint_id,
                                                                        .data_ctx_info = {}}, false);

                    _mexport.set_data_ctx_info(conn_ctx.conn_id, *conn_ctx.ctrl_data_ctx_id,
                                               {.subscribe = false, .nspace = {}});
#endif

                    stream_buffer->resetAny();
                    conn_ctx.setup_complete = true;


                    return true;
                }
                break;
            }
            case MoQMessageType::STREAM_HEADER_TRACK:
                break;
            case MoQMessageType::STREAM_HEADER_GROUP:
                break;
        }

        _logger->debug << " type: " << static_cast<int>(*conn_ctx.msg_type_received)
                      << " sbuf_size: " << stream_buffer->size()
                      << std::flush;
        return false;
    }

    std::optional<uint64_t> MoQInstance::subscribeTrack(TransportConnId conn_id,
                                                        std::shared_ptr<MoQTrackDelegate> track_delegate)
    {
        // Generate track alias
        auto tfn = TrackFullName{ track_delegate->getTrackNamespace(), track_delegate->getTrackName() };

        // Track hash is the track alias for now.
        // TODO(tievens): Evaluate; change hash to be more than 62 bits to avoid collisions
        auto th = TrackHash(tfn);

        track_delegate->setTrackAlias(th.track_fullname_hash);

        _logger->info << "Subscribe track conn_id: " << conn_id
                      << " hash: " << th.track_fullname_hash << std::flush;

        std::lock_guard<std::mutex> _(_state_mutex);
        auto conn_it = _connections.find(conn_id);
        if (conn_it == _connections.end()) {
            _logger->error << "Subscribe track conn_id: " << conn_id << " does not exist." << std::flush;
            return std::nullopt;
        }

        auto sid = conn_it->second._sub_id++;

        _logger->debug << "subscribe id to add to memory: " << sid << std::flush;

        // Set the track delegate for pub/sub using _sub_pub_id, which is the subscribe Id in MOQT
        conn_it->second.tracks_by_sub_id[sid] = track_delegate;

        track_delegate->setSubscribeId(sid);

        send_subscribe(conn_it->second, sid, tfn, th);

        return th.track_fullname_hash;
    }

    std::optional<uint64_t> MoQInstance::bindSubscribeTrack(TransportConnId conn_id,
                                                            uint64_t subscribe_id,
                                                            std::shared_ptr<MoQTrackDelegate> track_delegate) {

        // Generate track alias
        auto tfn = TrackFullName{ track_delegate->getTrackNamespace(), track_delegate->getTrackName() };

        // Track hash is the track alias for now.
        auto th = TrackHash(tfn);

        track_delegate->setTrackAlias(th.track_fullname_hash);

        _logger->info << "Bind subscribe track delegate conn_id: " << conn_id
                      << " hash: " << th.track_fullname_hash << std::flush;

        std::lock_guard<std::mutex> _(_state_mutex);
        auto conn_it = _connections.find(conn_id);
        if (conn_it == _connections.end()) {
            _logger->error << "Subscribe track conn_id: " << conn_id << " does not exist." << std::flush;
            return std::nullopt;
        }

        // Set the track delegate for pub/sub using _sub_pub_id, which is the subscribe Id in MOQT
        conn_it->second.tracks_by_sub_id[subscribe_id] = track_delegate;

        track_delegate->setSubscribeId(subscribe_id);

        return th.track_fullname_hash;
    }

    std::optional<uint64_t> MoQInstance::publishTrack(TransportConnId conn_id,
                                         std::shared_ptr<MoQTrackDelegate> track_delegate) {

        // Generate track alias
        auto tfn = TrackFullName{ track_delegate->getTrackNamespace(), track_delegate->getTrackName() };

        // Track hash is the track alias for now.
        // TODO(tievens): Evaluate; change hash to be more than 62 bits to avoid collisions
        auto th = TrackHash(tfn);

        track_delegate->setTrackAlias(th.track_fullname_hash);

        _logger->info << "Publish track conn_id: " << conn_id
                      << " hash: " << th.track_fullname_hash << std::flush;

        std::lock_guard<std::mutex> _(_state_mutex);

        auto conn_it = _connections.find(conn_id);
        if (conn_it == _connections.end()) {
            _logger->error << "Publish track conn_id: " << conn_id << " does not exist." << std::flush;
            return std::nullopt;
        }

        // Check if this published track is a new namespace or existing.
        auto pub_ns_it = conn_it->second.pub_tracks_by_name.find(th.track_namespace_hash);
        if (pub_ns_it == conn_it->second.pub_tracks_by_name.end()) {
            _logger->info << "Publish track has new namespace hash: " << th.track_namespace_hash
                          << " sending ANNOUNCE message" << std::flush;

            send_announce(conn_it->second, track_delegate->getTrackNamespace());

        } else {
            auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
            if (pub_n_it == pub_ns_it->second.end()) {
                _logger->info << "Publish track has new track "
                              << " namespace hash: " << th.track_namespace_hash
                              << " name hash: " << th.track_name_hash
                              << std::flush;
            }
        }

        // Set the track delegate for pub/sub
        conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_delegate;

        return th.track_fullname_hash;
    }

    std::optional<std::weak_ptr<MoQTrackDelegate>> MoQInstance::getPubTrackDelegate(ConnectionContext& conn_ctx,
                                                                                    TrackHash& th)
    {
        auto pub_ns_it = conn_ctx.pub_tracks_by_name.find(th.track_namespace_hash);
        if (pub_ns_it == conn_ctx.pub_tracks_by_name.end()) {
            return std::nullopt;
        } else {
            auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
            if (pub_n_it == pub_ns_it->second.end()) {
                return std::nullopt;
            }

            return pub_n_it->second;
        }
    }

    // ---------------------------------------------------------------------------------------
    // Transport delegate callbacks
    // ---------------------------------------------------------------------------------------

    void MoQInstance::on_connection_status(const TransportConnId& conn_id, const TransportStatus status)
    {
        _logger->debug << "Connection status conn_id: " << conn_id
                       << " status: " << static_cast<int>(status)
                       << std::flush;

        if (_client_mode) {
            auto& conn_ctx = _connections[conn_id];
            _logger->info << "Connection established, creating bi-dir stream and sending CLIENT_SETUP" << std::flush;

            conn_ctx.ctrl_data_ctx_id = _transport->createDataContext(conn_id, true, 0, true);
#ifndef LIBQUICR_WITHOUT_INFLUXDB
            _mexport.set_data_ctx_info(conn_ctx.conn_id, *conn_ctx.ctrl_data_ctx_id,
                                       {.subscribe = false, .nspace = {}});
#endif

            send_client_setup();
        }

        _status = Status::READY;
    }

    void MoQInstance::on_new_connection(const TransportConnId& conn_id, const TransportRemote& remote)
    {
        auto [conn_ctx, is_new] = _connections.try_emplace(conn_id, ConnectionContext{});

        _logger->info << "New connection conn_id: " << conn_id
                      << " remote ip: " << remote.host_or_ip
                      << " port: " << remote.port
                      << std::flush;

        conn_ctx->second.conn_id = conn_id;
    }

    void MoQInstance::on_recv_stream(const TransportConnId& conn_id,
                        uint64_t stream_id,
                        std::optional<DataContextId> data_ctx_id,
                        const bool is_bidir)
    {
        auto stream_buf = _transport->getStreamBuffer(conn_id, stream_id);
        auto& conn_ctx = _connections[conn_id];

        if (stream_buf == nullptr) {
            return;
        }

        if (is_bidir && not conn_ctx.ctrl_data_ctx_id) {
            if (not data_ctx_id) {
                close_connection(conn_id,
                                 MoQTerminationReason::INTERNAL_ERROR,
                                 "Received bidir is missing data context");
                return;
            }
            conn_ctx.ctrl_data_ctx_id = data_ctx_id;
        }

        while (stream_buf->size()) {
            if (not conn_ctx.msg_type_received) {
                auto msg_type = stream_buf->decode_uintV();

                if (msg_type) {
                    conn_ctx.msg_type_received = static_cast<MoQMessageType>(*msg_type);
                } else {
                    break;
                }
            }

            if (conn_ctx.msg_type_received) {
                if (process_recv_message(conn_ctx, stream_buf)) {
                    conn_ctx.msg_type_received = std::nullopt;
                }
            }
        }
    }

    void MoQInstance::on_recv_dgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id)
    {
        _logger->info << "datagram data conn_id: " << conn_id
                      << " data_ctx_id: " << (data_ctx_id ? *data_ctx_id : 0)
                      << std::flush;

    }

    void MoQInstance::close_connection(TransportConnId conn_id, messages::MoQTerminationReason reason,
                                       const std::string& reason_str)
    {
        _logger->info << "Closing conn_id: " << conn_id;
        switch (reason) {
            case MoQTerminationReason::NO_ERROR:
                _logger->info << " no error";
                break;
            case MoQTerminationReason::INTERNAL_ERROR:
                _logger->info << " internal error: " << reason_str;
                break;
            case MoQTerminationReason::UNAUTHORIZED:
                _logger->info << " unauthorized: " << reason_str;
                break;
            case MoQTerminationReason::PROTOCOL_VIOLATION:
                _logger->info << " protocol violation: " << reason_str;
            break;
            case MoQTerminationReason::DUP_TRACK_ALIAS:
                _logger->info << " duplicate track alias: " << reason_str;
                break;
            case MoQTerminationReason::PARAM_LEN_MISMATCH:
                _logger->info << " param length mismatch: " << reason_str;
                break;
            case MoQTerminationReason::GOAWAY_TIMEOUT:
                _logger->info << " goaway timeout: " << reason_str;
                break;
        }
        _logger->info << std::flush;

        _transport->close(conn_id, static_cast<uint64_t>(reason));

        if (_client_mode) {
            _logger->info << "Client connection closed, stopping client" << std::flush;
            _stop = true;
        }
    }


} // namespace quicr
