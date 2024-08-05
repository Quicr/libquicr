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
    class MOQTClient : public MOQTCore
    {
      public:
        /**
         * @brief MoQ Client Constructor to create the client mode instance
         *
         * @param cfg           MoQ Client Configuration
         * @param delegate      MoQ Client delegate of callbacks
         * @param logger        MoQ Log pointer to parent logger
         */
        MOQTClient(const MOQTClientConfig& cfg,
                   std::shared_ptr<MOQTClientDelegate> delegate,
                   const cantina::LoggerPointer& logger)
          : MOQTCore(cfg, delegate, logger)
        {
        }

        ~MOQTClient() = default;

        /**
         * @brief Make client connection and run
         *
         * @details Makes a client connection session if instance is in client mode and runs as client
         *     Session will be created using a thread to run the QUIC connection
         * @return Status indicating state or error. If successful, status will be
         *    CLIENT_CONNECTING.
         */
        Status run();
    };

} // namespace quicr
