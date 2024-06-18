/*
 *  Copyright (C) 2024
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 */

#include <quicr/moq_instance.h>
#include <quicr/moq_track_delegate.h>

namespace quicr {

    MoQTrackDelegate::SendError MoQTrackDelegate::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] const std::span<const uint8_t>& object)
    {
        return MoQTrackDelegate::SendError::OK;
    }

    MoQTrackDelegate::SendError MoQTrackDelegate::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] const std::span<const uint8_t>& object,
                                                             [[maybe_unused]] uint32_t ttl)
    {
        return MoQTrackDelegate::SendError::OK;
    }

    MoQTrackDelegate::SendError MoQTrackDelegate::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] const std::span<const uint8_t>& object,
                                                             [[maybe_unused]] uint8_t priority)
    {
        return MoQTrackDelegate::SendError::OK;
    }

    MoQTrackDelegate::SendError MoQTrackDelegate::sendObject([[maybe_unused]] const uint64_t  group_id,
                                                             [[maybe_unused]] const uint64_t object_id,
                                                             [[maybe_unused]] const std::span<const uint8_t>& object,
                                                             [[maybe_unused]] uint8_t priority,
                                                             [[maybe_unused]] uint32_t ttl)
    {
        return MoQTrackDelegate::SendError::OK;
    }

    MoQTrackDelegate::ReadError MoQTrackDelegate::readObject([[maybe_unused]] std::vector<const uint8_t>& object)
    {
        return MoQTrackDelegate::ReadError::NO_DATA;
    }


} // namespace quicr
