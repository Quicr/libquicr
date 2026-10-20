// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/containers/stream_buffer.h"
#include "quicr/messages/ctrl_message_types.h"
#include "quicr/messages/object.h"
#include "quicr/track_name.h"

#include <map>
#include <string>
#include <vector>

namespace quicr::messages {
    /**
     * Appends a length-prefixed extension block, or a zero length when absent.
     * @param buffer Destination bytes.
     * @param extensions Optional extensions to serialize.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const std::optional<Extensions>& extensions);

    /**
     * Appends a length-prefixed, sorted, delta-keyed extension block.
     * @param buffer Destination bytes.
     * @param extensions Extensions to serialize.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const Extensions& extensions);

    /**
     * Parses one length-prefixed extension block.
     * @param buffer Input span.
     * @param extensions Destination extensions.
     * @return Unconsumed suffix.
     */
    BytesSpan operator>>(BytesSpan buffer, Extensions& extensions);

    /**
     * Serialize extensions to MoQ encoding.
     * @param buffer Buffer to serialize to.
     * @param extensions Optionally, mutable extensions.
     * @param immutable_extensions Optionally, immutable extensions.
     * @throws ProtocolViolationException if immutable extensions contain kImmutable.
     */
    void SerializeExtensions(Bytes& buffer,
                             const std::optional<Extensions>& extensions,
                             const std::optional<Extensions>& immutable_extensions);

    /**
     * Parse extensions from MoQ encoded format.
     * @tparam StreamBufferType Incremental buffer providing UintVar and byte reads.
     * @param buffer Buffer to parse from.
     * @param extension_headers_length Extract length of all extension headers.
     * @param extensions Parsed mutable extensions, if any.
     * @param immutable_extensions Parsed immutable extensions, if any.
     * @param extension_bytes_remaining Total bytes of extension headers left to parse.
     * @param current_header Current header key being parsed (absolute type, after delta decoding).
     * @param prev_extension_type Previous extension type for delta decoding.
     * @return True when the complete block has been consumed; false when more bytes are required.
     * @throws ProtocolViolationException on delta overflow or nested immutable extensions.
     * @note All state arguments are in/out values and must be preserved unchanged
     * between calls except through this function. Successfully consumed bytes are
     * removed from @p buffer even when false is returned.
     */
    template<class StreamBufferType>
    bool ParseExtensions(StreamBufferType& buffer,
                         std::optional<std::size_t>& extension_headers_length,
                         std::optional<Extensions>& extensions,
                         std::optional<Extensions>& immutable_extensions,
                         std::size_t& extension_bytes_remaining,
                         std::optional<std::uint64_t>& current_header,
                         std::uint64_t& prev_extension_type);

    /**
     * Possible datagram object header types.
     */
    enum class DatagramHeaderType : uint8_t
    {
        kNotEndOfGroupNoExtensionsObjectId = 0x00,     ///< Payload, explicit object ID, no extensions.
        kNotEndOfGroupWithExtensionsObjectId = 0x01,   ///< Payload, explicit object ID and extensions.
        kEndOfGroupNoExtensionsObjectId = 0x02,        ///< End-of-group payload with explicit object ID.
        kEndOfGroupWithExtensionsObjectId = 0x03,      ///< End-of-group payload with extensions.
        kNotEndOfGroupNoExtensionsNoObjectId = 0x04,   ///< Payload with implicit zero object ID.
        kNotEndOfGroupWithExtensionsNoObjectId = 0x05, ///< Payload with implicit zero ID and extensions.
        kEndOfGroupNoExtensionsNoObjectId = 0x06,      ///< End-of-group payload with implicit zero ID.
        kEndOfGroupWithExtensionsNoObjectId = 0x07     ///< End-of-group payload with implicit zero ID and extensions.
    };

    /**
     * Possible datagram status types.
     */
    enum class DatagramStatusType : uint8_t
    {
        kNoExtensions = 0x20,  ///< Status datagram without extensions.
        kWithExtensions = 0x21 ///< Status datagram with extensions.
    };

    /**
     * Possible fetch header types.
     */
    enum class FetchHeaderType : uint8_t
    {
        kFetchHeader = 0x05 ///< FETCH stream header.
    };

    /**
     * Possible ways of communicating subgroup ID in stream headers.
     */
    enum class SubgroupIdType : std::uint8_t
    {
        kIsZero = 0b00,             ///< ID is zero and omitted from the wire.
        kSetFromFirstObject = 0b01, ///< ID comes from the first object and is omitted here.
        kExplicit = 0b10,           ///< ID is explicitly serialized.
        kReserved = 0b11            ///< Reserved and rejected.
    };

    /**
     * Describes the properties of a stream header type.
     */
    struct StreamHeaderProperties
    {
        const bool extensions; ///< Whether every subgroup object carries an extension-block length, possibly zero.
        const SubgroupIdType subgroup_id_mode; ///< Subgroup-ID omission/serialization mode.
        const bool end_of_group;               ///< Whether this subgroup contains the largest object in its group.
        const bool default_priority;           ///< Whether priority is omitted and inherited from the publisher.
        const bool first_object; ///< Whether this is the subgroup's original first object rather than a resumed stream.

        static constexpr std::uint8_t kExtensionsBit = 0x01;  ///< Bit indicating extension lengths are present.
        static constexpr std::uint8_t kSubgroupIdMask = 0x06; ///< Two-bit subgroup-ID mode mask.
        static constexpr std::uint8_t kEndOfGroupBit = 0x08;  ///< Bit indicating end of group.

        /// Required subgroup-stream type bit.
        static constexpr std::uint8_t kTypeBit = 0x10;
        static constexpr std::uint8_t kDefaultPriorityBit = 0x20; ///< Bit indicating inherited publisher priority.
        static constexpr std::uint8_t kFirstObjectBit = 0x40;     ///< Bit indicating the original first object.

        /// Mask of all recognized stream-header bits.
        static constexpr std::uint64_t kAllowedBitsMask =
          kExtensionsBit | kSubgroupIdMask | kEndOfGroupBit | kTypeBit | kDefaultPriorityBit | kFirstObjectBit;

        /**
         * Decodes properties from a wire type.
         * @throws ProtocolViolationException if reserved bits, the type bit, or subgroup mode are invalid.
         */
        explicit constexpr StreamHeaderProperties(const std::uint64_t type)
          : extensions(type & kExtensionsBit)
          , subgroup_id_mode(static_cast<SubgroupIdType>((type & kSubgroupIdMask) >> 1))
          , end_of_group(type & kEndOfGroupBit)
          , default_priority(type & kDefaultPriorityBit)
          , first_object(type & kFirstObjectBit)
        {
            if (!IsValid(type)) {
                throw ProtocolViolationException("Invalid stream header type");
            }
        }

        /**
         * Constructs properties from fields.
         * @throws ProtocolViolationException if subgroup_id_mode is kReserved.
         */
        constexpr StreamHeaderProperties(const bool extensions,
                                         const SubgroupIdType subgroup_id_mode,
                                         const bool end_of_group,
                                         const bool default_priority,
                                         const bool first_object)
          : extensions(extensions)
          , subgroup_id_mode(subgroup_id_mode)
          , end_of_group(end_of_group)
          , default_priority(default_priority)
          , first_object(first_object)
        {
            if (subgroup_id_mode == SubgroupIdType::kReserved) {
                throw ProtocolViolationException("Subgroup ID mode cannot be kReserved");
            }
        }

        /// @return Encoded stream-header type bitfield.
        constexpr std::uint64_t GetType() const
        {
            std::uint64_t type = kTypeBit;
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
            if (first_object) {
                type |= kFirstObjectBit;
            }
            return type;
        }

        /// @param type Stream-header type bitfield to validate.
        /// @return Whether @p type uses only allowed bits, has the type bit, and has a nonreserved subgroup mode.
        static constexpr bool IsValid(const std::uint64_t type) noexcept
        {
            return (type & ~kAllowedBitsMask) == 0 && (type & kTypeBit) != 0 &&
                   (type & kSubgroupIdMask) != kSubgroupIdMask;
        }
    };

    /**
     * Describes the properties of a datagram header type.
     */
    struct DatagramHeaderProperties
    {
        const bool extensions;       ///< Whether an extension block is present.
        const bool end_of_group;     ///< Whether this payload object ends its group.
        const bool zero_object_id;   ///< Whether object ID is implicitly zero and omitted.
        const bool default_priority; ///< Whether priority is inherited and omitted.
        const bool status;           ///< Whether this is a status datagram rather than payload.

        static constexpr std::uint8_t kExtensionsBit = 0x01;      ///< Extensions-present bit.
        static constexpr std::uint8_t kEndOfGroupBit = 0x02;      ///< End-of-group bit.
        static constexpr std::uint8_t kZeroObjectIdBit = 0x04;    ///< Implicit-zero-object-ID bit.
        static constexpr std::uint8_t kDefaultPriorityBit = 0x08; ///< Inherited-priority bit.
        static constexpr std::uint8_t kStatusBit = 0x20;          ///< Status-datagram bit.

        /**
         * @param type Datagram header bitfield.
         * @throws ProtocolViolationException for reserved bits or status combined with end-of-group.
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

        /**
         * Builds properties without validation.
         * @param extensions Whether extensions are present.
         * @param end_of_group Whether this marks the group end.
         * @param zero_object_id Whether object ID zero is omitted.
         * @param default_priority Whether publisher priority is inherited.
         * @param status Whether this is a status datagram.
         */
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
         * @return Encoded datagram-header type bitfield.
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
        kFetchHeader,   ///< Fetch data stream.
        kSubgroupHeader ///< Subgroup object stream.
    };

    /**
     * Classifies a stream type.
     * @param type Wire type value.
     * @return Fetch or subgroup classification.
     * @throws ProtocolViolationException if @p type matches neither format.
     */
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

    /// Header opening a fetch data stream.
    struct FetchHeader
    {
        /// Wire type; defaults to kFetchHeader.
        FetchHeaderType type{ FetchHeaderType::kFetchHeader };
        uint64_t request_id; ///< Request identifier associated with the fetch.

        /**
         * Incrementally parses a fetch header.
         * @tparam StreamBufferType Incremental stream-buffer type.
         * @param buffer Input buffer.
         * @param msg Header receiving parsed fields and retaining parser state.
         * @return True once complete; false when more bytes are needed.
         * @note Consumed bytes are removed and parser state remains in @p msg.
         */
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, FetchHeader& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    /**
     * Declared contiguous-span fetch-header decoder.
     * @param buffer Input span.
     * @param msg Destination header.
     * @return Unconsumed suffix.
     * @note No definition is present in this repository; use the StreamBuffer overload.
     */
    BytesSpan operator>>(BytesSpan buffer, FetchHeader& msg);

    /**
     * Serializes type and request ID as UintVars.
     * @param buffer Destination bytes.
     * @param msg Header to serialize.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const FetchHeader& msg);

    /// Fetch data-plane serialization flag.
    struct FetchSerializationProperties
    {
        /// Special FETCH range-termination condition.
        enum class EndOfRange
        {
            kNonExistent, ///< End location is known not to exist.
            kUnknown,     ///< Existence of the end location is unknown.
        };

        /// Encoding used to reconstruct a normal stream object's subgroup ID.
        enum class SubgroupIdMode : std::uint8_t
        {
            kZero = 0x00,     ///< Subgroup ID is zero.
            kPrior = 0x01,    ///< Reuse prior subgroup ID.
            kNext = 0x02,     ///< Prior subgroup ID plus one.
            kExplicit = 0x03, ///< Read an explicit subgroup ID.
        };

        std::optional<EndOfRange> end_of_range; ///< End-of-range marker, or absent for a normal object.

        /// Subgroup mode for stream objects; absent for datagrams and end markers.
        std::optional<SubgroupIdMode> subgroup_id_mode;
        bool object_id_delta_present; ///< Whether an object-ID delta/absolute field follows.
        bool group_id_delta_present;  ///< Whether a group-ID delta/absolute field follows.
        bool priority_present;        ///< Whether publisher priority follows.
        bool properties_present;      ///< Whether an extension block follows.
        bool datagram;                ///< Whether the resolved object used datagram forwarding.

        static constexpr std::uint8_t kSubgroupMask = 0x03;           ///< Subgroup mode mask.
        static constexpr std::uint8_t kObjectIdDeltaBit = 0x04;       ///< Object-ID-field bit.
        static constexpr std::uint8_t kGroupIdDeltaBit = 0x08;        ///< Group-ID-field bit.
        static constexpr std::uint8_t kPriorityBit = 0x10;            ///< Publisher-priority-field bit.
        static constexpr std::uint8_t kPropertiesBit = 0x20;          ///< Extensions-field bit.
        static constexpr std::uint8_t kDatagramBit = 0x40;            ///< Datagram-forwarding bit.
        static constexpr std::uint64_t kEndOfNonExistentRange = 0x8C; ///< End-of-nonexistent-range marker value.
        static constexpr std::uint64_t kEndOfUnknownRange = 0x10C;    ///< End-of-unknown-range marker value.

        /** Decodes flags. @throws ProtocolViolationException for undefined values at or above 128. */
        explicit FetchSerializationProperties(std::uint64_t value);

        /**
         * Constructs normal-object flags from their components.
         * @param subgroup_id_mode Subgroup-ID encoding mode; ignored for datagrams.
         * @param object_id_delta_present Whether an object-ID field is present.
         * @param group_id_delta_present Whether a group-ID field is present.
         * @param priority_present Whether publisher priority is present.
         * @param properties_present Whether an extension block is present.
         * @param datagram Whether the resolved object uses datagram forwarding.
         */
        FetchSerializationProperties(SubgroupIdMode subgroup_id_mode,
                                     bool object_id_delta_present,
                                     bool group_id_delta_present,
                                     bool priority_present,
                                     bool properties_present,
                                     bool datagram) noexcept;

        /// @return Encoded flags or end-of-range marker.
        [[nodiscard]] std::uint64_t GetType() const noexcept;
    };

    /// One wire-level object or end-of-range marker on a fetch stream.
    struct FetchObject
    {
        /// Parsed serialization flags; required for encoding and resolution.
        std::optional<FetchSerializationProperties> properties;
        std::optional<std::uint64_t>
          group_id_delta; ///< Absolute first group ID or later group delta, as selected by flags.

        /// Explicit subgroup ID when the selected mode requires one.
        std::optional<std::uint64_t> subgroup_id;
        std::optional<std::uint64_t>
          object_id_delta; ///< Absolute first/new-group object ID or same-group object delta.

        /// Explicit publisher priority when the priority-present bit is set.
        std::optional<std::uint8_t> publisher_priority;
        std::optional<Extensions> extensions; ///< Mutable object extensions.
        std::optional<Extensions>
          immutable_extensions; ///< Immutable extensions extracted from the immutable container.

        /// Parsed payload length; serialization derives length from payload instead.
        uint64_t payload_len{ 0 };

        /// Owned payload bytes.
        Bytes payload;

        /**
         * Incrementally parses one fetch object.
         * @tparam StreamBufferType Incremental stream-buffer type.
         * @param buffer Input buffer.
         * @param msg Object receiving parsed fields and retaining parser state.
         * @return True once complete; false when more bytes are needed.
         * @note Consumed input and private parse state are retained between calls.
         */
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, FetchObject& msg);

      private:
        std::optional<std::size_t> extension_headers_length;
        std::size_t extension_bytes_remaining{ 0 };
        std::optional<uint64_t> current_tag{};
        std::uint64_t prev_extension_type{ 0 };
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    /// Fetch object after stateful delta resolution.
    struct ResolvedFetchObject
    {
        ObjectHeaders headers; ///< Absolute headers reconstructed from this object and prior state.

        /// Owned payload moved from the wire object.
        Bytes payload;
    };

    /// Fetch data-plane (de)serializer.
    class FetchObjectSerializationState
    {
      public:
        /** @param group_order Direction used to encode and resolve group deltas. */
        explicit FetchObjectSerializationState(GroupOrder group_order) noexcept
          : group_order_(group_order)
        {
        }

        /**
         * Delta-encodes an object relative to previously encoded objects.
         * @param headers Absolute object headers.
         * @param priority Effective publisher priority.
         * @param payload Non-owning payload view copied into the result.
         * @return Wire-level fetch object.
         * @throws std::invalid_argument when group/object ordering contradicts the configured order.
         * @note Updates this encoder's prior-object state.
         */
        FetchObject Encode(const ObjectHeaders& headers, std::uint8_t priority, BytesSpan payload);

        /**
         * Resolves deltas relative to previously decoded objects.
         * @param object Wire object whose payload and extensions are moved into the result.
         * @return Resolved object, or std::nullopt for an end-of-range marker.
         * @throws ProtocolViolationException for missing prior state, missing required fields, or overflow/underflow.
         * @note End markers update only prior group/object location; normal objects update all prior state.
         */
        std::optional<ResolvedFetchObject> Decode(FetchObject&& object);

      private:
        GroupOrder group_order_;
        std::optional<std::uint64_t> prior_group_id_;
        std::optional<std::uint64_t> prior_object_id_;
        std::optional<std::uint64_t> prior_subgroup_id_;
        std::optional<std::uint8_t> prior_priority_;
    };

    /**
     * Declared byte-vector fetch-object decoder.
     * @param buffer Input bytes.
     * @param msg Destination object.
     * @return Whether decoding completed.
     * @note No definition is present in this repository; use the StreamBuffer overload.
     */
    bool operator>>(Bytes& buffer, FetchObject& msg);

    /**
     * Serializes one fetch object.
     * @param buffer Destination bytes.
     * @param msg Object to serialize.
     * @return @p buffer.
     * @throws std::invalid_argument if an end marker has payload.
     * @throws ProtocolViolationException for nested immutable extensions.
     * @warning Required optionals are enforced with assertions in debug builds.
     */
    Bytes& operator<<(Bytes& buffer, const FetchObject& msg);

    //
    // Data Streams
    //

    /// Payload-bearing object datagram.
    struct ObjectDatagram
    {
        std::uint64_t track_alias; ///< Track alias encoded as UintVar.
        std::uint64_t group_id;    ///< Group identifier encoded as UintVar.

        /// Object identifier; omitted on wire when zero.
        std::uint64_t object_id;
        std::optional<std::uint8_t> priority; ///< Explicit publisher priority, or absent to inherit the track default.
        std::optional<Extensions> extensions; ///< Mutable object extensions.
        std::optional<Extensions> immutable_extensions; ///< Immutable extensions nested in the immutable container.

        /// Decoder-observed payload length; not serialized as a datagram field.
        uint64_t payload_len{ 0 };

        /// Not serialized by ObjectDatagram; payload datagrams imply available data.
        ObjectStatus object_status;
        Bytes payload;              ///< Bytes occupying the remainder of this datagram.
        bool end_of_group{ false }; ///< Whether the header marks this as the final object in its group.

        /**
         * Incrementally parses a complete datagram.
         * @tparam StreamBufferType Incremental buffer bounded to exactly one datagram.
         * @param buffer Input buffer.
         * @param msg Datagram receiving parsed fields and retaining parser state.
         * @return True once all currently bounded bytes are consumed as payload.
         * @note Datagram payload has no length field; buffer.Size() after headers
         * is treated as the final payload size. Do not feed an unbounded stream.
         */
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
        std::uint64_t prev_extension_type{ 0 };
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    /**
     * Serializes header fields followed by raw payload bytes.
     * @param buffer Destination bytes.
     * @param msg Datagram to serialize.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const ObjectDatagram& msg);

    /// Object-status datagram without payload.
    struct ObjectDatagramStatus
    {
        std::uint64_t track_alias; ///< Track alias.
        std::uint64_t group_id;    ///< Group identifier.

        /// Object identifier; omitted when zero.
        std::uint64_t object_id;
        std::optional<std::uint8_t>
          publisher_priority;                 ///< Explicit publisher priority, or absent to inherit the default.
        std::optional<Extensions> extensions; ///< Mutable extensions.
        std::optional<Extensions> immutable_extensions; ///< Immutable extensions.
        ObjectStatus status;                            ///< Reported object status encoded as UintVar.

        /**
         * Incrementally parses a status datagram.
         * @tparam StreamBufferType Incremental stream-buffer type.
         * @param buffer Input buffer.
         * @param msg Datagram receiving parsed fields and retaining parser state.
         * @return True once complete; false when more input is needed.
         */
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, ObjectDatagramStatus& msg);

        /// @return Datagram status header type derived from optional fields.
        std::uint64_t GetType() const
        {
            const auto properties = DatagramHeaderProperties(extensions.has_value() || immutable_extensions.has_value(),
                                                             false,
                                                             object_id == 0,
                                                             !publisher_priority.has_value(),
                                                             true);
            return properties.GetType();
        }

      private:
        std::optional<DatagramHeaderProperties> properties;
        std::optional<std::size_t> extension_headers_length;
        std::size_t extension_bytes_left{ 0 };
        std::optional<uint64_t> current_tag{};
        std::uint64_t prev_extension_type{ 0 };
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    /**
     * Serializes a status datagram.
     * @param buffer Destination bytes.
     * @param msg Datagram to serialize.
     * @return @p buffer.
     */
    Bytes& operator<<(Bytes& buffer, const ObjectDatagramStatus& msg);

    // SubGroups
    /// Header opening a subgroup object stream.
    struct StreamHeaderSubGroup
    {
        /// Parsed/selected stream-header flags; required for serialization.
        std::optional<StreamHeaderProperties> properties;
        std::uint64_t track_alias; ///< Track alias.
        std::uint64_t group_id;    ///< Group identifier.
        std::optional<std::uint64_t>
          subgroup_id; ///< Explicit/derived subgroup ID, or absent for set-from-first-object mode.

        /// Explicit priority, or absent when default_priority is set.
        std::optional<std::uint8_t> publisher_priority;

        /**
         * Incrementally parses a subgroup header.
         * @tparam StreamBufferType Incremental stream-buffer type.
         * @param buffer Input buffer.
         * @param msg Header receiving parsed fields and retaining parser state.
         * @return True once complete; false when more input is needed.
         */
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, StreamHeaderSubGroup& msg);

      private:
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    /**
     * Declared byte-vector subgroup-header decoder.
     * @param buffer Input bytes.
     * @param msg Destination header.
     * @return Whether decoding completed.
     * @note No definition is present in this repository; use the StreamBuffer overload.
     */
    bool operator>>(Bytes& buffer, StreamHeaderSubGroup& msg);

    /**
     * Serializes a subgroup header.
     * @param buffer Destination bytes.
     * @param msg Header to serialize.
     * @return @p buffer.
     * @throws std::invalid_argument for subgroup-ID fields inconsistent with the selected mode.
     */
    Bytes& operator<<(Bytes& buffer, const StreamHeaderSubGroup& msg);

    /// One object record carried after a StreamHeaderSubGroup.
    struct StreamSubGroupObject
    {
        std::uint64_t object_delta; ///< Object-ID delta from the stream's reference object.

        /// Parsed payload length; serialization derives this from payload.
        uint64_t payload_len{ 0 };

        /// Status serialized only when payload is empty.
        ObjectStatus object_status;
        std::optional<Extensions> extensions;           ///< Mutable extensions.
        std::optional<Extensions> immutable_extensions; ///< Immutable extensions.

        /// Owned payload bytes.
        Bytes payload;

        /// Parent stream properties; caller must set before parsing or serialization.
        std::optional<StreamHeaderProperties> properties{};

        /**
         * Incrementally parses one subgroup object.
         * @tparam StreamBufferType Incremental stream-buffer type.
         * @param buffer Input buffer.
         * @param msg Object receiving parsed fields and retaining parser state.
         * @return True once complete; false when more input is needed.
         * @note When payload length is zero, a status UintVar follows.
         */
        template<class StreamBufferType>
        friend bool operator>>(StreamBufferType& buffer, StreamSubGroupObject& msg);

      private:
        std::optional<std::size_t> extension_headers_length;
        std::size_t extension_bytes_left{ 0 };
        std::optional<uint64_t> current_tag{};
        std::uint64_t prev_extension_type{ 0 };
        uint64_t current_pos{ 0 };
        bool parse_completed{ false };
    };

    /**
     * Declared byte-vector subgroup-object decoder.
     * @param buffer Input bytes.
     * @param msg Destination object.
     * @return Whether decoding completed.
     * @note No definition is present in this repository; use the StreamBuffer overload.
     */
    bool operator>>(Bytes& buffer, StreamSubGroupObject& msg);

    /**
     * Serializes one subgroup object.
     * @param buffer Destination bytes.
     * @param msg Object to serialize.
     * @return @p buffer.
     * @warning Parent properties and extension compatibility are enforced with assertions.
     */
    Bytes& operator<<(Bytes& buffer, const StreamSubGroupObject& msg);

} // end of namespace quicr::_messages
