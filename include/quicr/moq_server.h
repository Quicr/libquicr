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
     * @Details MoQ Client is the handler of the MOQT QUIC transport IP connection.
     */
    class MoQServer : public MoQImpl
    {
      public:
        /**
         * @brief MoQ Server constructor to create the MOQ server mode instance
         *
         * @param cfg           MoQ Server Configuration
         * @param delegate      MoQ Server delegate of callbacks
         * @param logger        MoQ Log pointer to parent logger
         */
        MoQServer(const MoQServerConfig& cfg,
                  std::shared_ptr<MoQServerDelegate> delegate,
                    const cantina::LoggerPointer& logger)
        : MoQImpl(cfg, delegate, logger) {}

        ~MoQServer() = default;
    };

} // namespace quicr
