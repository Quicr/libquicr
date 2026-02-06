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
     * Possible fetch header types.
     */
    enum class FetchHeaderType : uint8_t
    {
        kFetchHeader = 0x05
    };

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
            switch (subgroup_id_type) {
                case SubgroupIdType::kIsZero: {
                    if (end_of_group) {
                        return may_contain_extensions ? StreamHeaderType::kSubgroup0EndOfGroupWithExtensions
                                                      : StreamHeaderType::kSubgroup0EndOfGroupNoExtensions;
                    }
                    return may_contain_extensions ? StreamHeaderType::kSubgroup0NotEndOfGroupWithExtensions
                                                  : StreamHeaderType::kSubgroup0NotEndOfGroupNoExtensions;
                }
                case SubgroupIdType::kSetFromFirstObject: {
                    if (end_of_group) {
                        return may_contain_extensions ? StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions
                                                      : StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions;
                    }
                    return may_contain_extensions ? StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupWithExtensions
                                                  : StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupNoExtensions;
                }
                case SubgroupIdType::kExplicit: {
                    if (end_of_group) {
                        return may_contain_extensions ? StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions
                                                      : StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions;
                    }
                    return may_contain_extensions ? StreamHeaderType::kSubgroupExplicitNotEndOfGroupWithExtensions
                                                  : StreamHeaderType::kSubgroupExplicitNotEndOfGroupNoExtensions;
                }
            }
            throw ProtocolViolationException("Unknown subgroup header type");
        }

      private:
        static constexpr bool MayContainExtensions(const StreamHeaderType type)
        {
            switch (type) {
                case StreamHeaderType::kSubgroup0NotEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroup0EndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupExplicitNotEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions:
                    return false;
                case StreamHeaderType::kSubgroup0NotEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroup0EndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupExplicitNotEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions:
                    return true;
            }
            throw ProtocolViolationException("Unknown subgroup header type");
        }

        static constexpr bool EndOfGroup(const StreamHeaderType type)
        {
            switch (type) {
                case StreamHeaderType::kSubgroup0EndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroup0EndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions:
                    return true;
                case StreamHeaderType::kSubgroup0NotEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroup0NotEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupExplicitNotEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupExplicitNotEndOfGroupWithExtensions:
                    return false;
            }
            throw ProtocolViolationException("Unknown subgroup header type");
        }

        static constexpr SubgroupIdType GetSubgroupIdType(const StreamHeaderType type)
        {
            switch (type) {
                case StreamHeaderType::kSubgroup0NotEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroup0NotEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroup0EndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroup0EndOfGroupWithExtensions:
                    return SubgroupIdType::kIsZero;
                case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions:
                    return SubgroupIdType::kSetFromFirstObject;
                case StreamHeaderType::kSubgroupExplicitNotEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupExplicitNotEndOfGroupWithExtensions:
                case StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions:
                case StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions:
                    return SubgroupIdType::kExplicit;
            }
            throw ProtocolViolationException("Unknown subgroup header type");
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
     * Check if the given stream message type is a subgroup header.
     * @param type The type to query.
     * @return True if this is a stream subgroup header type, false otherwise.
     */
    [[maybe_unused]] static bool TypeIsStreamHeaderType(const StreamMessageType type)
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
        throw ProtocolViolationException("Unknown stream header type");
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

    /**
     * Describes how a FetchObject is/should be serialized.
     */
    struct FetchSerializationProperties
    {
        // End of range values.
        enum class EndOfRange
        {
            kEndOfNonExistentRange,
            kEndOfUnknownRange
        };

        // Subgroup encoding variants.
        enum class FetchSubgroupIdType : std::uint8_t
        {
            kSubgroupZero = 0x00,
            kSubgroupPrior = 0x01,
            kSubgroupNext = 0x02,
            kSubgroupExplicit = 0x03
        };

        // Properties.
        const std::optional<EndOfRange> end_of_range;
        const std::optional<FetchSubgroupIdType> subgroup_id_mode;
        const bool object_id_present;
        const bool group_id_present;
        const bool priority_present;
        const bool extensions_present;
        const bool datagram;

        // Bitmask values.
        constexpr static std::uint8_t kSubgroupBitmask = 0x03;
        constexpr static std::uint8_t kObjectIdBitmask = 0x04;
        constexpr static std::uint8_t kGroupIdBitmask = 0x08;
        constexpr static std::uint8_t kPriorityBitmask = 0x10;
        constexpr static std::uint8_t kExtensionsBitmask = 0x20;
        constexpr static std::uint8_t kDatagramBitmask = 0x40;
        constexpr static std::uint64_t kEndOfNonExistentRange = 0x8C;
        constexpr static std::uint64_t kEndOfUnknownRange = 0x10C;

        /**
         * Parse a FetchObject's serialization value into its properties.
         * @param wire The wire serialization flags.
         * @throws ProtocolViolationException if the wire value is invalid.
         */
        explicit FetchSerializationProperties(std::uint64_t wire);

        /**
         * Create serialization for an end of range FETCH object.
         * @param end_of_range The end of range type
         */
        explicit FetchSerializationProperties(EndOfRange end_of_range) noexcept;

        /**
         * Describe how a FetchObject should be serialized (stream).
         * @param subgroup_id_mode Subgroup encoding.
         * @param object_id_present True to encode the field, false for +1 previous.
         * @param group_id_present True to encode the field, false for same as last.
         * @param priority_present True to encode the field, false for same as last.
         * @param extensions_present True if extensions present.
         * @param datagram True if datagram.
         */
        FetchSerializationProperties(FetchSubgroupIdType subgroup_id_mode,
                                     bool object_id_present,
                                     bool group_id_present,
                                     bool priority_present,
                                     bool extensions_present) noexcept;

        /**
         * Describe how a FetchObject should be serialized (datagram).
         * @param object_id_present True to encode the field, false for +1 previous.
         * @param group_id_present True to encode the field, false for same as last.
         * @param priority_present True to encode the field, false for same as last.
         * @param extensions_present True if extensions present.
         */
        FetchSerializationProperties(bool object_id_present,
                                     bool group_id_present,
                                     bool priority_present,
                                     bool extensions_present) noexcept;

        /**
         * Get the serialization flag / type value for this set of properties.
         * @return The value.
         */
        [[nodiscard]] std::uint64_t GetType() const noexcept;

      private:
        static std::optional<EndOfRange> ParseEndOfRange(std::uint64_t value) noexcept;
    };

    /**
     * State wrapper to create/restore FetchObject serialization.
     */
    struct FetchObjectSerializationState
    {
        // Working state.
        std::optional<GroupId> prior_group_id;
        std::optional<ObjectId> prior_object_id;
        std::optional<SubGroupId> prior_subgroup_id;
        std::optional<ObjectPriority> prior_priority;

        /// Figure out how to serialize a FetchObject based on its content.
        [[nodiscard]] FetchSerializationProperties MakeProperties(const ObjectHeaders& object_headers,
                                                                  ObjectPriority priority) const noexcept;

        void Update(const ObjectHeaders& object_headers) noexcept;
    };

    struct FetchObject
    {
        std::optional<FetchSerializationProperties> properties;
        std::optional<GroupId> group_id;
        std::optional<SubGroupId> subgroup_id;
        std::optional<ObjectId> object_id;
        std::optional<ObjectPriority> publisher_priority;
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
