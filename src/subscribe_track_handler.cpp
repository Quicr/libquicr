// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/handlers/subscribe_track_handler.h"

#include "quicr/containers/stream_buffer.h"
#include "quicr/log.h"
#include "quicr/messages/messages.h"
#include "quicr/messages/parameters.h"
#include "quicr/session.h"

namespace quicr {

    void SubscribeTrackHandler::SupportNewGroupRequest(bool is_supported) noexcept
    {
        support_new_group_request_ = is_supported;
    }

    void SubscribeTrackHandler::ObjectReceived(
      [[maybe_unused]] const ObjectHeaders& object_headers,
      [[maybe_unused]] BytesSpan data,
      [[maybe_unused]] std::optional<messages::StreamHeaderProperties> stream_mode)
    {
    }

    void SubscribeTrackHandler::DgramDataRecv(std::shared_ptr<const std::vector<uint8_t>> data)
    {
        dgram_buffer_.Clear();
        dgram_buffer_.Push(*data);

        // Payload or status?
        const auto properties = messages::DatagramHeaderProperties(data->front());
        if (properties.status) {
            messages::ObjectDatagramStatus status_msg;
            if (dgram_buffer_ >> status_msg) {
                subscribe_track_metrics_.objects_received++;

                ObjectStatusReceived(
                  status_msg.group_id,
                  status_msg.object_id,
                  status_msg.priority.value_or(priority_), // TODO: This should be publisher priority.
                  status_msg.status,
                  std::move(status_msg.extensions),
                  std::move(status_msg.immutable_extensions));
            }
            return;
        }

        // Data.
        messages::ObjectDatagram msg;
        if (dgram_buffer_ >> msg) {

            subscribe_track_metrics_.objects_received++;
            subscribe_track_metrics_.bytes_received += msg.payload.size();

            ObjectReceived(
              {
                msg.group_id,
                msg.object_id,
                0, // datagrams don't have subgroups
                msg.payload.size(),
                ObjectStatus::kAvailable,
                msg.priority,
                std::nullopt,
                TrackMode::kDatagram,
                std::move(msg.extensions),
                std::move(msg.immutable_extensions),
              },
              std::move(msg.payload));
        }
    }

    void SubscribeTrackHandler::Pause() noexcept
    {
        auto session = GetSession().lock();
        const auto request_stream = GetRequestStream();
        if (!session || status_ == Status::kPaused || status_ == Status::kNotConnected || request_stream == nullptr) {
            return;
        }

        status_ = Status::kPaused;
        session->SendRequestUpdate(request_stream, TrackHash(GetFullTrackName()), std::nullopt, GetPriority(), false);
    }

    void SubscribeTrackHandler::Resume() noexcept
    {
        auto session = GetSession().lock();
        const auto request_stream = GetRequestStream();
        if (!session || request_stream == nullptr) {
            return;
        }

        if (status_ != Status::kPaused) {
            return;
        }

        status_ = Status::kOk;
        session->SendRequestUpdate(request_stream, TrackHash(GetFullTrackName()), std::nullopt, GetPriority(), true);
    }

    void SubscribeTrackHandler::RequestNewGroup(uint64_t group_id) noexcept
    {
        auto session = GetSession().lock();
        const auto request_stream = GetRequestStream();
        if (!session || status_ != Status::kOk || !support_new_group_request_ || request_stream == nullptr) {
            return;
        }

        session->SendRequestUpdate(request_stream, TrackHash(GetFullTrackName()), group_id, GetPriority(), true);
    }

    void SubscribeTrackHandler::RequestOkReceived(const messages::Parameters& params)
    {
        // SUBSCRIBE request OK itself is SUBSCRIBE_OK, so any RequestOk will be REQUEST_UPDATE_OK.
        messages::ValidateParameters(params,
                                     {
                                       messages::ParameterType::kExpires,
                                       messages::ParameterType::kLargestObject,
                                     });
        // TODO: EXPIRES
        // TODO: LARGEST_OBJECT
    }

    Reply<messages::Parameters, ErrorCode> SubscribeTrackHandler::RequestUpdateReceived(
      const messages::Parameters& params)
    {
        if (IsPublisherInitiated()) {
            // Publish can rev keys but nothing else.
            messages::ValidateParameters(params,
                                         {
                                           messages::ParameterType::kAuthorizationToken,
                                         });
            // TODO: AUTHORIZATION_TOKEN
            return messages::Parameters{};
        }

        throw messages::ProtocolViolationException("Unexpected REQUEST_UPDATE");
    }

} // namespace quicr
