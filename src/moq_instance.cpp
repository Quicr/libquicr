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
        , _logger(std::make_shared<cantina::Logger>("MIC", logger))
#ifndef LIBQUICR_WITHOUT_INFLUXDB
        , _mexport(logger)
#endif
        , _delegate(std::move(delegate))
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
      , _logger(std::make_shared<cantina::Logger>("MIS", logger))
#ifndef LIBQUICR_WITHOUT_INFLUXDB
        , _mexport(logger)
#endif
      , _delegate(std::move(delegate))
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

    void MoQInstance::send_unannounce(ConnectionContext& conn_ctx, std::span<uint8_t const> track_namespace)
    {
        StreamBuffer<uint8_t> buffer;
        auto unannounce = MoqUnannounce{};

        unannounce.track_namespace.assign(track_namespace.begin(), track_namespace.end());
        buffer <<  unannounce;

        std::vector<uint8_t> net_data = buffer.front(buffer.size());

        _logger->debug << "Sending UNANNOUNCE to conn_id: " << conn_ctx.conn_id << std::flush;

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

    void MoQInstance::send_subscribe_done(ConnectionContext& conn_ctx,
                                          uint64_t subscribe_id,
                                          const std::string& reason)
    {
        StreamBuffer<uint8_t> buffer;

        auto subscribe_done  = MoqSubscribeDone {};
        subscribe_done.subscribe_id = subscribe_id;
        subscribe_done.reason_phrase.assign(reason.begin(), reason.end());
        subscribe_done.content_exists = false;
        buffer << subscribe_done;

        std::vector<uint8_t> net_data = buffer.front(buffer.size());

        _logger->debug << "Sending SUBSCRIBE DONE to conn_id: " << conn_ctx.conn_id
                       << " subscribe_id: " << subscribe_id
                       << std::flush;

        send_ctrl_msg(conn_ctx, buffer.front(buffer.size()));
    }


    void MoQInstance::send_unsubscribe(ConnectionContext& conn_ctx,
                                       uint64_t subscribe_id)
    {
        StreamBuffer<uint8_t> buffer;

        auto unsubscribe  = MoqUnsubscribe {};
        unsubscribe.subscribe_id = subscribe_id;
        buffer << unsubscribe;

        std::vector<uint8_t> net_data = buffer.front(buffer.size());

        _logger->debug << "Sending UNSUBSCRIBE to conn_id: " << conn_ctx.conn_id
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

    bool MoQInstance::process_recv_ctrl_message(ConnectionContext& conn_ctx,
                                           std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer)
    {
        if (stream_buffer->size() == 0) { // should never happen
            close_connection(conn_ctx.conn_id,
                             MoQTerminationReason::INTERNAL_ERROR,
                             "Stream buffer cannot be zero when parsing message type");
        }

        if (not conn_ctx.ctrl_msg_type_received) { // should never happen
            close_connection(conn_ctx.conn_id,
                             MoQTerminationReason::INTERNAL_ERROR,
                             "Process recv message connection context is missing message type");
        }

        switch (*conn_ctx.ctrl_msg_type_received) {
            case MoQMessageType::SUBSCRIBE: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received subscribe, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqSubscribe>();
                }

                auto& msg = stream_buffer->getAny<MoqSubscribe>();
                if (*stream_buffer >> msg) {
                    auto tfn = TrackFullName{ msg.track_namespace, msg.track_name };
                    auto th = TrackHash(tfn);

                    if (msg.subscribe_id > conn_ctx._sub_id) {
                        conn_ctx._sub_id = msg.subscribe_id + 1;
                    }

                    // For client/publisher, notify track that there is a subscriber
                    if (_client_mode) {
                        auto ptd = getPubTrackDelegate(conn_ctx, th);
                        if (not ptd.has_value()) {
                            _logger->warning
                              << "Received subscribe unknown publish track" << " conn_id:" << conn_ctx.conn_id
                              << " namespace hash: " << th.track_namespace_hash << " name hash: " << th.track_name_hash
                              << std::flush;

                            send_subscribe_error(conn_ctx,
                                                 msg.subscribe_id,
                                                 msg.track_alias,
                                                 MoQSubscribeError::TRACK_NOT_EXIST,
                                                 "Published track not found");
                            return true;
                        }

                        send_subscribe_ok(conn_ctx, msg.subscribe_id, MOQT_SUBSCRIBE_EXPIRES, false);

                        _logger->debug << "Received subscribe to announced track alias: " << msg.track_alias
                                       << " recv subscribe_id: " << msg.subscribe_id
                                       << ", setting send"
                                          ""
                                          " state to ready"
                                       << std::flush;

                        // Indicate send is ready upon subscribe
                        // TODO(tievens): Maybe needs a delay as subscriber may have not received ok before data is sent
                        ptd->lock()->setSubscribeId(msg.subscribe_id);
                        ptd->lock()->setSendStatus(MoQTrackDelegate::TrackSendStatus::OK);
                        ptd->lock()->cb_sendReady();

                        conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };

                    } else { // Server mode
                        // TODO(tievens): add filter type when caching supports it
                        if (_delegate->cb_subscribe(conn_ctx.conn_id, msg.subscribe_id, tfn.name_space, tfn.name)) {
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
                        _logger->warning << "Received subscribe ok to unknown subscribe track"
                                         << " conn_id:" << conn_ctx.conn_id << " subscribe_id: " << msg.subscribe_id
                                         << " , ignored" << std::flush;

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race condition
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
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        _logger->warning << "Received subscribe error to unknown subscribe_id"
                                         << " conn_id:" << conn_ctx.conn_id << " subscribe_id: " << msg.subscribe_id
                                         << " , ignored" << std::flush;

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race condition
                        stream_buffer->resetAny();
                        return true;
                    }

                    sub_it->second.get()->cb_readNotReady(MoQTrackDelegate::TrackReadStatus::SUBSCRIBE_ERROR);
                    remove_subscribeTrack(conn_ctx, *sub_it->second);

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
                    auto tfn = TrackFullName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    if (_delegate->cb_announce(conn_ctx.conn_id, th.track_namespace_hash)) {
                        send_announce_ok(conn_ctx, msg.track_namespace);
                        _delegate->cb_announce_post(conn_ctx.conn_id, th.track_namespace_hash);
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
                    auto tfn = TrackFullName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);
                    _logger->debug << "Received announce ok, " << " conn_id:" << conn_ctx.conn_id
                                   << " namespace_hash: " << th.track_namespace_hash << std::flush;

                    // Update each track to indicate status is okay to publish
                    auto pub_it = conn_ctx.pub_tracks_by_name.find(th.track_namespace_hash);
                    for (const auto& td : pub_it->second) {
                        if (td.second.get()->statusSend() != MoQTrackDelegate::TrackSendStatus::OK)
                            td.second.get()->setSendStatus(MoQTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                    }

                    stream_buffer->resetAny();
                    return true;
                }
                break;
            }
            case MoQMessageType::ANNOUNCE_ERROR: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received announce error, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqAnnounceError>();
                }

                auto& msg = stream_buffer->getAny<MoqAnnounceError>();
                if (*stream_buffer >> msg) {
                    if (msg.track_namespace) {
                        std::string reason = "unknown";
                        auto tfn = TrackFullName{ *msg.track_namespace, {} };
                        auto th = TrackHash(tfn);

                        if (msg.reason_phrase) {
                            reason.assign(msg.reason_phrase->begin(), msg.reason_phrase->end());
                        }

                        _logger->info << "Received announce error for namespace_hash: " << th.track_namespace_hash
                                      << " error code: " << (msg.err_code.has_value() ? *msg.err_code : 0)
                                      << " reason: " << reason << std::flush;

                        stream_buffer->resetAny();
                        return true;
                    }
                }

                break;
            }
            case MoQMessageType::UNANNOUNCE: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received unannounce, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqUnannounce>();
                }

                auto& msg = stream_buffer->getAny<MoqUnannounce>();
                if (*stream_buffer >> msg) {
                    auto tfn = TrackFullName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    _logger->info << "Received unannounce for namespace_hash: " << th.track_namespace_hash
                                  << std::flush;

                    _delegate->cb_unannounce(conn_ctx.conn_id, th.track_namespace_hash, std::nullopt);

                    stream_buffer->resetAny();
                    return true;
                }

                break;
            }
            case MoQMessageType::UNSUBSCRIBE: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received unsubscribe, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqUnsubscribe>();
                }

                auto& msg = stream_buffer->getAny<MoqUnsubscribe>();
                if (*stream_buffer >> msg) {
                    if (!_client_mode) {
                        auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                        if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                            _logger->warning << "Received unsubscribe to unknown subscribe_id"
                                             << " conn_id:" << conn_ctx.conn_id << " subscribe_id: " << msg.subscribe_id
                                             << " , ignored" << std::flush;

                            // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race condition
                            stream_buffer->resetAny();
                            return true;
                        }

                        _logger->debug << "Received unsubscribe conn_id: " << conn_ctx.conn_id
                                       << " subscribe_id: " << msg.subscribe_id << std::flush;
                        sub_it->second.get()->cb_readNotReady(MoQTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);

                        _delegate->cb_unsubscribe(conn_ctx.conn_id, msg.subscribe_id);

                        remove_subscribeTrack(conn_ctx, *sub_it->second);
                    } else {
                        const auto& [name_space, name] = conn_ctx.recv_sub_id[msg.subscribe_id];
                        TrackHash th(name_space, name);
                        if (auto pdt = getPubTrackDelegate(conn_ctx, th)) {
                            pdt->lock()->setSendStatus(MoQTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                            pdt->lock()->cb_sendNotReady(MoQTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                        }

                        conn_ctx.recv_sub_id.erase(msg.subscribe_id);
                    }

                    stream_buffer->resetAny();
                    return true;
                }

                break;
            }
            case MoQMessageType::SUBSCRIBE_DONE: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received subscribe done, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqSubscribeDone>();
                }

                auto& msg = stream_buffer->getAny<MoqSubscribeDone>();
                if (*stream_buffer >> msg) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        _logger->warning << "Received subscribe done to unknown subscribe_id"
                                         << " conn_id:" << conn_ctx.conn_id << " subscribe_id: " << msg.subscribe_id
                                         << " , ignored" << std::flush;

                        // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race condition
                        stream_buffer->resetAny();
                        return true;
                    }
                    auto tfn = TrackFullName{ sub_it->second->getTrackNamespace(), sub_it->second->getTrackNamespace() };
                    auto th = TrackHash(tfn);

                    _logger->debug << "Received subscribe done conn_id: " << conn_ctx.conn_id
                                   << " subscribe_id: " << msg.subscribe_id
                                   << " track namespace hash: " << th.track_namespace_hash
                                   << " name hash: " << th.track_name_hash
                                   << " track alias: " << th.track_fullname_hash
                                   << std::flush;

                    sub_it->second.get()->cb_readNotReady(MoQTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);
                    _delegate->cb_unannounce(conn_ctx.conn_id, th.track_namespace_hash, th.track_name_hash);

                    stream_buffer->resetAny();
                    return true;
                }
                break;
            }
            case MoQMessageType::ANNOUNCE_CANCEL: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received announce cancel, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqAnnounceCancel>();
                }

                auto& msg = stream_buffer->getAny<MoqAnnounceCancel>();
                if (*stream_buffer >> msg) {
                    auto tfn = TrackFullName{ msg.track_namespace, {} };
                    auto th = TrackHash(tfn);

                    _logger->info << "Received announce cancel for namespace_hash: " << th.track_namespace_hash
                                  << std::flush;

                    stream_buffer->resetAny();
                    return true;
                }

                break;
            }
            case MoQMessageType::TRACK_STATUS_REQUEST: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received track status request, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqTrackStatusRequest>();
                }

                auto& msg = stream_buffer->getAny<MoqTrackStatusRequest>();
                if (*stream_buffer >> msg) {
                    auto tfn = TrackFullName{ msg.track_namespace, msg.track_name };
                    auto th = TrackHash(tfn);

                    _logger->info << "Received track status request for "
                                  << " namespace_hash: " << th.track_namespace_hash
                                  << " name_hash: " << th.track_name_hash
                                  << std::flush;

                    stream_buffer->resetAny();
                    return true;
                }

                break;
            }
            case MoQMessageType::TRACK_STATUS: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received track status, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqTrackStatus>();
                }

                auto& msg = stream_buffer->getAny<MoqTrackStatus>();
                if (*stream_buffer >> msg) {
                    auto tfn = TrackFullName{ msg.track_namespace, msg.track_name };
                    auto th = TrackHash(tfn);

                    _logger->info << "Received track status for "
                                  << " namespace_hash: " << th.track_namespace_hash
                                  << " name_hash: " << th.track_name_hash
                                  << std::flush;

                    stream_buffer->resetAny();
                    return true;
                }

                break;
            }
            case MoQMessageType::GOWAY: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received goaway, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqGoaway>();
                }

                auto& msg = stream_buffer->getAny<MoqGoaway>();
                if (*stream_buffer >> msg) {
                    std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                    _logger->info << "Received goaway "
                                  << " new session uri: " << new_sess_uri
                                  << std::flush;

                    stream_buffer->resetAny();
                    return true;
                }

                break;
            }
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

                        _logger->info << "Client setup received "
                                      << " conn_id:" << conn_ctx.conn_id
                                      << " from: " << client_endpoint_id
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
                                  << " conn_id:" << conn_ctx.conn_id
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

            default:
                _logger->error << "Unsupported MOQT message "
                               << "type: " << static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received)
                               << std::flush;
                close_connection(conn_ctx.conn_id,
                                 MoQTerminationReason::PROTOCOL_VIOLATION,
                                 "Unsupported MOQT message type");
                return true;

        } // End of switch(msg type)

        _logger->debug << " type: " << static_cast<int>(*conn_ctx.ctrl_msg_type_received)
                      << " sbuf_size: " << stream_buffer->size()
                      << std::flush;
        return false;
    }

    bool MoQInstance::process_recv_stream_data_message(ConnectionContext& conn_ctx,
                                                       std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer)
    {
        if (stream_buffer->size() == 0) { // should never happen
            close_connection(conn_ctx.conn_id,
                             MoQTerminationReason::INTERNAL_ERROR,
                             "Stream buffer cannot be zero when parsing message type");
        }

        // Header not set, get the header for this stream or datagram
        MoQMessageType data_type;
        if (!stream_buffer->anyHasValue()) {
            auto val = stream_buffer->decode_uintV();
            if (val) {
                data_type = static_cast<MoQMessageType>(*val);
            } else {
                return false;
            }
        } else {
            auto dt = stream_buffer->getAnyType();
            if (dt.has_value()) {
                data_type = static_cast<MoQMessageType>(*dt);
            }
            else {
                _logger->warning << "Unknown data type for data stream" << std::flush;
                return true;
            }
        }

        switch (data_type) {
            case MoQMessageType::OBJECT_STREAM: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received stream header object, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqObjectStream>(
                      static_cast<uint64_t>(MoQMessageType::OBJECT_STREAM));
                }

                auto& msg = stream_buffer->getAny<MoqObjectStream>();
                if (*stream_buffer >> msg) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        _logger->warning << "Received stream_object to unknown subscribe track"
                                         << " subscribe_id: " << msg.subscribe_id << " , ignored" << std::flush;

                        // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                        return true;
                    }

                    _logger->debug << "Received stream_object " << " subscribe_id: " << msg.subscribe_id
                                   << " priority: " << msg.priority << " track_alias: " << msg.track_alias
                                   << " group_id: " << msg.group_id << " object_id: " << msg.object_id
                                   << " data size: " << msg.payload.size() << std::flush;
                    sub_it->second->cb_objectReceived(msg.group_id, msg.object_id, msg.priority,
                                                      std::move(msg.payload),
                                                      MoQTrackDelegate::TrackMode::STREAM_PER_OBJECT);
                    stream_buffer->resetAny();
                }
                break;
            }

            case MoQMessageType::STREAM_HEADER_TRACK: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received stream header track, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqStreamHeaderTrack>(
                      static_cast<uint64_t>(MoQMessageType::STREAM_HEADER_TRACK));
                }

                auto& msg = stream_buffer->getAny<MoqStreamHeaderTrack>();
                if (!stream_buffer->anyHasValueB() && *stream_buffer >> msg) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        _logger->warning << "Received stream_header_track to unknown subscribe track"
                                         << " subscribe_id: " << msg.subscribe_id << " , ignored" << std::flush;

                        // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                        return true;
                    }

                    // Init second working buffer to read data object
                    stream_buffer->initAnyB<MoqStreamTrackObject>();

                    _logger->debug << "Received stream_header_track " << " subscribe_id: " << msg.subscribe_id
                                   << " priority: " << msg.priority << " track_alias: " << msg.track_alias
                                   << std::flush;
                }

                if (stream_buffer->anyHasValueB()) {
                    MoqStreamTrackObject obj;
                    if (*stream_buffer >> obj) {
                        auto sub_it


                          = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                        if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                            _logger->warning << "Received stream_header_group to unknown subscribe track"
                                             << " subscribe_id: " << msg.subscribe_id << " , ignored" << std::flush;

                            // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                            return true;
                        }

                        _logger->debug << "Received stream_track_object " << " subscribe_id: " << msg.subscribe_id
                                       << " priority: " << msg.priority << " track_alias: " << msg.track_alias
                                       << " group_id: " << obj.group_id << " object_id: " << obj.object_id
                                       << " data size: " << obj.payload.size() << std::flush;
                        stream_buffer->resetAnyB();

                        sub_it->second->cb_objectReceived(obj.group_id, obj.object_id, msg.priority,
                                                          std::move(obj.payload),
                                                          MoQTrackDelegate::TrackMode::STREAM_PER_TRACK);
                    }
                }
                break;
            }
            case MoQMessageType::STREAM_HEADER_GROUP: {
                if (not stream_buffer->anyHasValue()) {
                    _logger->debug << "Received stream header group, init stream buffer" << std::flush;
                    stream_buffer->initAny<MoqStreamHeaderGroup>(static_cast<uint64_t>(MoQMessageType::STREAM_HEADER_GROUP));
                }

                auto& msg = stream_buffer->getAny<MoqStreamHeaderGroup>();
                if (!stream_buffer->anyHasValueB() && *stream_buffer >> msg) {
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        _logger->warning << "Received stream_header_group to unknown subscribe track"
                                  << " subscribe_id: " << msg.subscribe_id
                                  << " , ignored"
                                  << std::flush;

                        // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                        return true;
                    }

                    // Init second working buffer to read data object
                    stream_buffer->initAnyB<MoqStreamGroupObject>();

                    _logger->debug << "Received stream_header_group "
                                   << " subscribe_id: " << msg.subscribe_id
                                   << " priority: " << msg.priority
                                   << " track_alais: " << msg.track_alias
                                   << " group_id: " << msg.group_id
                                   << std::flush;
                }

                if (stream_buffer->anyHasValueB()) {
                    MoqStreamGroupObject obj;
                    if (*stream_buffer >> obj) {
                        auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                        if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                            _logger->warning << "Received stream_header_group to unknown subscribe track"
                                      << " subscribe_id: " << msg.subscribe_id
                                      << " , ignored"
                                      << std::flush;

                            // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                            return true;
                        }

                        _logger->debug << "Received stream_group_object "
                                       << " subscribe_id: " << msg.subscribe_id
                                       << " priority: " << msg.priority
                                       << " track_alais: " << msg.track_alias
                                       << " group_id: " << msg.group_id
                                       << " object_id: " << obj.object_id
                                       << " data size: " << obj.payload.size()
                                       << std::flush;
                        stream_buffer->resetAnyB();

                        sub_it->second->cb_objectReceived(msg.group_id, obj.object_id, msg.priority,
                                                          std::move(obj.payload),
                                                          MoQTrackDelegate::TrackMode::STREAM_PER_GROUP);
                    }
                }

                break;
            }

            default:
                // Process the stream object type
                /*
                _logger->error << "Unsupported MOQT data message "
                               << "type: " << static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received)
                               << std::flush;
                close_connection(conn_ctx.conn_id,
                                 MoQTerminationReason::PROTOCOL_VIOLATION,
                                 "Unsupported MOQT data message type");
                 */
                return true;

        }

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

        track_delegate->_mi_conn_id = conn_id;

        track_delegate->_mi_send_data_ctx_id = _transport->createDataContext(
          conn_id,
          track_delegate->_mi_track_mode == MoQTrackDelegate::TrackMode::DATAGRAM ? false : true,
          track_delegate->_def_priority,
          false);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
        Name n;
        n += th.track_fullname_hash;

        _mexport.set_data_ctx_info(conn_id, track_delegate->_mi_send_data_ctx_id,
                                   {.subscribe = false, .nspace = {n, 64} } );
#endif

        // Setup the function for the track delegate to use to send objects with thread safety
        track_delegate->_mi_sendObjFunc =
          [&,
           track_delegate = track_delegate,
           subscribe_id = track_delegate->getSubscribeId()](uint8_t priority,
                                                                        uint32_t ttl,
                                                                        bool stream_header_needed,
                                                                        uint64_t group_id,
                                                                        uint64_t object_id,
                                                                        std::span<uint8_t const> data) -> MoQTrackDelegate::SendError {
            return send_object(track_delegate,
                               priority,
                               ttl,
                               stream_header_needed,
                               group_id,
                               object_id,
                               data);
        };


        return th.track_fullname_hash;
    }

    void MoQInstance::unsubscribeTrack(qtransport::TransportConnId conn_id, std::shared_ptr<MoQTrackDelegate> track_delegate)
    {
        auto& conn_ctx = _connections[conn_id];
        if (track_delegate->getSubscribeId().has_value()) {
            send_unsubscribe(conn_ctx, *track_delegate->getSubscribeId());
        }
        remove_subscribeTrack(conn_ctx,*track_delegate);
    }

    void MoQInstance::remove_subscribeTrack(ConnectionContext& conn_ctx,
                                          MoQTrackDelegate& delegate, bool remove_delegate)
    {
        delegate.setReadStatus(MoQTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);
        delegate.setSubscribeId(std::nullopt);

        auto subscribe_id = delegate.getSubscribeId();
        if (subscribe_id.has_value()) {

            send_unsubscribe(conn_ctx, *subscribe_id);

            _logger->debug << "remove subscribe id: " << *subscribe_id << std::flush;

            _transport->deleteDataContext(conn_ctx.conn_id, delegate._mi_send_data_ctx_id);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
            _mexport.del_data_ctx_info(conn_ctx.conn_id, delegate._mi_send_data_ctx_id);
#endif
            delegate._mi_send_data_ctx_id = 0;


            if (remove_delegate) {
                std::lock_guard<std::mutex> _(_state_mutex);
                conn_ctx.tracks_by_sub_id.erase(*subscribe_id);
            }
        }
    }

    void MoQInstance::unpublishTrack(TransportConnId conn_id,
                                     std::shared_ptr<MoQTrackDelegate> track_delegate) {

        // Generate track alias
        auto tfn = TrackFullName{ track_delegate->getTrackNamespace(), track_delegate->getTrackName() };
        auto th = TrackHash(tfn);

        _logger->info << "Unpublish track conn_id: " << conn_id
                      << " hash: " << th.track_fullname_hash << std::flush;

        std::lock_guard<std::mutex> _(_state_mutex);

        auto conn_it = _connections.find(conn_id);
        if (conn_it == _connections.end()) {
            _logger->error << "Unpublish track conn_id: " << conn_id << " does not exist." << std::flush;
            return;
        }

        // Check if this published track is a new namespace or existing.
        auto pub_ns_it = conn_it->second.pub_tracks_by_name.find(th.track_namespace_hash);
        if (pub_ns_it != conn_it->second.pub_tracks_by_name.end()) {
            auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
            if (pub_n_it != pub_ns_it->second.end()) {

                // Send subscribe done if track has subscriber and is sending
                if (pub_n_it->second->getSendStatus() == MoQTrackDelegate::TrackSendStatus::OK
                    && pub_n_it->second->getSubscribeId().has_value()) {
                    _logger->info << "Unpublish track namespace hash: " << th.track_namespace_hash
                                  << " track_name_hash: " << th.track_name_hash
                                  << " track_alias: " << th.track_fullname_hash
                                  << ", sending subscribe_done"
                                  << std::flush;
                    send_subscribe_done(conn_it->second, *pub_n_it->second->getSubscribeId(), "Unpublish track");
                } else {
                    _logger->info << "Unpublish track namespace hash: " << th.track_namespace_hash
                                  << " track_name_hash: " << th.track_name_hash
                                  << " track_alias: " << th.track_fullname_hash
                                  << std::flush;
                }

#ifndef LIBQUICR_WITHOUT_INFLUXDB
                _mexport.del_data_ctx_info(conn_id, track_delegate->_mi_send_data_ctx_id);
#endif
                pub_n_it->second->_mi_send_data_ctx_id = 0;

                pub_n_it->second->setSendStatus(MoQTrackDelegate::TrackSendStatus::NOT_ANNOUNCED);
                pub_ns_it->second.erase(pub_n_it);
            }

            if (!pub_ns_it->second.size()) {
                _logger->info << "Unpublish namespace hash: " << th.track_namespace_hash
                              << ", has no tracks, sending unannounce" << std::flush;
                send_unannounce(conn_it->second, track_delegate->getTrackNamespace());
                conn_it->second.pub_tracks_by_name.erase(pub_ns_it);
            }
        }
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

            track_delegate->setSendStatus(MoQTrackDelegate::TrackSendStatus::PENDING_ANNOUNCE_RESPONSE);
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

        track_delegate->_mi_conn_id = conn_id;
        track_delegate->_mi_send_data_ctx_id = _transport->createDataContext(
          conn_id,
          track_delegate->_mi_track_mode == MoQTrackDelegate::TrackMode::DATAGRAM ? false : true,
          track_delegate->_def_priority,
          false);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
        Name n;
        n += th.track_fullname_hash;

        _mexport.set_data_ctx_info(conn_id, track_delegate->_mi_send_data_ctx_id,
                                   {.subscribe = false, .nspace = {n, 64} } );
#endif


        // Setup the function for the track delegate to use to send objects with thread safety
        track_delegate->_mi_sendObjFunc =
          [&,
           track_delegate = track_delegate,
           subscribe_id = track_delegate->getSubscribeId()](uint8_t priority,
                                                            uint32_t ttl,
                                                            bool stream_header_needed,
                                                            uint64_t group_id,
                                                            uint64_t object_id,
                                                            std::span<const uint8_t> data) -> MoQTrackDelegate::SendError {
            return send_object(track_delegate,
                               priority,
                               ttl,
                               stream_header_needed,
                               group_id,
                               object_id,
                               data);
        };

        return th.track_fullname_hash;
    }

    MoQTrackDelegate::SendError MoQInstance::send_object(std::weak_ptr<MoQTrackDelegate> track_delegate,
                                                         uint8_t priority,
                                                         uint32_t ttl,
                                                         bool stream_header_needed,
                                                         uint64_t group_id,
                                                         uint64_t object_id,
                                                         std::span<const uint8_t> data)
    {

        auto td = track_delegate.lock();

        if (!td->getTrackAlias().has_value()) {
            return MoQTrackDelegate::SendError::NOT_ANNOUNCED;
        }

        if (!td->getSubscribeId().has_value()) {
            return MoQTrackDelegate::SendError::NO_SUBSCRIBERS;
        }

        ITransport::EnqueueFlags eflags;

        StreamBuffer<uint8_t> buffer;

        switch(td->_mi_track_mode) {
            case MoQTrackDelegate::TrackMode::DATAGRAM: {
                MoqObjectDatagram object;
                object.group_id = group_id;
                object.object_id = object_id;
                object.priority = priority;
                object.subscribe_id = *td->getSubscribeId();
                object.track_alias = *td->getTrackAlias();
                object.payload.assign(data.begin(), data.end());
                buffer << object;
                break;
            }
            case MoQTrackDelegate::TrackMode::STREAM_PER_OBJECT: {
                eflags.use_reliable = true;
                eflags.new_stream = true;

                MoqObjectStream object;
                object.group_id = group_id;
                object.object_id = object_id;
                object.priority = priority;
                object.subscribe_id = *td->getSubscribeId();
                object.track_alias = *td->getTrackAlias();
                object.payload.assign(data.begin(), data.end());
                buffer << object;

                break;
            }

            case MoQTrackDelegate::TrackMode::STREAM_PER_GROUP: {
                eflags.use_reliable = true;

                if (stream_header_needed) {
                    eflags.new_stream = true;
                    eflags.clear_tx_queue = true;
                    eflags.use_reset = true;

                    MoqStreamHeaderGroup group_hdr;
                    group_hdr.group_id = group_id;
                    group_hdr.priority = priority;
                    group_hdr.subscribe_id = *td->getSubscribeId();
                    group_hdr.track_alias = *td->getTrackAlias();
                    buffer << group_hdr;
                }

                MoqStreamGroupObject object;
                object.object_id = object_id;
                object.payload.assign(data.begin(), data.end());
                buffer << object;

                break;
            }
            case MoQTrackDelegate::TrackMode::STREAM_PER_TRACK: {
                eflags.use_reliable = true;

                if (stream_header_needed) {
                    eflags.new_stream = true;

                    MoqStreamHeaderTrack track_hdr;
                    track_hdr.priority = priority;
                    track_hdr.subscribe_id = *td->getSubscribeId();
                    track_hdr.track_alias = *td->getTrackAlias();
                    buffer << track_hdr;
                }

                MoqStreamTrackObject object;
                object.group_id = group_id;
                object.object_id = object_id;
                object.payload.assign(data.begin(), data.end());
                buffer << object;

                break;
            }
        }

        // TODO(tievens): Add M10x specific chunking... lacking in the draft
        std::vector<uint8_t> serialized_data = buffer.front(buffer.size());

        _transport->enqueue(td->_mi_conn_id, td->_mi_send_data_ctx_id, std::move(serialized_data),
                    { MethodTraceItem{} }, priority,
                    ttl, 0, eflags);

        return MoQTrackDelegate::SendError::OK;
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

        switch (status) {
            case TransportStatus::Ready: {
                if (_client_mode) {
                    auto& conn_ctx = _connections[conn_id];
                    _logger->info << "Connection established, creating bi-dir stream and sending CLIENT_SETUP"
                                  << std::flush;

                    conn_ctx.ctrl_data_ctx_id = _transport->createDataContext(conn_id, true, 0, true);
#ifndef LIBQUICR_WITHOUT_INFLUXDB
                    _mexport.set_data_ctx_info(
                      conn_ctx.conn_id, *conn_ctx.ctrl_data_ctx_id, { .subscribe = false, .nspace = {} });
#endif

                    send_client_setup();
                    _status = Status::READY;
                }
                break;
            }

            case TransportStatus::Connecting:
                _status = Status::CLIENT_CONNECTING;
                break;
            case TransportStatus::RemoteRequestClose:
                [[fallthrough]];

            case TransportStatus::Disconnected: {

                // Clean up publish and subscribe tracks
                std::lock_guard<std::mutex> _(_state_mutex);
                auto conn_it = _connections.find(conn_id);

                if (conn_it == _connections.end()) {
                    return;
                }

                for (const auto& [sub_id, delegate] : conn_it->second.tracks_by_sub_id) {
                    delegate->cb_readNotReady(MoQTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);
                    _delegate->cb_unsubscribe(conn_id, sub_id);
                    remove_subscribeTrack(conn_it->second, *delegate);
                }

                for (const auto& [name_space, track] : conn_it->second.recv_sub_id) {
                    TrackHash th(track.first, track.second);
                    if (auto pdt = getPubTrackDelegate(conn_it->second, th)) {
                        pdt->lock()->setSendStatus(MoQTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                        pdt->lock()->cb_sendNotReady(MoQTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                    }
                }

                conn_it->second.recv_sub_id.clear();
                conn_it->second.tracks_by_sub_id.clear();

                // TODO(tievens): Clean up publish tracks

#ifndef LIBQUICR_WITHOUT_INFLUXDB
                _mexport.del_conn_ctx_info(conn_id);
#endif
                _connections.erase(conn_it);

                break;
            }
            case TransportStatus::Shutdown:
                _status = Status::NOT_READY;
                break;
        }
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

        //std::lock_guard<std::mutex> _(_state_mutex);

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

        for (int i=0; i < 60; i++) { // don't loop forever, especially on bad stream
            // bidir is Control stream, data streams are unidirectional
            if (is_bidir) {
                if (not conn_ctx.ctrl_msg_type_received) {
                    auto msg_type = stream_buf->decode_uintV();

                    if (msg_type) {
                        conn_ctx.ctrl_msg_type_received = static_cast<MoQMessageType>(*msg_type);
                    } else {
                        break;
                    }
                }

                if (conn_ctx.ctrl_msg_type_received) {
                    if (process_recv_ctrl_message(conn_ctx, stream_buf)) {
                        conn_ctx.ctrl_msg_type_received = std::nullopt;
                        break;
                    }
                }
            }

            // Data stream, unidirectional
            else {
                if (process_recv_stream_data_message(conn_ctx, stream_buf)) {
                    break;
                }
            }


            if (!stream_buf->size()) { // done
                break;
            }
        }
    }

    void MoQInstance::on_recv_dgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id)
    {
        MoqObjectStream object_datagram_out;
        for (int i=0; i < 70; i++) {
            auto data = _transport->dequeue(conn_id, data_ctx_id);
            if (data && !data->empty()) {
                StreamBuffer<uint8_t> buffer;
                buffer.push(*data);

                auto msg_type = buffer.decode_uintV();
                if (!msg_type || static_cast<MoQMessageType>(*msg_type) != MoQMessageType::OBJECT_DATAGRAM) {
                    _logger->warning << "Received datagram that is not message type OBJECT_DATAGRAM, dropping" << std::flush;
                    continue;
                }

                MoqObjectDatagram msg;
                if (buffer >> msg) {

                    //std::lock_guard<std::mutex> _(_state_mutex);

                    auto& conn_ctx = _connections[conn_id];
                    auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                    if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                        _logger->warning << "Received datagram to unknown subscribe track"
                                         << " subscribe_id: " << msg.subscribe_id << ", ignored" << std::flush;

                        // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                        continue;
                    }


                    _logger->debug << "Received object datagram conn_id: " << conn_id
                                   << " data_ctx_id: " << (data_ctx_id ? *data_ctx_id : 0)
                                   << " subscriber_id: " << msg.subscribe_id
                                   << " track_alias: " << msg.track_alias
                                   << " group_id: " << msg.group_id
                                   << " object_id: " << msg.object_id
                                   << " data size: " << msg.payload.size()
                                   << std::flush;

                    sub_it->second->cb_objectReceived(msg.group_id, msg.object_id, msg.priority,
                                                      std::move(msg.payload),
                                                      MoQTrackDelegate::TrackMode::DATAGRAM);

                } else {
                    _logger->warning << "Failed to decode datagram conn_id: " << conn_id
                                     << " data_ctx_id: " << (data_ctx_id ? *data_ctx_id : 0)
                                     << std::flush;
                }

            }
        }

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
