/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#pragma once

#include <quicr/moq_impl.h>

namespace quicr {
    using namespace qtransport;

    /**
     * @brief MoQ Client
     *
     * @details MoQ Client is the handler of the MOQT QUIC transport IP connection.
     */
    class MoQClient : public MoQImpl
    {
      public:
        /**
         * @brief MoQ Client Constructor to create the client mode instance
         *
         * @param cfg           MoQ Client Configuration
         * @param delegate      MoQ Client delegate of callbacks
         * @param logger        MoQ Log pointer to parent logger
         */
        MoQClient(const MoQClientConfig& cfg,
                  std::shared_ptr<MoQClientDelegate> delegate,
                    const cantina::LoggerPointer& logger)
        : MoQImpl(cfg, delegate, logger) {}

        ~MoQClient() = default;
    };

} // namespace quicr
