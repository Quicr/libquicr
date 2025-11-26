// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/base_track_handler.h"
#include "quicr/detail/messages.h"

#include <cstdint>
#include <vector>

namespace quicr {

    /**
     * @brief
     *
     * @details
     */
    class ReceiveTrackHandler : public BaseTrackHandler
    {
      public:
        using BaseTrackHandler::BaseTrackHandler;

        virtual ~ReceiveTrackHandler() = default;

        /**
         * @brief Notification of received stream data slice
         *
         * @details Event notification to provide the caller the raw data received on a stream
         *
         * @param is_start    True to indicate if this data is the start of a new stream
         * @param stream_id   Stream ID data was received on
         * @param data        Shared pointer to the data received
         */
        virtual void StreamDataRecv(bool is_start,
                                    uint64_t stream_id,
                                    std::shared_ptr<const std::vector<uint8_t>> data) = 0;

        /**
         * @brief Notification of received datagram data
         *
         * @details Event notification to provide the caller the raw data received as a datagram
         *
         * @param data        Shared pointer to the data received
         */
        virtual void DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data) = 0;
    };
}
