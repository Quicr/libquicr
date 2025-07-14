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
        kNoEnd_NoExt = 0x00,
        kNoEnd_Ext = 0x01,
        kEnd_NoExt = 0x02,
        kEnd_Ext = 0x03
    };

    enum class DatagramStatusType : uint8_t
    {
        kNoExt = 0x04,
        kExt = 0x05
    };

    enum class StreamHeaderType : uint8_t
    {
        kSubgroupId0_NoExt_NoEnd = 0x10,
        kSubgroupId0_Ext_NoEnd = 0x11,
        kSubgroupIdFirst_NoExt_NoEnd = 0x12,
        kSubgroupIdFirst_Ext_NoEnd = 0x13,
        kSubgroupIdPresent_NoExt_NoEnd = 0x14,
        kSubgroupIdPresent_Ext_NoEnd = 0x15,
        kSubgroupId0_NoExt_End = 0x18,
        kSubgroupId0_Ext_End = 0x19,
        kSubgroupIdFirst_NoExt_End = 0x1A,
        kSubgroupIdFirst_Ext_End = 0x1B,
        kSubgroupIdPresent_NoExt_End = 0x1C,
        kSubgroupIdPresent_Ext_End = 0x1D
    };

    enum class DataMessageType : uint8_t
    {
        kNoEnd_NoExt = static_cast<uint8_t>(DatagramHeaderType::kEnd_NoExt),
        kNoEnd_Ext = static_cast<uint8_t>(DatagramHeaderType::kEnd_Ext),
        kEnd_NoExt = static_cast<uint8_t>(DatagramHeaderType::kNoEnd_NoExt),
        kEnd_Ext = static_cast<uint8_t>(DatagramHeaderType::kNoEnd_Ext),
        kNoExt = static_cast<uint8_t>(DatagramStatusType::kNoExt),
        kExt = static_cast<uint8_t>(DatagramStatusType::kExt),
        kFetchHeader = 0x05,
        kSubgroupId0_NoExt_NoExt = static_cast<uint8_t>(StreamHeaderType::kSubgroupId0_NoExt_NoEnd),
        kSubgroupId0_Ext_NoExt = static_cast<uint8_t>(StreamHeaderType::kSubgroupId0_Ext_NoEnd),
        kSubgroupIdFirst_NoExt_NoExt = static_cast<uint8_t>(StreamHeaderType::kSubgroupIdFirst_NoExt_NoEnd),
        kSubgroupIdFirst_Ext_NoExt = static_cast<uint8_t>(StreamHeaderType::kSubgroupIdFirst_Ext_NoEnd),
        kSubgroupIdPresent_NoExt_NoExt = static_cast<uint8_t>(StreamHeaderType::kSubgroupIdPresent_NoExt_NoEnd),
        kSubgroupIdPresent_Ext_NoExt = static_cast<uint8_t>(StreamHeaderType::kSubgroupIdPresent_Ext_NoEnd),
        kSubgroupId0_NoExt_End = static_cast<uint8_t>(StreamHeaderType::kSubgroupId0_NoExt_End),
        kSubgroupId0_Ext_End = static_cast<uint8_t>(StreamHeaderType::kSubgroupId0_Ext_End),
        kSubgroupIdFirst_NoExt_End = static_cast<uint8_t>(StreamHeaderType::kSubgroupIdFirst_NoExt_End),
        kSubgroupIdFirst_Ext_End = static_cast<uint8_t>(StreamHeaderType::kSubgroupIdFirst_Ext_End),
        kSubgroupIdPresent_NoExt_End = static_cast<uint8_t>(StreamHeaderType::kSubgroupIdPresent_NoExt_End),
        kSubgroupIdPresent_Ext_End = static_cast<uint8_t>(StreamHeaderType::kSubgroupIdPresent_Ext_End)
    };

    /**
     * True if the subgroup of this stream header is 0.
     * @param type Type to check.
     * @return True if subgroup should be 0, false otherwise.
     */
    constexpr bool TypeIsSubgroupId0(const StreamHeaderType type)
    {
        switch (type) {
            case StreamHeaderType::kSubgroupId0_NoExt_NoEnd:
            case StreamHeaderType::kSubgroupId0_Ext_NoEnd:
            case StreamHeaderType::kSubgroupId0_NoExt_End:
            case StreamHeaderType::kSubgroupId0_Ext_End:
                return true;
            default:
                return false;
        }
    }

    constexpr bool TypeIsSubgroupIdFirst(const StreamHeaderType type)
    {
        switch (type) {
            case StreamHeaderType::kSubgroupIdFirst_NoExt_NoEnd:
            case StreamHeaderType::kSubgroupIdFirst_Ext_NoEnd:
            case StreamHeaderType::kSubgroupIdFirst_NoExt_End:
            case StreamHeaderType::kSubgroupIdFirst_Ext_End:
                return true;
            default:
                return false;
        }
    }

    constexpr bool TypeHasSubgroupId(const StreamHeaderType type)
    {
        switch (type) {
            case StreamHeaderType::kSubgroupIdPresent_NoExt_NoEnd:
            case StreamHeaderType::kSubgroupIdPresent_Ext_NoEnd:
            case StreamHeaderType::kSubgroupIdPresent_NoExt_End:
            case StreamHeaderType::kSubgroupIdPresent_Ext_End:
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
        return (value >= static_cast<uint8_t>(DataMessageType::kSubgroupId0_NoExt_NoExt) &&
                value <= static_cast<uint8_t>(DataMessageType::kSubgroupIdPresent_Ext_End));
    }

    static bool TypeIsDatagramHeaderType(const DataMessageType type)
    {
        const auto value = static_cast<uint8_t>(type);
        return (value >= static_cast<uint8_t>(DataMessageType::kNoEnd_NoExt) &&
                value <= static_cast<uint8_t>(DataMessageType::kEnd_Ext));
    }

    static bool TypeIsDatagramStatusType(const DataMessageType type)
    {
        const auto value = static_cast<uint8_t>(type);
        return (value >= static_cast<uint8_t>(DataMessageType::kNoExt) &&
                value <= static_cast<uint8_t>(DataMessageType::kExt));
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
            return extensions.has_value() ? DatagramStatusType::kExt : DatagramStatusType::kNoExt;
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
