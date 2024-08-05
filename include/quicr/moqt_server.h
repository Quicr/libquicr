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
     * @brief MOQT Server
     *
     * @details MOQT Server is the handler of the MOQT QUIC listening socket
     */
    class MOQTServer : public MOQTCore
    {
      public:
        /**
         * @brief MoQ Server constructor to create the MOQ server mode instance
         *
         * @param cfg           MOQT Server Configuration
         * @param callbacks     MOQT Server callbacks
         * @param logger        MOQT Log pointer to parent logger
         */
        MOQTServer(const MOQTServerConfig& cfg,
                   std::shared_ptr<MOQTServerCallbacks> callbacks,
                   const cantina::LoggerPointer& logger)
          : MOQTCore(cfg, callbacks, logger)
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
