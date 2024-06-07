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
        , _logger(std::make_shared<cantina::Logger>("MOQ_IC", logger))
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
      , _logger(std::make_shared<cantina::Logger>("MOQ_IS", logger))
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
        _connections.try_emplace(conn_id, ConnectionContext{ .conn_id =  conn_id });

        return _status;
    }

    void MoQInstance::send_ctrl_msg(const ConnectionContext& conn_ctx, std::vector<uint8_t>&& data)
    {
        _transport->enqueue(conn_ctx.conn_id,
                            conn_ctx.ctrl_data_ctx_id,
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

        client_setup.num_versions = 1;
        client_setup.supported_versions = { MOQT_VERSION };
        client_setup.role_parameter.param_type = static_cast<uint64_t>(ParameterType::Role);
        client_setup.role_parameter.param_length = 0x1; // length of 1 for role value
        client_setup.role_parameter.param_value = { 0x03 };

        buffer << client_setup;

        auto &conn_ctx = _connections.begin()->second;

        send_ctrl_msg(conn_ctx, buffer.front(buffer.size()));
    }


    MoQInstance::Status MoQInstance::status()
    {
        return _status;
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
            send_client_setup();
        }

        _status = Status::READY;
    }

    void MoQInstance::on_new_connection(const TransportConnId& conn_id, const TransportRemote& remote)
    {
        _logger->info << "New connection conn_id: " << conn_id
                      << " remote ip: " << remote.host_or_ip
                      << " port: " << remote.port
                      << std::flush;
    }

    void MoQInstance::on_recv_stream(const TransportConnId& conn_id,
                        uint64_t stream_id,
                        [[maybe_unused]] std::optional<DataContextId> data_ctx_id,
                        [[maybe_unused]] const bool is_bidir)
    {
        auto stream_buf = _transport->getStreamBuffer(conn_id, stream_id);
        auto& conn_ctx = _connections[conn_id];

        if (stream_buf == nullptr) {
            return;
        }

        while (true) {
            if (not conn_ctx.msg_type_received.has_value()) {
                auto msg_type = stream_buf->decode_uintV();

                if (msg_type) {
                    conn_ctx.msg_type_received = static_cast<MoQMessageType>(*msg_type);
                } else {
                    break;
                }
            }

            if (conn_ctx.msg_type_received) {

                // type is known, process data based on the message type
                _logger->debug << "processing incoming message type: "
                               << static_cast<int>(*conn_ctx.msg_type_received) << std::flush;
            }
        }
    }

    void MoQInstance::on_recv_dgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id)
    {
        _logger->info << "datagram data conn_id: " << conn_id
                      << " data_ctx_id: " << (data_ctx_id ? *data_ctx_id : 0)
                      << std::flush;

    }


} // namespace quicr
