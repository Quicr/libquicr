/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/moqt_core.h>

namespace quicr {
    using namespace qtransport;

    /**
     * @brief MoQ Client
     *
     * @details MoQ Client is the handler of the MOQT QUIC transport IP connection.
     */
    class MOQTServer : public MOQTCore
    {
      public:
        /**
         * @brief MoQ Server constructor to create the MOQ server mode instance
         *
         * @param cfg           MoQ Server Configuration
         * @param delegate      MoQ Server delegate of callbacks
         * @param logger        MoQ Log pointer to parent logger
         */
        MOQTServer(const MOQTServerConfig& cfg,
                   std::shared_ptr<MOQTServerCallbacks> delegate,
                   const cantina::LoggerPointer& logger)
          : MOQTCore(cfg, delegate, logger)
        {
        }

        ~MOQTServer() = default;

        /**
         * @brief Runs server transport thread to listen for new connections
         *
         * @details Creates a new transport thread to listen for new connections. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be
         *    READY.
         */
        Status run();
    };

} // namespace quicr
