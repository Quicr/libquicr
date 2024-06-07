/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <quicr/moq_instance.h>

namespace quicr {

    MoQInstance::MoQInstance(const MoQInstanceClientConfig& cfg,
                            std::shared_ptr<MoQInstanceDelegate> delegate,
                            const cantina::LoggerPointer& logger) :
        _client_mode(true)
        , _server_config({})
        , _client_config(cfg)
        , _delegate(std::move(delegate))
        , _transport_delegate({*this, _logger})
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
      , _transport_delegate({*this, _logger})
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
                                                       _transport_delegate, _logger);

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
                                                       _transport_delegate, _logger);

#ifndef LIBQUICR_WITHOUT_INFLUXDB
        _transport->start(_mexport.metrics_conn_samples, _mexport.metrics_data_samples);
#else
        _transport->start(nullptr, nullptr);
#endif

        _connections.push_back(_transport->start(_mexport.metrics_conn_samples, _mexport.metrics_data_samples));

        LOGGER_INFO(_logger, "Connecting session conn_id: " << _connections.front() << "...");

        while (!_stop && _transport->status() == TransportStatus::Connecting) {
            LOGGER_DEBUG(_logger, "Connecting... " << int(_stop.load()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (_stop || !_transport) {
            LOGGER_INFO(_logger, "Cancelling connecting session " << _connections.front());
            return Status::CLIENT_FAILED_TO_CONNECT;
        }

        if (_transport->status() != TransportStatus::Ready) {
            _logger->error << "Failed to connect status: " << static_cast<int>(_transport->status()) << std::flush;

            return Status::CLIENT_FAILED_TO_CONNECT;
        }

        return Status::READY;
    }


    void MoQInstanceTransportDelegate::on_connection_status(const TransportConnId& conn_id, const TransportStatus status)
    {
        _logger->info << "Connection status conn_id: " << conn_id
                      << " status: " << static_cast<int>(status)
                      << std::flush;

        _moq_instance.status();
    }

    void MoQInstanceTransportDelegate::on_new_connection(const TransportConnId& conn_id, const TransportRemote& remote)
    {
        _logger->info << "New connection conn_id: " << conn_id
                      << " remote ip: " << remote.host_or_ip
                      << " port: " << remote.port
                      << std::flush;

    }

    void MoQInstanceTransportDelegate::on_recv_stream(const TransportConnId& conn_id,
                        uint64_t stream_id,
                        std::optional<DataContextId> data_ctx_id,
                        const bool is_bidir)
    {
        _logger->info << "stream data conn_id: " << conn_id
                      << " stream_id: " << stream_id
                      << " data_ctx_id: " << (data_ctx_id ? *data_ctx_id : 0)
                      << " is_bidir: " << is_bidir
                      << std::flush;
    }

    void MoQInstanceTransportDelegate::on_recv_dgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id)
    {
        _logger->info << "datagram data conn_id: " << conn_id
                      << " data_ctx_id: " << (data_ctx_id ? *data_ctx_id : 0)
                      << std::flush;

    }


} // namespace quicr
