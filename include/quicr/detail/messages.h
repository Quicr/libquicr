// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"
#include "quicr/detail/ctrl_messages.h"
#include "quicr/object.h"
#include "quicr/track_name.h"
#include "stream_buffer.h"

#include <map>
#include <string>
#include <vector>

namespace quicr::messages {

    using SubGroupId = quicr::messages::GroupId;
    using ObjectPriority = uint8_t;
    using Extensions = std::map<uint64_t, Bytes>;

    /// @brief Stream Header types.
    enum class StreamHeaderType : uint8_t
    {
        kZeroNoExtensions = 0x08,          // No extensions, Subgroup ID = 0
        kZeroWithExtensions = 0x09,        // With extensions, Subgroup ID = 0
        kFirstObjectNoExtensions = 0x0A,   // No extensions, Subgroup ID = First Object ID
        kFirstObjectWithExtensions = 0x0B, // With extensions, Subgroup ID = First Object ID
        kExplicitNoExtensions = 0x0C,      // No extensions, Explicit Subgroup ID
        kExplicitWithExtensions = 0x0D,    // With extensions, Explicit Subgroup ID
    };

    /**
     * Check if this Stream Header type will serialize extensions (even empty ones).
     * @param type Type to check.
     * @return True if the type will serialize extensions (even empty ones), false otherwise.
     */
    [[maybe_unused]]
    static bool typeWillSerializeExtensions(const StreamHeaderType type)
    {
        switch (type) {
            case StreamHeaderType::kZeroWithExtensions:
                [[fallthrough]];
            case StreamHeaderType::kFirstObjectWithExtensions:
                [[fallthrough]];
            case StreamHeaderType::kExplicitWithExtensions:
                return true;
            default:
                return false;
        }
    }

    /**
     * Check if this Stream Header type will serialize subgroup.
     * @param type Type to check.
     * @return True if a header of this type will have a subgroup field on the wire.
     */
    [[maybe_unused]]
    static bool typeWillSerializeSubgroup(const StreamHeaderType type)
    {
        switch (type) {
            case StreamHeaderType::kExplicitNoExtensions:
                [[fallthrough]];
            case StreamHeaderType::kExplicitWithExtensions:
                return true;
            default:
                return false;
        }
    }

    enum class DataMessageType : uint8_t
    {
        kObjectDatagram = 0x01,
        kObjectDatagramStatus = 0x02,
        kFetchHeader = 0x5,

        // Subgroup Header Types.
        kStreamHeaderSubGroupZeroNoExtensions = static_cast<std::uint8_t>(StreamHeaderType::kZeroNoExtensions),
        kStreamHeaderSubGroupZeroExtensions = static_cast<std::uint8_t>(StreamHeaderType::kZeroWithExtensions),
        kStreamHeaderSubGroupFirstObjectNoExtensions =
          static_cast<std::uint8_t>(StreamHeaderType::kFirstObjectNoExtensions),
        kStreamHeaderSubGroupFirstObjectExtensions =
          static_cast<std::uint8_t>(StreamHeaderType::kFirstObjectWithExtensions),
        kStreamHeaderSubGroupExplicitNoExtensions = static_cast<std::uint8_t>(StreamHeaderType::kExplicitNoExtensions),
        kStreamHeaderSubGroupExplicitExtensions = static_cast<std::uint8_t>(StreamHeaderType::kExplicitWithExtensions),
    };

    /**
     * @brief Check if the type is a stream header type.
     * @param type The type to check.
     * @return true if the type is a stream header type, false otherwise.
     */
    [[maybe_unused]]
    static bool typeIsStreamHeaderType(const DataMessageType type)
    {
        switch (type) {
            case DataMessageType::kStreamHeaderSubGroupZeroNoExtensions:
                [[fallthrough]];
            case DataMessageType::kStreamHeaderSubGroupZeroExtensions:
                [[fallthrough]];
            case DataMessageType::kStreamHeaderSubGroupFirstObjectNoExtensions:
                [[fallthrough]];
            case DataMessageType::kStreamHeaderSubGroupFirstObjectExtensions:
                [[fallthrough]];
            case DataMessageType::kStreamHeaderSubGroupExplicitNoExtensions:
                [[fallthrough]];
            case DataMessageType::kStreamHeaderSubGroupExplicitExtensions:
                return true;
            default:
                return false;
        }
    }

    /**
     * Check if this data type needs its type field inside the message.
     * @param type The type to check.
     * @return True if the type needs its type field inside the message, false otherwise.
     */
    [[maybe_unused]]
    static bool typeNeedsTypeField(const DataMessageType type)
    {
        return typeIsStreamHeaderType(type);
    }

    struct FetchHeader
    {
        uint64_t subscribe_id;

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, FetchHeader& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    BytesSpan operator>>(BytesSpan buffer, FetchHeader& msg);
    Bytes& operator<<(Bytes& buffer, const FetchHeader& msg);

    struct FetchObject
    {
        messages::GroupId group_id;
        SubGroupId subgroup_id;
        ObjectId object_id;
        ObjectPriority publisher_priority;
        std::optional<Extensions> extensions;
        uint64_t payload_len{ 0 };
        ObjectStatus object_status;
        Bytes payload;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, FetchObject& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    bool operator>>(Bytes& buffer, FetchObject& msg);
    Bytes& operator<<(Bytes& buffer, const FetchObject& msg);

    //
    // Data Streams
    //

    struct ObjectDatagram
    {
        messages::TrackAlias track_alias;
        messages::GroupId group_id;
        ObjectId object_id;
        ObjectPriority priority;
        std::optional<Extensions> extensions;
        uint64_t payload_len{ 0 };
        ObjectStatus object_status;
        Bytes payload;

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, ObjectDatagram& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    Bytes& operator<<(Bytes& buffer, const ObjectDatagram& msg);

    struct ObjectDatagramStatus
    {
        messages::TrackAlias track_alias;
        messages::GroupId group_id;
        ObjectId object_id;
        ObjectPriority priority;
        ObjectStatus status;

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, ObjectDatagramStatus& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    Bytes& operator<<(Bytes& buffer, const ObjectDatagramStatus& msg);

    // SubGroups
    struct StreamHeaderSubGroup
    {
        StreamHeaderType type;
        messages::TrackAlias track_alias;
        messages::GroupId group_id;
        std::optional<SubGroupId> subgroup_id;
        ObjectPriority priority;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, StreamHeaderSubGroup& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    bool operator>>(Bytes& buffer, StreamHeaderSubGroup& msg);
    Bytes& operator<<(Bytes& buffer, const StreamHeaderSubGroup& msg);

    struct StreamSubGroupObject
    {
        ObjectId object_id;
        uint64_t payload_len{ 0 };
        ObjectStatus object_status;
        bool serialize_extensions;
        std::optional<Extensions> extensions;
        Bytes payload;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, StreamSubGroupObject& msg);

      private:
        uint64_t num_extensions{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    bool operator>>(Bytes& buffer, StreamSubGroupObject& msg);
    Bytes& operator<<(Bytes& buffer, const StreamSubGroupObject& msg);

} // end of namespace quicr::_messages
