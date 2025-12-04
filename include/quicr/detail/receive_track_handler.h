// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/detail/base_track_handler.h"
#include "quicr/detail/messages.h"
#include "quicr/metrics.h"

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
         * @brief Notification callback to provide sampled metrics
         *
         * @details Callback will be triggered on Config::metrics_sample_ms to provide the sampled data based
         *      on the sample period.  After this callback, the period/sample based metrics will reset and start over
         *      for the new period.
         *
         * @param metrics           Copy of the subscribed metrics for the sample period
         */
        virtual void MetricsSampled([[maybe_unused]] const SubscribeTrackMetrics& metrics) {}

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
                                    std::shared_ptr<const std::vector<uint8_t>> data);

        /**
         * @brief Notification of received datagram data
         *
         * @details Event notification to provide the caller the raw data received as a datagram
         *
         * @param data        Shared pointer to the data received
         */
        virtual void DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data);

      protected:
        virtual void ObjectReceived(const messages::TrackAlias& track_alias,
                                    const ObjectHeaders& object_headers,
                                    BytesSpan data) = 0;

      protected:
        SubscribeTrackMetrics track_metrics_;

        StreamBuffer<uint8_t> stream_buffer_;

        std::optional<uint64_t> next_object_id_;
        uint64_t current_group_id_{ 0 };
        uint64_t current_subgroup_id_{ 0 };
        std::optional<uint64_t> pending_new_group_request_id_;

        messages::SubscriberPriority priority_ = 0;
        messages::GroupOrder group_order_;
        messages::FilterType filter_type_;
        uint64_t current_stream_id_{ 0 };
        std::optional<messages::Location> latest_location_;
        std::optional<uint64_t> track_alias_;
        std::optional<uint64_t> received_track_alias_; ///< Received track alias from publisher client or relay
        std::chrono::milliseconds delivery_timeout_{ 0 };

        bool publisher_initiated_{ false };
        bool support_new_group_request_{ false };

        friend class Transport;
        friend class Client;
        friend class Server;
    };
}
