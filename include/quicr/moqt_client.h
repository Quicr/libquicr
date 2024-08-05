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
     * @brief MOQT Client
     *
     * @details MOQT Client is the handler of the MOQT QUIC transport IP connection.
     */
    class MOQTClient : public MOQTCore
    {
      public:
        /**
         * @brief MoQ Client Constructor to create the client mode instance
         *
         * @param cfg           MOQT Client Configuration
         * @param callbacks     MOQT Client callbacks
         * @param logger        MOQT Log pointer to parent logger
         */
        MOQTClient(const MOQTClientConfig& cfg,
                   std::shared_ptr<MOQTClientCallbacks> callbacks,
                   const cantina::LoggerPointer& logger)
          : MOQTCore(cfg, callbacks, logger)
        {
        }

        ~MOQTClient() = default;

        /**
         * @brief Runs client connection in transport thread
         *
         * @details Makes a client connection session and runs in a newly created thread. All control and track
         *   callbacks will be run based on events.
         *
         * @return Status indicating state or error. If successful, status will be
         *    CLIENT_CONNECTING.
         */
        Status run();

    };

} // namespace quicr
