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

    enum class DatagramHeaderType : uint8_t
    {
        kNotEndOfGroupNoExtensions = 0x00,
        kNotEndOfGroupWithExtensions = 0x01,
        kEndOfGroupNoExtensions = 0x02,
        kEndOfGroupWithExtensions = 0x03
    };

    enum class DatagramStatusType : uint8_t
    {
        kNoExtensions = 0x04,
        kWithExtensions = 0x05
    };

    enum class FetchHeaderType : uint8_t
    {
        kFetchHeader = 0x05
    };

    // TODO: Make a type / factory to better refer to these combinations over the enum.

    enum class StreamHeaderType : uint8_t
    {
        kSubgroup0NotEndOfGroupNoExtensions = 0x10,
        kSubgroup0NotEndOfGroupWithExtensions = 0x11,
        kSubgroupFirstObjectNotEndOfGroupNoExtensions = 0x12,
        kSubgroupFirstObjectNotEndOfGroupWithExtensions = 0x13,
        kSubgroupExplicitNotEndOfGroupNoExtensions = 0x14,
        kSubgroupExplicitNotEndOfGroupWithExtensions = 0x15,
        kSubgroup0EndOfGroupNoExtensions = 0x18,
        kSubgroup0EndOfGroupWithExtensions = 0x19,
        kSubgroupFirstObjectEndOfGroupNoExtensions = 0x1A,
        kSubgroupFirstObjectEndOfGroupWithExtensions = 0x1B,
        kSubgroupExplicitEndOfGroupNoExtensions = 0x1C,
        kSubgroupExplicitEndOfGroupWithExtensions = 0x1D
    };

    enum class DataMessageType : uint8_t
    {
        kDatagramNotEndOfGroupNoExtensions = static_cast<uint8_t>(DatagramHeaderType::kEndOfGroupNoExtensions),
        kDatagramNotEndOfGroupWithExtensions = static_cast<uint8_t>(DatagramHeaderType::kEndOfGroupWithExtensions),
        kDatagramEndOfGroupNoExtensions = static_cast<uint8_t>(DatagramHeaderType::kNotEndOfGroupNoExtensions),
        kDatagramEndOfGroupWithExtensions = static_cast<uint8_t>(DatagramHeaderType::kNotEndOfGroupWithExtensions),
        kDatagramStatusNoExtensions = static_cast<uint8_t>(DatagramStatusType::kNoExtensions),
        kDatagramStatusWithExtensions = static_cast<uint8_t>(DatagramStatusType::kWithExtensions),
        kFetchHeader = static_cast<uint8_t>(FetchHeaderType::kFetchHeader),
        kSubgroup0NotEndOfGroupNoExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroup0NotEndOfGroupNoExtensions),
        kSubgroup0NotEndOfGroupWithExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroup0NotEndOfGroupWithExtensions),
        kSubgroupFirstObjectNotEndOfGroupNoExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupNoExtensions),
        kSubgroupFirstObjectNotEndOfGroupWithExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupWithExtensions),
        kSubgroupExplicitNotEndOfGroupNoExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroupExplicitNotEndOfGroupNoExtensions),
        kSubgroupExplicitNotEndOfGroupWithExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroupExplicitNotEndOfGroupWithExtensions),
        kSubgroup0EndOfGroupNoExtensions = static_cast<uint8_t>(StreamHeaderType::kSubgroup0EndOfGroupNoExtensions),
        kSubgroup0EndOfGroupWithExtensions = static_cast<uint8_t>(StreamHeaderType::kSubgroup0EndOfGroupWithExtensions),
        kSubgroupFirstObjectEndOfGroupNoExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions),
        kSubgroupFirstObjectEndOfGroupWithExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions),
        kSubgroupExplicitEndOfGroupNoExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions),
        kSubgroupExplicitEndOfGroupWithExtensions =
          static_cast<uint8_t>(StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions)
    };

    /**
     * True if the subgroup of this stream header is 0.
     * @param type Type to check.
     * @return True if subgroup should be 0, false otherwise.
     */
    constexpr bool TypeIsSubgroupId0(const StreamHeaderType type)
    {
        switch (type) {
            case StreamHeaderType::kSubgroup0NotEndOfGroupNoExtensions:
            case StreamHeaderType::kSubgroup0NotEndOfGroupWithExtensions:
            case StreamHeaderType::kSubgroup0EndOfGroupNoExtensions:
            case StreamHeaderType::kSubgroup0EndOfGroupWithExtensions:
                return true;
            default:
                return false;
        }
    }

    constexpr bool TypeIsSubgroupIdFirst(const StreamHeaderType type)
    {
        switch (type) {
            case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupNoExtensions:
            case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupWithExtensions:
            case StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions:
            case StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions:
                return true;
            default:
                return false;
        }
    }

    constexpr bool TypeHasSubgroupId(const StreamHeaderType type)
    {
        switch (type) {
            case StreamHeaderType::kSubgroupExplicitNotEndOfGroupNoExtensions:
            case StreamHeaderType::kSubgroupExplicitNotEndOfGroupWithExtensions:
            case StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions:
            case StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions:
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
    constexpr bool TypeWillSerializeSubgroup(StreamHeaderType type)
    {
        return (static_cast<uint8_t>(type) & 0x04) != 0;
    }

    /**
     * True if the Stream's subgroup is the first object ID.
     * @param type Type to check.
     * @return True if the Stream's subgroup is the first object ID, false otherwise.
     */
    constexpr bool TypeSubgroupIsFirstObjectId(StreamHeaderType type)
    {
        return (static_cast<uint8_t>(type) & 0x06) == 0x02;
    }

    constexpr bool TypeWillSerializeExtensions(const uint8_t type)
    {
        return (type & 0x01) != 0;
    }

    /**
     * Check if this type will serialize extensions.
     * @param type Type to check.
     * @return True if the type will serialize extensions, false otherwise.
     */
    constexpr bool TypeWillSerializeExtensions(DatagramHeaderType type)
    {
        return TypeWillSerializeExtensions(static_cast<uint8_t>(type));
    }

    /**
     * Check if this type will serialize extensions.
     * @param type Type to check.
     * @return True if the type will serialize extensions, false otherwise.
     */
    constexpr bool TypeWillSerializeExtensions(DatagramStatusType type)
    {
        return TypeWillSerializeExtensions(static_cast<uint8_t>(type));
    }

    /**
     * Check if this type will serialize extensions.
     * @param type Type to check.
     * @return True if the type will serialize extensions, false otherwise.
     */
    constexpr bool TypeWillSerializeExtensions(StreamHeaderType type)
    {
        return TypeWillSerializeExtensions(static_cast<uint8_t>(type));
    }

    /**
     * Check is this object is the last object in the group.
     * @param type Type to check.
     * @return True if this is the last object in the group.
     */
    constexpr bool TypeContainsEndOfGroup(DatagramHeaderType type)
    {
        return (static_cast<uint8_t>(type) & 0x02) != 0;
    }

    /**
     * Check is this object is the last object in the group.
     * @param type Type to check.
     * @return True if this is the last object in the group.
     */
    constexpr bool TypeContainsEndOfGroup(StreamHeaderType type)
    {
        return (static_cast<uint8_t>(type) & 0x08) != 0;
    }

    /**
     * @brief Check if the type is a stream header type.
     * @param type The type to check.
     * @return true if the type is a stream header type, false otherwise.
     */
    static bool TypeIsStreamHeaderType(const DataMessageType type)
    {
        const auto value = static_cast<uint8_t>(type);
        return (value >= static_cast<uint8_t>(DataMessageType::kSubgroup0NotEndOfGroupNoExtensions) &&
                value <= static_cast<uint8_t>(DataMessageType::kSubgroupExplicitEndOfGroupWithExtensions));
    }

    static bool TypeIsDatagramHeaderType(const DataMessageType type)
    {
        const auto value = static_cast<uint8_t>(type);
        return (value >= static_cast<uint8_t>(DataMessageType::kDatagramNotEndOfGroupNoExtensions) &&
                value <= static_cast<uint8_t>(DataMessageType::kDatagramStatusWithExtensions));
    }

    static bool TypeIsDatagramStatusType(const DataMessageType type)
    {
        const auto value = static_cast<uint8_t>(type);
        return (value >= static_cast<uint8_t>(DataMessageType::kDatagramStatusNoExtensions) &&
                value <= static_cast<uint8_t>(DataMessageType::kDatagramStatusWithExtensions));
    }

    [[maybe_unused]] static bool TypeIsDatagram(const DataMessageType type)
    {
        return TypeIsDatagramHeaderType(type) || TypeIsDatagramStatusType(type);
    }

    /**
     * Check if this data type needs its type field inside the message.
     * @param type The type to check.
     * @return True if the type needs its type field inside the message, false otherwise.
     */
    constexpr bool TypeNeedsTypeField(const DataMessageType type)
    {
        return TypeIsStreamHeaderType(type);
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
        bool end_of_group{ false };

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
        TrackAlias track_alias;
        GroupId group_id;
        ObjectId object_id;
        ObjectPriority priority;
        std::optional<Extensions> extensions;
        ObjectStatus status;

        DatagramStatusType get_type() const
        {
            return extensions.has_value() ? DatagramStatusType::kWithExtensions : DatagramStatusType::kNoExtensions;
        }

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
