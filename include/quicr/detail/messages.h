// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"
#include "quicr/detail/ctrl_messages.h"
#include "quicr/object.h"
#include "quicr/track_name.h"
#include "stream_buffer.h"

#include <map>
#include <source_location>
#include <string>
#include <vector>

namespace quicr::messages {
    using SubGroupId = quicr::messages::GroupId;
    using ObjectPriority = uint8_t;
    using Extensions = std::map<uint64_t, std::vector<Bytes>>;

    Bytes& operator<<(Bytes& buffer, const std::optional<Extensions>& extensions);
    Bytes& operator<<(Bytes& buffer, const Extensions& extensions);

    /**
     * Serialize extensions to MoQ encoding.
     * @param buffer Buffer to serialize to.
     * @param extensions Optionally, mutable extensions.
     * @param immutable_extensions Optionally, immutable extensions.
     */
    void SerializeExtensions(Bytes& buffer,
                             const std::optional<Extensions>& extensions,
                             const std::optional<Extensions>& immutable_extensions);

    /**
     * Parse extensions from MoQ encoded format.
     * @tparam StreamBufferType
     * @param buffer Buffer to parse from.
     * @param extension_headers_length Extract length of all extension headers.
     * @param extensions Parsed mutable extensions, if any.
     * @param immutable_extensions Parsed immutable extensions, if any.
     * @param extension_bytes_remaining Total bytes of extension headers left to parse.
     * @param current_header Current header key being parsed.
     * @return True if parsing completed successfully, false if not.
     */
    template<class StreamBufferType>
    bool ParseExtensions(StreamBufferType& buffer,
                         std::optional<std::size_t>& extension_headers_length,
                         std::optional<Extensions>& extensions,
                         std::optional<Extensions>& immutable_extensions,
                         std::size_t& extension_bytes_remaining,
                         std::optional<std::uint64_t>& current_header);

    struct ProtocolViolationException : std::runtime_error
    {
        const std::string reason;
        ProtocolViolationException(const std::string& reason,
                                   const std::source_location location = std::source_location::current())
          : std::runtime_error("Protocol violation: " + reason + " (line " + std::to_string(location.line()) +
                               ", file " + location.file_name() + ")")
          , reason(reason)
        {
        }
    };

    /**
     * Extension Header Types
     */
    enum class ExtensionHeaderType : uint64_t
    {
        kImmutable = 0xb,
        kPriorGroupIdGap = 0x3c,
        kPriorObjectIdGap = 0x3e,
    };

    /**
     * Datagram bitfield masks.
     */
    namespace DatagramBits {
        constexpr uint8_t kExtensions = 0x01;
        constexpr uint8_t kEndOfGroup = 0x02;
        constexpr uint8_t kNoObjectId = 0x04;
        constexpr uint8_t kStatus = 0x20;
        constexpr uint8_t kDataValidMask = 0xF8;
        constexpr uint8_t kStatusBaseMask = 0xFE;
    }

    /**
     * Possible datagram object header types.
     */
    enum class DatagramHeaderType : uint8_t
    {
        kNotEndOfGroupNoExtensionsObjectId = 0x00,
        kNotEndOfGroupWithExtensionsObjectId = 0x01,
        kEndOfGroupNoExtensionsObjectId = 0x02,
        kEndOfGroupWithExtensionsObjectId = 0x03,
        kNotEndOfGroupNoExtensionsNoObjectId = 0x04,
        kNotEndOfGroupWithExtensionsNoObjectId = 0x05,
        kEndOfGroupNoExtensionsNoObjectId = 0x06,
        kEndOfGroupWithExtensionsNoObjectId = 0x07
    };

    /**
     * Possible datagram status types.
     */
    enum class DatagramStatusType : uint8_t
    {
        kNoExtensions = 0x20,
        kWithExtensions = 0x21
    };

    /**
     * Possible fetch header types.
     */
    enum class FetchHeaderType : uint8_t
    {
        kFetchHeader = 0x05
    };

    /**
     * Stream header bitfields.
     */
    namespace StreamBits {
        constexpr uint8_t kExtensions = 0x01;
        constexpr uint8_t kSubgroupIdModeMask = 0x06;
        constexpr uint8_t kSubgroupIdModeShift = 1;
        constexpr uint8_t kEndOfGroup = 0x08;
        constexpr uint8_t kStreamMarker = 0x10;
        constexpr uint8_t kReservedMode = 0x06;    // Mode value 3 (0b11) is reserved
        constexpr uint8_t kInvalidBitsMask = 0xE0; // Bits 5,6,7 must be 0
    }

    /**
     * Possible stream subgroup header types.
     */
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

    /**
     * Possible ways of communicating subgroup ID in stream headers.
     */
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
        // The way in which the subgroup ID is serialized & handled.
        const SubgroupIdType subgroup_id_type;
        // True if the last object in this subgroup is the end of the group.
        const bool end_of_group;
        // If true, all objects in this subgroup will have an extension header length serialized (it may be 0).
        // If false, no objects in this subgroup will have extensions, and extension length is not serialized.
        const bool may_contain_extensions;

        constexpr StreamHeaderProperties(const SubgroupIdType subgroup_id_type,
                                         const bool end_of_group,
                                         const bool may_contain_extensions)
          : subgroup_id_type(subgroup_id_type)
          , end_of_group(end_of_group)
          , may_contain_extensions(may_contain_extensions)
        {
        }

        constexpr StreamHeaderProperties(const StreamHeaderType type)
          : subgroup_id_type(GetSubgroupIdType(type))
          , end_of_group(EndOfGroup(type))
          , may_contain_extensions(MayContainExtensions(type))
        {
        }

        /**
         * Get the type of this stream header based on its properties.
         * @return The StreamHeaderType corresponding to these properties.
         */
        StreamHeaderType GetType() const
        {
            uint8_t type = StreamBits::kStreamMarker;
            if (may_contain_extensions) {
                type |= StreamBits::kExtensions;
            }
            switch (subgroup_id_type) {
                case SubgroupIdType::kIsZero:
                    break;
                case SubgroupIdType::kSetFromFirstObject:
                    type |= 1 << StreamBits::kSubgroupIdModeShift;
                    break;
                case SubgroupIdType::kExplicit:
                    type |= 2 << StreamBits::kSubgroupIdModeShift;
                    break;
            }
            if (end_of_group) {
                type |= StreamBits::kEndOfGroup;
            }
            return static_cast<StreamHeaderType>(type);
        }

      private:
        static constexpr bool MayContainExtensions(const StreamHeaderType type)
        {
            return static_cast<uint8_t>(type) & StreamBits::kExtensions;
        }

        static constexpr bool EndOfGroup(const StreamHeaderType type)
        {
            return static_cast<uint8_t>(type) & StreamBits::kEndOfGroup;
        }

        static constexpr SubgroupIdType GetSubgroupIdType(const StreamHeaderType type)
        {
            constexpr auto kModeMask = StreamBits::kSubgroupIdModeMask >> StreamBits::kSubgroupIdModeShift;
            const auto mode = (static_cast<uint8_t>(type) >> StreamBits::kSubgroupIdModeShift) & kModeMask;
            switch (mode) {
                case 0:
                    return SubgroupIdType::kIsZero;
                case 1:
                    return SubgroupIdType::kSetFromFirstObject;
                case 2:
                    return SubgroupIdType::kExplicit;
                default:
                    throw ProtocolViolationException("Reserved subgroup ID mode");
            }
        }
    };

    /**
     * Describes the properties of a datagram header type.
     */
    struct DatagramHeaderProperties
    {
        // True if this object is end of the group.
        const bool end_of_group;
        // True if this object has extensions.
        const bool has_extensions;
        // True if this object encodes an object ID.
        const bool encodes_object_id;

        constexpr explicit DatagramHeaderProperties(const DatagramHeaderType type)
          : end_of_group(static_cast<uint8_t>(type) & DatagramBits::kEndOfGroup)
          , has_extensions(static_cast<uint8_t>(type) & DatagramBits::kExtensions)
          , encodes_object_id((static_cast<uint8_t>(type) & DatagramBits::kNoObjectId) == 0)
        {
        }

        constexpr DatagramHeaderProperties(const bool end_of_group, const bool has_extensions, const bool has_object_id)
          : end_of_group(end_of_group)
          , has_extensions(has_extensions)
          , encodes_object_id(has_object_id)
        {
        }

        /**
         * Get the type of this datagram header based on its properties.
         * @return The DatagramHeaderType corresponding to these properties.
         */
        constexpr DatagramHeaderType GetType() const
        {
            uint8_t type = 0;
            if (has_extensions)
                type |= DatagramBits::kExtensions;
            if (end_of_group)
                type |= DatagramBits::kEndOfGroup;
            if (!encodes_object_id)
                type |= DatagramBits::kNoObjectId;
            return static_cast<DatagramHeaderType>(type);
        }
    };

    /**
     * Describes the properties of a datagram status type.
     */
    struct DatagramStatusProperties
    {
        // True if this datagram status message has extensions.
        const bool has_extensions;

        explicit constexpr DatagramStatusProperties(const bool has_extensions)
          : has_extensions(has_extensions)
        {
        }

        explicit constexpr DatagramStatusProperties(const DatagramStatusType type)
          : has_extensions(HasExtensions(type))
        {
        }

        /**
         * Get the datagram status type based on its properties.
         * @return The DatagramStatusType corresponding to these properties.
         */
        constexpr DatagramStatusType GetType() const
        {
            return has_extensions ? DatagramStatusType::kWithExtensions : DatagramStatusType::kNoExtensions;
        }

      private:
        static constexpr bool HasExtensions(const DatagramStatusType type)
        {
            return static_cast<uint8_t>(type) & DatagramBits::kExtensions;
        }
    };

    /**
     * The possible message types arriving over datagram transport.
     */
    enum class DatagramMessageType : uint8_t
    {
        kDatagramNotEndOfGroupNoExtensionsObjectId =
          static_cast<uint8_t>(DatagramHeaderType::kNotEndOfGroupNoExtensionsObjectId),
        kDatagramNotEndOfGroupWithExtensionsObjectId =
          static_cast<uint8_t>(DatagramHeaderType::kNotEndOfGroupWithExtensionsObjectId),
        kDatagramEndOfGroupNoExtensionsObjectId =
          static_cast<uint8_t>(DatagramHeaderType::kEndOfGroupNoExtensionsObjectId),
        kDatagramEndOfGroupWithExtensionsObjectId =
          static_cast<uint8_t>(DatagramHeaderType::kEndOfGroupWithExtensionsObjectId),
        kDatagramNotEndOfGroupNoExtensionsNoObjectId =
          static_cast<uint8_t>(DatagramHeaderType::kNotEndOfGroupNoExtensionsNoObjectId),
        kDatagramNotEndOfGroupWithExtensionsNoObjectId =
          static_cast<uint8_t>(DatagramHeaderType::kNotEndOfGroupWithExtensionsNoObjectId),
        kDatagramEndOfGroupNoExtensionsNoObjectId =
          static_cast<uint8_t>(DatagramHeaderType::kEndOfGroupNoExtensionsNoObjectId),
        kDatagramEndOfGroupWithExtensionsNoObjectId =
          static_cast<uint8_t>(DatagramHeaderType::kEndOfGroupWithExtensionsNoObjectId),
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
     * @brief Check if the given datagram message type is a datagram object header.
     * @param type The type to query.
     * @return True if this is a datagram object type, false otherwise.
     */
    [[maybe_unused]]
    static bool TypeIsDatagramHeaderType(const DatagramMessageType type)
    {
        const auto val = static_cast<uint8_t>(type);
        return (val & DatagramBits::kDataValidMask) == 0;
    }

    /**
     * @brief Check if the given datagram message type is a datagram status header.
     * @param type The type to query.
     * @return True if this is a datagram object status, false otherwise.
     */
    [[maybe_unused]]
    static bool TypeIsDatagramStatusType(const DatagramMessageType type)
    {
        const auto val = static_cast<uint8_t>(type);
        return (val & DatagramBits::kStatusBaseMask) == DatagramBits::kStatus;
    }

    /**
     * @brief Check if the given type is a datagram message type.
     * @details This primarily exists to enforce compile time validation of message type handling. The actual message
     * type can be assumed to be a datagram message if it arrives on a datagram transport.
     * @param type The type to query.
     * @return True if this is a datagram message type, false otherwise.*/
    constexpr bool TypeIsDatagram(const DatagramMessageType type)
    {
        return TypeIsDatagramHeaderType(type) || TypeIsDatagramStatusType(type);
    }

    /**
     * Check if the given stream message type is a subgroup header.
     * @param type The type to query.
     * @return True if this is a stream subgroup header type, false otherwise.
     */
    [[maybe_unused]] static bool TypeIsStreamHeaderType(const StreamMessageType type)
    {
        const auto val = static_cast<uint8_t>(type);
        return (val & StreamBits::kInvalidBitsMask) == 0 && (val & StreamBits::kStreamMarker) != 0 &&
               (val & StreamBits::kSubgroupIdModeMask) != StreamBits::kReservedMode;
    }

    struct FetchHeader
    {
        FetchHeaderType type{ FetchHeaderType::kFetchHeader };
        uint64_t request_id;

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
        std::optional<Extensions> immutable_extensions;
        uint64_t payload_len{ 0 };
        ObjectStatus object_status;
        Bytes payload;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, FetchObject& msg);

      private:
        std::optional<std::size_t> extension_headers_length;
        std::size_t extension_bytes_remaining{ 0 };
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
        std::optional<Extensions> immutable_extensions;
        uint64_t payload_len{ 0 };
        ObjectStatus object_status;
        Bytes payload;
        bool end_of_group{ false };

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, ObjectDatagram& msg);

        /**
         * Determine the type of this datagram header based on its properties.
         * @return The DatagramHeaderType corresponding to this datagram header.
         */
        DatagramHeaderType GetType() const { return GetProperties().GetType(); }

        /**
         * Determine the properties of this datagram header based on its fields.
         * @return The DatagramHeaderProperties corresponding to this datagram object.
         */
        DatagramHeaderProperties GetProperties() const
        {
            return DatagramHeaderProperties(
              end_of_group, extensions.has_value() || immutable_extensions.has_value(), object_id > 0);
        }

      private:
        std::optional<DatagramHeaderType> type;
        std::optional<std::size_t> extension_headers_length;
        std::size_t extension_bytes_left{ 0 };
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
        std::optional<Extensions> immutable_extensions;
        ObjectStatus status;

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, ObjectDatagramStatus& msg);

        DatagramStatusType GetType() const
        {
            const auto properties =
              DatagramStatusProperties(extensions.has_value() || immutable_extensions.has_value());
            return properties.GetType();
        }

      private:
        std::optional<DatagramStatusType> type;
        std::optional<std::size_t> extension_headers_length;
        std::size_t extension_bytes_left{ 0 };
        std::optional<uint64_t> current_tag{};
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
        ObjectId object_delta;
        uint64_t payload_len{ 0 };
        ObjectStatus object_status;
        std::optional<Extensions> extensions;
        std::optional<Extensions> immutable_extensions;
        Bytes payload;
        std::optional<StreamHeaderType> stream_type;
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, StreamSubGroupObject& msg);

      private:
        std::optional<std::size_t> extension_headers_length;
        std::size_t extension_bytes_left{ 0 };
        std::optional<uint64_t> current_tag{};
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    bool operator>>(Bytes& buffer, StreamSubGroupObject& msg);
    Bytes& operator<<(Bytes& buffer, const StreamSubGroupObject& msg);

} // end of namespace quicr::_messages
