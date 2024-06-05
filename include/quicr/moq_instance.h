/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/metrics_exporter.h>
#include <quicr/quicr_common.h>
#include <transport/transport.h>

#include <quicr/moq_instance_delegate.h>
#include <quicr/moq_track_delegate.h>

namespace quicr {
    using namespace qtransport;



    class MoQInstanceConfig
    {
        std::string instance_id; /// Instance ID for the client or server, should be unique
        TransportConfig transport_config;
    };

    class MoQInstanceClientConfig : MoQInstanceConfig
    {
        std::string server_host_ip;     /// Relay hostname or IP to connect to
        uint16_t server__port;          /// Relay port to connect to
        TransportProtocol server_proto; /// Protocol to use when connecting to relay
    };

    class MoQInstanceServerConfig : MoQInstanceConfig
    {
        std::string server_bind_ip;     /// IP address to bind to, can be 0.0.0.0
        uint16_t server_port;           /// Listening port for server
        TransportProtocol server_proto; /// Protocol to use
    };

    /**
     * @brief MOQ Instance
     * @Details MoQ Instance is the handler for either a client or server. It can run
     *   in only one mode, client or server.
     */
    class MoQInstance
    {
      public:
        enum class Status : uint8_t
        {
            READY = 0,

            ERROR_NOT_IN_CLIENT_MODE,
            ERROR_NOT_IN_SERVER_MODE,

            CLIENT_INVALID_PARAMS,
            CLIENT_NOT_CONNECTED,
            CLIENT_CONNECTING,
        };

        /**
         * @brief Client mode Constructor to create the MOQ instance
         *
         * @param cfg     MoQ Instance Client Configuration
         * @param logger  MoQ Log pointer to parent logger
         */
        MoQInstance(const MoQInstanceClientConfig& cfg, const cantina::LoggerPointer& logger);

        /**
         * @brief Server mode Constructor to create the MOQ instance
         *
         * @param cfg     MoQ Instance Server Configuration
         * @param logger  MoQ Log pointer to parent logger
         */
        MoQInstance(const MoQInstanceServerConfig& cfg, const cantina::LoggerPointer& logger);

        ~MoQInstance();

        // -------------------------------------------------------------------------------------------------
        // Public API MoQ Intance API methods
        // -------------------------------------------------------------------------------------------------
        /**
         * @brief Subscribe to a track
         *
         * @param track_delegate    Track delegate to use for track related functions and callbacks
         *
         * @returns `track_alias` if no error and nullopt on error
         */
        std::optional<uint64_t> subscribeTrack(TransportConnId conn_id,
                                               std::shared_ptr<MoQTrackDelegate> track_delegate);

        /**
         * @brief Publish to a track
         *
         * @param track_delegate    Track delegate to use for track related functions
         *                          and callbacks
         *
         * @returns `track_alias` if no error and nullopt on error
         */
        std::optional<uint64_t> publishTrack(TransportConnId conn_id,
                                             std::shared_ptr<MoQTrackDelegate> track_delegate);

        /**
         * @brief Make client connection
         *
         * @details Makes a client connection session if instance is in client mode
         *
         * @return Status indicating state or error. If succesful, status will be
         *    CLIENT_CONNECTING.
         */
        Status client_connect();

        /**
         * @brief Get the instance status
         *
         * @return Status indicating the state/status of the instance
         */
        Status status();

    private:
        /**
         * @brief Create a transport session/connection
         *
         * @param peer_config           Peer/relay configuration parameters
         *
         */
        void createSession(const TransportRemote& peer_config);

      private:
        std::mutex _mutex;
        const bool _client_mode;
        std::atomic<bool> _stop{ false };
        const MoQInstanceServerConfig& _server_config;
        const MoQInstanceClientConfig& _client_config;


        // Log handler to use
        cantina::LoggerPointer logger;

#ifndef LIBQUICR_WITHOUT_INFLUXDB
        std::shared_ptr<MetricsExporter> _mexport;
#endif
    };

    /**
     * @brief MOQ Instance Transport Delegate
     * @details MOQ implements the transport delegate, which is shared by all sessions
     */
    class MoQInstanceTransportDelegate : public ITransport::TransportDelegate
    {
      public:
        MoQInstanceTransportDelegate(const MoQInstance& moq_instance, const cantina::LoggerPointer& logger) :
           _moq_instance(moq_instance)
           , _logger(std::make_shared<cantina::Logger>("MOQ_ITD", logger))
        {}

        // -------------------------------------------------------------------------------------------------
        // Transprot Delegate/callback functions
        // -------------------------------------------------------------------------------------------------

        void on_new_data_context(const TransportConnId& conn_id, const DataContextId& data_ctx_id) override {}
        void on_connection_status(const TransportConnId& conn_id, const TransportStatus status) override;
        void on_new_connection(const TransportConnId& conn_id, const TransportRemote& remote) override;
        void on_recv_stream(const TransportConnId& conn_id,
                            uint64_t stream_id,
                            std::optional<DataContextId> data_ctx_id,
                            const bool is_bidir = false) override;
        void on_recv_dgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id) override;

      private:
        cantina::LoggerPointer _logger;
        const MoQInstance& _moq_instance;
    };

} // namespace quicr
