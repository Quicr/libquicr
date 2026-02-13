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

    Bytes& operator<<(Bytes& buffer, const std::optional<Extensions>& extensions);
    Bytes& operator<<(Bytes& buffer, const Extensions& extensions);
    BytesSpan operator>>(BytesSpan buffer, Extensions& extensions);

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
     * Possible ways of communicating subgroup ID in stream headers.
     */
    enum class SubgroupIdType : std::uint8_t
    {
        // The subgroup ID should be set to zero, and not serialized on the wire.
        kIsZero = 0b00,
        // The subgroup ID should be set from the first object in the group, and not serialized on the wire.
        kSetFromFirstObject = 0b01,
        // The subgroup ID is explicitly set and serialized on the wire.
        kExplicit = 0b10,
        // Reserved for future use.
        kReserved = 0b11
    };

    /**
     * Describes the properties of a stream header type.
     */
    struct StreamHeaderProperties
    {
        // If true, all objects in this subgroup will have an extension header length serialized (it may be 0).
        // If false, no objects in this subgroup will have extensions, and extension length is not serialized.
        const bool extensions;
        // The way in which the subgroup ID is serialized & handled.
        const SubgroupIdType subgroup_id_mode;
        // Indicates that this subgroup contains the largest Object in the Group.
        const bool end_of_group;
        // If true, the priority field is omitted and the subgroup inherits the publisher priority.
        // If false, the priority field is present and the subgroup has its own priority.
        const bool default_priority;

        static constexpr std::uint8_t kExtensionsBit = 0x01;
        static constexpr std::uint8_t kSubgroupIdBit = 0x06;
        static constexpr std::uint8_t kEndOfGroupBit = 0x08;
        static constexpr std::uint8_t kDefaultPriorityBit = 0x20;

        explicit constexpr StreamHeaderProperties(const std::uint64_t type)
          : extensions(type & kExtensionsBit)
          , subgroup_id_mode(static_cast<SubgroupIdType>((type & kSubgroupIdBit) >> 1))
          , end_of_group(type & kEndOfGroupBit)
          , default_priority(type & kDefaultPriorityBit)
        {
            if (!IsValid(type)) {
                throw ProtocolViolationException("Invalid stream header type");
            }
        }

        constexpr StreamHeaderProperties(const bool extensions,
                                         const SubgroupIdType subgroup_id_mode,
                                         const bool end_of_group,
                                         const bool default_priority)
          : extensions(extensions)
          , subgroup_id_mode(subgroup_id_mode)
          , end_of_group(end_of_group)
          , default_priority(default_priority)
        {
            if (subgroup_id_mode == SubgroupIdType::kReserved) {
                throw ProtocolViolationException("Subgroup ID mode cannot be kReserved");
            }
        }

        constexpr std::uint64_t GetType() const
        {
            std::uint64_t type = 0b00010000;
            if (extensions) {
                type |= kExtensionsBit;
            }
            type |= static_cast<std::uint64_t>(subgroup_id_mode) << 1;
            if (end_of_group) {
                type |= kEndOfGroupBit;
            }
            if (default_priority) {
                type |= kDefaultPriorityBit;
            }
            return type;
        }

        static constexpr bool IsValid(const std::uint64_t type) noexcept
        {
            if ((type & 0b11010000) != 0b00010000) {
                return false;
            }
            if ((type & 0x06) == 0x06) {
                return false;
            }
            return true;
        }
    };

    /**
     * Describes the properties of a datagram header type.
     */
    struct DatagramHeaderProperties
    {
        // True if this object has extensions.
        const bool extensions;
        // True if this object is end of the group.
        const bool end_of_group;
        // True if this object ID is 0 and non-serialized.
        const bool zero_object_id;
        // True if the object priority is the publisher priority and non-serialized.
        const bool default_priority;
        // True if this is an object datagram status, false for payload.
        const bool status;

        // Bitfield masks.
        static constexpr std::uint8_t kExtensionsBit = 0x01;
        static constexpr std::uint8_t kEndOfGroupBit = 0x02;
        static constexpr std::uint8_t kZeroObjectIdBit = 0x04;
        static constexpr std::uint8_t kDefaultPriorityBit = 0x08;
        static constexpr std::uint8_t kStatusBit = 0x20;

        /**
         * Parse the properties of a datagram header from its type.
         */
        constexpr explicit DatagramHeaderProperties(const std::uint8_t type)
          : extensions(type & kExtensionsBit)
          , end_of_group(type & kEndOfGroupBit)
          , zero_object_id(type & kZeroObjectIdBit)
          , default_priority(type & kDefaultPriorityBit)
          , status(type & kStatusBit)
        {
            if (!IsValid(type)) {
                throw ProtocolViolationException("Invalid Datagram type");
            }
        }

        /// Build the properties of a datagram header.
        constexpr DatagramHeaderProperties(const bool extensions,
                                           const bool end_of_group,
                                           const bool zero_object_id,
                                           const bool default_priority,
                                           const bool status) noexcept
          : extensions(extensions)
          , end_of_group(end_of_group)
          , zero_object_id(zero_object_id)
          , default_priority(default_priority)
          , status(status)
        {
        }

        /**
         * Get the type of this datagram header based on its properties.
         */
        constexpr std::uint8_t GetType() const noexcept
        {
            std::uint8_t type = 0;
            if (extensions) {
                type |= kExtensionsBit;
            }
            if (end_of_group) {
                type |= kEndOfGroupBit;
            }
            if (zero_object_id) {
                type |= kZeroObjectIdBit;
            }
            if (default_priority) {
                type |= kDefaultPriorityBit;
            }
            if (status) {
                type |= kStatusBit;
            }
            return type;
        }

        /**
         * Checks if the given datagram type is a valid value.
         * @param type The type of the datagram message.
         * @return False if a protocol violation.
         */
        static constexpr bool IsValid(const std::uint8_t type) noexcept
        {
            if (type & 0xD0) {
                return false;
            }
            if (type & kEndOfGroupBit && type & kStatusBit) {
                return false;
            }
            return true;
        }
    };

    /**
     * The possible message types arriving over stream transport.
     */
    enum class StreamMessageType
    {
        kFetchHeader,
        kSubgroupHeader
    };

    [[maybe_unused]] [[nodiscard]] static StreamMessageType GetStreamMessageType(const std::uint64_t type)
    {
        if (type == static_cast<std::uint64_t>(FetchHeaderType::kFetchHeader)) {
            return StreamMessageType::kFetchHeader;
        }
        if (StreamHeaderProperties::IsValid(type)) {
            return StreamMessageType::kSubgroupHeader;
        }
        throw ProtocolViolationException("Invalid stream header type");
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
        std::optional<ObjectPriority> priority;
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
        std::uint8_t GetType() const { return GetProperties().GetType(); }

        /**
         * Determine the properties of this datagram header based on its fields.
         * @return The DatagramHeaderProperties corresponding to this datagram object.
         */
        DatagramHeaderProperties GetProperties() const
        {
            return DatagramHeaderProperties(extensions.has_value() || immutable_extensions.has_value(),
                                            end_of_group,
                                            object_id == 0,
                                            !priority.has_value(),
                                            false);
        }

      private:
        std::optional<DatagramHeaderProperties> properties;
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
        std::optional<ObjectPriority> priority;
        std::optional<Extensions> extensions;
        std::optional<Extensions> immutable_extensions;
        ObjectStatus status;

        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, ObjectDatagramStatus& msg);

        std::uint64_t GetType() const
        {
            const auto properties = DatagramHeaderProperties(extensions.has_value() || immutable_extensions.has_value(),
                                                             false,
                                                             object_id == 0,
                                                             !priority.has_value(),
                                                             true);
            return properties.GetType();
        }

      private:
        std::optional<DatagramHeaderProperties> properties;
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
        std::optional<StreamHeaderProperties> properties;
        messages::TrackAlias track_alias;
        messages::GroupId group_id;
        std::optional<SubGroupId> subgroup_id;
        std::optional<ObjectPriority> priority;
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
        std::optional<StreamHeaderProperties> properties{};
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
