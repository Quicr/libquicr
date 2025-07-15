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

    enum class SubgroupIdType
    {
        // The subgroup ID should be set to zero, and not serialized on the wire.
        kIsZero,
        // The subgroup ID should be set from the first object in the group, and not serialized on the wire.
        kSetFromFirstObject,
        // The subgroup ID is explicitly set and serialized on the wire.
        kExplicit
    };

    /**
     * Describes the properties of a stream header type.
     */
    struct StreamHeaderProperties
    {
        // The type of the stream header.
        const StreamHeaderType type;
        // The way in which the subgroup ID is serialized & handled.
        const SubgroupIdType subgroup_id_type;
        // True if the last object in this subgroup is the end of the group.
        const bool end_of_group;
        // If true, all objects in this subgroup will have an extension header length serialized (it may be 0).
        // If false, no objects in this subgroup will have extensions, and extension length is not serialized.
        const bool may_contain_extensions;
    };

    /**
     * Get the properties of a stream header type.
     * @param type The type to query.
     * @return A StreamHeaderProperties object describing the properties of the type.
     */
    constexpr StreamHeaderProperties GetStreamHeaderProperties(const StreamHeaderType type)
    {
        switch (type) {
            case StreamHeaderType::kSubgroup0NotEndOfGroupNoExtensions:
                return { type, SubgroupIdType::kIsZero, false, false };
            case StreamHeaderType::kSubgroup0NotEndOfGroupWithExtensions:
                return { type, SubgroupIdType::kIsZero, false, true };
            case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupNoExtensions:
                return { type, SubgroupIdType::kSetFromFirstObject, false, false };
            case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupWithExtensions:
                return { type, SubgroupIdType::kSetFromFirstObject, false, true };
            case StreamHeaderType::kSubgroupExplicitNotEndOfGroupNoExtensions:
                return { type, SubgroupIdType::kExplicit, false, false };
            case StreamHeaderType::kSubgroupExplicitNotEndOfGroupWithExtensions:
                return { type, SubgroupIdType::kExplicit, false, true };
            case StreamHeaderType::kSubgroup0EndOfGroupNoExtensions:
                return { type, SubgroupIdType::kIsZero, true, false };
            case StreamHeaderType::kSubgroup0EndOfGroupWithExtensions:
                return { type, SubgroupIdType::kIsZero, true, true };
            case StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions:
                return { type, SubgroupIdType::kSetFromFirstObject, true, false };
            case StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions:
                return { type, SubgroupIdType::kSetFromFirstObject, true, true };
            case StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions:
                return { type, SubgroupIdType::kExplicit, true, false };
            case StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions:
                return { type, SubgroupIdType::kExplicit, true, true };
        }
        assert(false);
        return { type, SubgroupIdType::kIsZero, false, false };
    }

    /**
     * Describes the properties of a datagram header type.
     */
    struct DatagramHeaderProperties
    {
        // The type of the datagram header.
        const DatagramHeaderType type;
        // True if this object is end of the group.
        const bool end_of_group;
        // True if this object has extensions.
        const bool has_extensions;
    };

    /**
     * Get the properties of a datagram header type.
     * @param type The type to query.
     * @return A DatagramHeaderProperties object describing the properties of the type.
     */
    constexpr DatagramHeaderProperties GetDatagramHeaderProperties(const DatagramHeaderType type)
    {
        switch (type) {
            case DatagramHeaderType::kNotEndOfGroupNoExtensions:
                return { type, false, false };
            case DatagramHeaderType::kNotEndOfGroupWithExtensions:
                return { type, false, true };
            case DatagramHeaderType::kEndOfGroupNoExtensions:
                return { type, true, false };
            case DatagramHeaderType::kEndOfGroupWithExtensions:
                return { type, true, true };
        }
        assert(false);
        return { type, false, false };
    }

    /**
     * Describes the properties of a datagram status type.
     */
    struct DatagramStatusProperties
    {
        // The type of the datagram status.
        const DatagramStatusType type;
        // True if this datagram status message has extensions.
        const bool has_extensions;
    };

    /**
     * Get the properties of a datagram status type.
     * @param type The type to query.
     * @return A DatagramStatusProperties object describing the properties of the type.
     */
    constexpr DatagramStatusProperties GetDatagramStatusProperties(const DatagramStatusType type)
    {
        switch (type) {
            case DatagramStatusType::kNoExtensions:
                return { type, false };
            case DatagramStatusType::kWithExtensions:
                return { type, true };
        }
        assert(false);
        return { type, false };
    }

    /**
     * The possible message types arriving over datagram transport.
     */
    enum class DatagramMessageType : uint8_t
    {
        kDatagramNotEndOfGroupNoExtensions = static_cast<uint8_t>(DatagramHeaderType::kEndOfGroupNoExtensions),
        kDatagramNotEndOfGroupWithExtensions = static_cast<uint8_t>(DatagramHeaderType::kEndOfGroupWithExtensions),
        kDatagramEndOfGroupNoExtensions = static_cast<uint8_t>(DatagramHeaderType::kNotEndOfGroupNoExtensions),
        kDatagramEndOfGroupWithExtensions = static_cast<uint8_t>(DatagramHeaderType::kNotEndOfGroupWithExtensions),
        kDatagramStatusNoExtensions = static_cast<uint8_t>(DatagramStatusType::kNoExtensions),
        kDatagramStatusWithExtensions = static_cast<uint8_t>(DatagramStatusType::kWithExtensions),
    };

    /**
     * The possible message types arriving over stream transport.
     */
    enum class StreamMessageType : uint8_t
    {
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
     * @brief Check if the given type is a datagram message type.
     * @details This primarily exists to enforce compile time validation of message type handling. The actual message
     * type can be assumed to be a datagram message if it arrives on a datagram transport.
     * @param type The type to query.
     * @return True if this is a datagram message type, false otherwise.
     */
    constexpr bool TypeIsDatagram(const DatagramMessageType type)
    {
        switch (type) {
            case DatagramMessageType::kDatagramNotEndOfGroupNoExtensions:
            case DatagramMessageType::kDatagramNotEndOfGroupWithExtensions:
            case DatagramMessageType::kDatagramEndOfGroupNoExtensions:
            case DatagramMessageType::kDatagramEndOfGroupWithExtensions:
            case DatagramMessageType::kDatagramStatusNoExtensions:
            case DatagramMessageType::kDatagramStatusWithExtensions:
                return true;
        }
        assert(false);
        return false;
    }

    /**
     * @brief Check if the given datagram message type is a datagram object header.
     * @param type The type to query.
     * @return True if this is a datagram object type, false otherwise.
     */
    [[maybe_unused]]
    static bool TypeIsDatagramHeaderType(const DatagramMessageType type)
    {
        switch (type) {
            case DatagramMessageType::kDatagramNotEndOfGroupNoExtensions:
            case DatagramMessageType::kDatagramNotEndOfGroupWithExtensions:
            case DatagramMessageType::kDatagramEndOfGroupNoExtensions:
            case DatagramMessageType::kDatagramEndOfGroupWithExtensions:
                return true;
            case DatagramMessageType::kDatagramStatusNoExtensions:
            case DatagramMessageType::kDatagramStatusWithExtensions:
                return false;
        }
        assert(false);
        return false;
    }

    /**
     * @brief Check if the given datagram message type is a datagram status header.
     * @param type The type to query.
     * @return True if this is a datagram object status, false otherwise.
     */
    [[maybe_unused]]
    static bool TypeIsDatagramStatusType(const DatagramMessageType type)
    {
        switch (type) {
            case DatagramMessageType::kDatagramStatusNoExtensions:
            case DatagramMessageType::kDatagramStatusWithExtensions:
                return true;
            case DatagramMessageType::kDatagramNotEndOfGroupNoExtensions:
            case DatagramMessageType::kDatagramNotEndOfGroupWithExtensions:
            case DatagramMessageType::kDatagramEndOfGroupNoExtensions:
            case DatagramMessageType::kDatagramEndOfGroupWithExtensions:
                return false;
        }
        assert(false);
        return false;
    }

    /**
     * Check if the given stream message type is a subgroup header.
     * @param type The type to query.
     * @return True if this is a stream subgroup header type, false otherwise.
     */
    static bool TypeIsStreamHeaderType(const StreamMessageType type)
    {
        switch (type) {
            case StreamMessageType::kSubgroup0NotEndOfGroupNoExtensions:
            case StreamMessageType::kSubgroup0NotEndOfGroupWithExtensions:
            case StreamMessageType::kSubgroupFirstObjectNotEndOfGroupNoExtensions:
            case StreamMessageType::kSubgroupFirstObjectNotEndOfGroupWithExtensions:
            case StreamMessageType::kSubgroupExplicitNotEndOfGroupNoExtensions:
            case StreamMessageType::kSubgroupExplicitNotEndOfGroupWithExtensions:
            case StreamMessageType::kSubgroup0EndOfGroupNoExtensions:
            case StreamMessageType::kSubgroup0EndOfGroupWithExtensions:
            case StreamMessageType::kSubgroupFirstObjectEndOfGroupNoExtensions:
            case StreamMessageType::kSubgroupFirstObjectEndOfGroupWithExtensions:
            case StreamMessageType::kSubgroupExplicitEndOfGroupNoExtensions:
            case StreamMessageType::kSubgroupExplicitEndOfGroupWithExtensions:
                return true;
            case StreamMessageType::kFetchHeader:
                return false;
        }
        assert(false);
        return false;
    }

    /**
     * Check if this data type needs its type field inside the message.
     * @param type The type to check.
     * @return True if the type needs its type field inside the message, false otherwise.
     */
    constexpr bool TypeNeedsTypeField(const StreamMessageType type)
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
