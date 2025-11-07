// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/messages.h"

#include <any>
#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <sys/socket.h>

using namespace quicr;
using namespace quicr::messages;
using namespace std::string_literals;

static Bytes
FromASCII(const std::string& ascii)
{
    return std::vector<uint8_t>(ascii.begin(), ascii.end());
}

const TrackNamespace kTrackNamespaceConf{ FromASCII("conf.example.com"), FromASCII("conf"), FromASCII("1") };
const Bytes kTrackNameAliceVideo = FromASCII("alice/video");
const UintVar kTrackAliasAliceVideo{ 0xA11CE };

// Values that will encode to the corresponding UintVar values.
const Bytes kUint1ByteValue = { 0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const Bytes kUint2ByteValue = { 0xBD, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const Bytes kUint4ByteValue = { 0x7D, 0x3E, 0x7F, 0x1D, 0x00, 0x00, 0x00, 0x00 };
const Bytes kUint8ByteValue = { 0x8C, 0xE8, 0x14, 0xFF, 0x5E, 0x7C, 0x19, 0x02 };

enum class ExtensionTest
{
    kNone = 0,
    kMutable = 1,
    kImmutable = 2,
    kBoth = 3,
};
const Extensions kExampleExtensions = { { 0x1, { { 0x1, 0x2 } } }, // Raw bytes.
                                        { 0x2, { kUint1ByteValue } },
                                        { 0x4, { kUint2ByteValue } },
                                        { 0x6, { kUint4ByteValue } },
                                        { 0x8, { kUint8ByteValue } } };
const std::optional<Extensions> kOptionalExtensions = kExampleExtensions;

template<typename T>
bool
Verify(std::vector<uint8_t>& buffer, T& message, [[maybe_unused]] size_t slice_depth = 1)
{
    // TODO: support Size_depth > 1, if needed
    StreamBuffer<uint8_t> in_buffer;
    in_buffer.InitAny<T>(); // Set parsed data to be of this type using out param

    bool done = false;
    for (auto& v : buffer) {
        auto& msg = in_buffer.GetAny<T>();
        in_buffer.Push(v);
        done = in_buffer >> msg;
        if (done) {
            // copy the working parsed data to out param.
            message = msg;
            break;
        }
    }
    return done;
}

template<typename T>
bool
VerifyCtrl(BytesSpan buffer, uint64_t message_type, T& message)
{
    uint64_t msg_type = 0;
    uint64_t length = 0;
    buffer = buffer >> msg_type;
    buffer = buffer >> length;

    CHECK_EQ(msg_type, message_type);
    CHECK_EQ(length, buffer.size());

    buffer = buffer >> message;

    return true;
}

static bool
CompareExtensions(const std::optional<Extensions>& sent, const std::optional<Extensions>& recv, bool expect_immutable)
{
    if (!expect_immutable) {
        return sent == recv;
    }

    // The immutable extensions serialized blob will be contained in the received
    // extensions, but not in the sent. Check it's there, but remove it to make
    // mutable check 1:1 equality. The blob's content should be ensured
    // by a separate decoded immutable equality check.
    REQUIRE(recv.has_value());
    constexpr auto key = static_cast<std::uint64_t>(ExtensionHeaderType::kImmutable);
    REQUIRE(recv->contains(key));
    auto it = recv->find(key);
    REQUIRE(it != recv->end());
    REQUIRE_GT(it->second.size(), 0);
    CHECK_GT(it->second[0].size(), 0);
    auto copy = std::move(recv);
    copy->erase(key);
    if (copy->size() == 0) {
        copy = std::nullopt;
    }
    return sent == copy;
}

static void
ObjectDatagramEncodeDecode(ExtensionTest extensions, bool end_of_group)
{
    CAPTURE(extensions);
    CAPTURE(end_of_group);
    DatagramHeaderProperties expected = { end_of_group, extensions != ExtensionTest::kNone };
    const DatagramHeaderType expected_type = expected.GetType();

    Bytes buffer;
    auto object_datagram = messages::ObjectDatagram{};
    object_datagram.track_alias = uint64_t(kTrackAliasAliceVideo);
    object_datagram.group_id = 0x1000;
    object_datagram.object_id = 0xFF;
    object_datagram.priority = 0xA;
    if (extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kMutable) {
        object_datagram.extensions = kOptionalExtensions;
    } else {
        object_datagram.extensions = std::nullopt;
    }
    if (extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kImmutable) {
        object_datagram.immutable_extensions = kOptionalExtensions;
    } else {
        object_datagram.immutable_extensions = std::nullopt;
    }
    object_datagram.payload = { 0x1, 0x2, 0x3, 0x5, 0x6 };
    object_datagram.end_of_group = end_of_group;
    REQUIRE_EQ(object_datagram.GetType(), expected_type);

    buffer << object_datagram;

    messages::ObjectDatagram object_datagram_out;
    StreamBuffer<uint8_t> sbuf;
    sbuf.Push(buffer);

    sbuf >> object_datagram_out;

    CHECK_EQ(object_datagram_out.GetType(), expected_type);
    CHECK_EQ(object_datagram.track_alias, object_datagram_out.track_alias);
    CHECK_EQ(object_datagram.group_id, object_datagram_out.group_id);
    CHECK_EQ(object_datagram.object_id, object_datagram_out.object_id);
    CHECK_EQ(object_datagram.priority, object_datagram_out.priority);
    CompareExtensions(object_datagram.extensions,
                      object_datagram_out.extensions,
                      extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kImmutable);
    CHECK_EQ(object_datagram.immutable_extensions, object_datagram_out.immutable_extensions);
    CHECK(object_datagram.payload.size() > 0);
    CHECK_EQ(object_datagram.payload, object_datagram_out.payload);
}

TEST_CASE("ObjectDatagram  Message encode/decode")
{
    for (const auto ext :
         { ExtensionTest::kNone, ExtensionTest::kMutable, ExtensionTest::kImmutable, ExtensionTest::kBoth }) {
        ObjectDatagramEncodeDecode(ext, false);
        ObjectDatagramEncodeDecode(ext, true);
    }
}

static void
ObjectDatagramStatusEncodeDecode(ExtensionTest extensions)
{
    CAPTURE(extensions);
    auto expected_type =
      extensions != ExtensionTest::kNone ? DatagramStatusType::kWithExtensions : DatagramStatusType::kNoExtensions;

    Bytes buffer;
    auto object_datagram_status = ObjectDatagramStatus{};
    object_datagram_status.track_alias = uint64_t(kTrackAliasAliceVideo);
    object_datagram_status.group_id = 0x1000;
    object_datagram_status.object_id = 0xFF;
    object_datagram_status.priority = 0xA;
    object_datagram_status.status = ObjectStatus::kAvailable;
    if (extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kMutable) {
        object_datagram_status.extensions = kOptionalExtensions;
    } else {
        object_datagram_status.extensions = std::nullopt;
    }
    if (extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kImmutable) {
        object_datagram_status.immutable_extensions = kOptionalExtensions;
    } else {
        object_datagram_status.immutable_extensions = std::nullopt;
    }
    REQUIRE_EQ(object_datagram_status.GetType(), expected_type);

    buffer << object_datagram_status;

    ObjectDatagramStatus object_datagram_status_out;
    CHECK(Verify(buffer, object_datagram_status_out));
    CHECK_EQ(object_datagram_status.track_alias, object_datagram_status_out.track_alias);
    CHECK_EQ(object_datagram_status.group_id, object_datagram_status_out.group_id);
    CHECK_EQ(object_datagram_status.object_id, object_datagram_status_out.object_id);
    CHECK_EQ(object_datagram_status.priority, object_datagram_status_out.priority);
    CHECK_EQ(object_datagram_status.status, object_datagram_status_out.status);
    CompareExtensions(object_datagram_status.extensions,
                      object_datagram_status_out.extensions,
                      extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kImmutable);
    CHECK_EQ(object_datagram_status.immutable_extensions, object_datagram_status.immutable_extensions);
    CHECK_EQ(object_datagram_status.GetType(), object_datagram_status_out.GetType());
}

TEST_CASE("ObjectDatagramStatus  Message encode/decode")
{
    for (const auto ext :
         { ExtensionTest::kNone, ExtensionTest::kMutable, ExtensionTest::kImmutable, ExtensionTest::kBoth }) {
        ObjectDatagramStatusEncodeDecode(ext);
    }
}

static void
StreamHeaderEncodeDecode(StreamHeaderType type)
{
    const auto hdr_properties = StreamHeaderProperties(type);

    Bytes buffer;
    auto hdr = StreamHeaderSubGroup{};
    hdr.type = type;
    hdr.track_alias = static_cast<uint64_t>(kTrackAliasAliceVideo);
    hdr.group_id = 0x1000;
    if (hdr_properties.subgroup_id_type == SubgroupIdType::kExplicit) {
        hdr.subgroup_id = 0x5000;
    }
    hdr.priority = 0xA;
    buffer << hdr;
    StreamHeaderSubGroup hdr_out;
    CHECK(Verify(buffer, hdr_out));
    CHECK_EQ(hdr.type, hdr_out.type);
    CHECK_EQ(hdr.track_alias, hdr_out.track_alias);
    CHECK_EQ(hdr.group_id, hdr_out.group_id);

    switch (hdr_properties.subgroup_id_type) {
        case SubgroupIdType::kIsZero:
            CHECK_EQ(hdr_out.subgroup_id, 0);
            return;
        case SubgroupIdType::kSetFromFirstObject:
            CHECK_EQ(hdr_out.subgroup_id, std::nullopt);
            return;
        case SubgroupIdType::kExplicit:
            CHECK_EQ(hdr_out.subgroup_id, hdr.subgroup_id);
            return;
    }
    FAIL("Bad subgroup id type in StreamHeader");
}

TEST_CASE("StreamHeader Message encode/decode")
{
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroup0NotEndOfGroupNoExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroup0NotEndOfGroupWithExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupNoExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupWithExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroupExplicitNotEndOfGroupNoExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroupExplicitNotEndOfGroupWithExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroup0EndOfGroupNoExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroup0EndOfGroupWithExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions);
    StreamHeaderEncodeDecode(StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions);
}

static void
StreamPerSubGroupObjectEncodeDecode(StreamHeaderType type, ExtensionTest extensions, bool empty_payload)
{
    const auto properties = StreamHeaderProperties(type);

    Bytes buffer;
    auto hdr_grp = messages::StreamHeaderSubGroup{};
    hdr_grp.type = type;
    hdr_grp.track_alias = uint64_t(kTrackAliasAliceVideo);
    hdr_grp.group_id = 0x1000;
    if (properties.subgroup_id_type == SubgroupIdType::kExplicit) {
        hdr_grp.subgroup_id = 0x5000;
    }
    hdr_grp.priority = 0xA;

    buffer << hdr_grp;

    messages::StreamHeaderSubGroup hdr_group_out;
    CHECK(Verify(buffer, hdr_group_out, 1));
    CHECK_EQ(hdr_grp.type, hdr_group_out.type);
    CHECK_EQ(hdr_grp.track_alias, hdr_group_out.track_alias);
    CHECK_EQ(hdr_grp.group_id, hdr_group_out.group_id);
    switch (properties.subgroup_id_type) {
        case SubgroupIdType::kIsZero:
            CHECK_EQ(hdr_group_out.subgroup_id, 0);
            return;
        case SubgroupIdType::kSetFromFirstObject:
            CHECK_EQ(hdr_group_out.subgroup_id, std::nullopt);
            return;
        case SubgroupIdType::kExplicit:
            CHECK_EQ(hdr_group_out.subgroup_id, hdr_grp.subgroup_id);
            return;
    }

    // stream all the objects
    buffer.clear();
    auto objects = std::vector<messages::StreamSubGroupObject>{};
    // send 10 objects
    for (size_t i = 0; i < 10; i++) {
        auto obj = messages::StreamSubGroupObject{};
        obj.stream_type = type;
        obj.object_delta = 0x1234;

        if (empty_payload) {
            obj.object_status = ObjectStatus::kDoesNotExist;
        } else {
            obj.payload = { 0x1, 0x2, 0x3, 0x4, 0x5 };
        }

        // Only set extensions if the header type allows it.
        if (properties.may_contain_extensions) {
            if (extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kMutable) {
                obj.extensions = kOptionalExtensions;
            } else {
                obj.extensions = std::nullopt;
            }
            if (extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kImmutable) {
                obj.immutable_extensions = kOptionalExtensions;
            } else {
                obj.immutable_extensions = std::nullopt;
            }
        }
        objects.push_back(obj);
        buffer << obj;
    }

    auto obj_out = messages::StreamSubGroupObject{};
    size_t object_count = 0;
    StreamBuffer<uint8_t> in_buffer;
    for (size_t i = 0; i < buffer.size(); i++) {
        obj_out.stream_type = type;
        in_buffer.Push(buffer.at(i));
        bool done = in_buffer >> obj_out;
        if (!done) {
            continue;
        }

        CHECK_EQ(obj_out.object_delta, objects[object_count].object_delta);
        if (empty_payload) {
            CHECK_EQ(obj_out.object_status, objects[object_count].object_status);
        } else {
            CHECK(obj_out.payload.size() > 0);
            CHECK_EQ(obj_out.payload, objects[object_count].payload);
        }

        if (properties.may_contain_extensions) {
            CompareExtensions(objects[object_count].extensions,
                              obj_out.extensions,
                              extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kImmutable);
            CHECK_EQ(obj_out.extensions, objects[object_count].extensions);
            CHECK_EQ(obj_out.immutable_extensions, objects[object_count].immutable_extensions);
        } else {
            CHECK_EQ(obj_out.extensions, std::nullopt);
            CHECK_EQ(obj_out.immutable_extensions, std::nullopt);
        }
        // got one object
        object_count++;
        obj_out = {};
        in_buffer.Pop(in_buffer.Size());
    }

    CHECK_EQ(object_count, objects.size());
}

TEST_CASE("StreamPerSubGroup Object Message encode/decode")
{
    const auto stream_headers = { StreamHeaderType::kSubgroup0NotEndOfGroupNoExtensions,
                                  StreamHeaderType::kSubgroup0NotEndOfGroupWithExtensions,
                                  StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupNoExtensions,
                                  StreamHeaderType::kSubgroupFirstObjectNotEndOfGroupWithExtensions,
                                  StreamHeaderType::kSubgroupExplicitNotEndOfGroupNoExtensions,
                                  StreamHeaderType::kSubgroupExplicitNotEndOfGroupWithExtensions,
                                  StreamHeaderType::kSubgroup0EndOfGroupNoExtensions,
                                  StreamHeaderType::kSubgroup0EndOfGroupWithExtensions,
                                  StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions,
                                  StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions,
                                  StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions,
                                  StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions };
    for (const auto& type : stream_headers) {
        CAPTURE(type);
        for (const auto ext :
             { ExtensionTest::kNone, ExtensionTest::kMutable, ExtensionTest::kImmutable, ExtensionTest::kBoth }) {
            CAPTURE(ext);
            for (bool empty_payload : { true, false }) {
                CAPTURE(empty_payload);
                StreamPerSubGroupObjectEncodeDecode(type, ext, empty_payload);
            }
        }
    }
}

static void
FetchStreamEncodeDecode(ExtensionTest extensions, bool empty_payload)
{
    CAPTURE(extensions);
    CAPTURE(empty_payload);

    Bytes buffer;
    auto fetch_header = messages::FetchHeader{};
    fetch_header.request_id = 0x1234;

    buffer << fetch_header;

    messages::FetchHeader fetch_header_out;
    CHECK(Verify(buffer, fetch_header_out));
    CHECK_EQ(fetch_header.type, fetch_header_out.type);
    CHECK_EQ(fetch_header.type, FetchHeaderType::kFetchHeader);
    CHECK_EQ(fetch_header.request_id, fetch_header_out.request_id);

    // stream all the objects
    buffer.clear();
    auto objects = std::vector<messages::FetchObject>{};
    // send 10 objects
    for (size_t i = 0; i < 10; i++) {
        auto obj = messages::FetchObject{};
        obj.group_id = 0x1234;
        obj.subgroup_id = 0x5678;
        obj.object_id = 0x9012;
        obj.publisher_priority = 127;

        if (empty_payload) {
            obj.object_status = ObjectStatus::kDoesNotExist;
        } else {
            obj.payload = { 0x1, 0x2, 0x3, 0x4, 0x5 };
        }

        if (extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kMutable) {
            obj.extensions = kOptionalExtensions;
        } else {
            obj.extensions = std::nullopt;
        }
        if (extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kImmutable) {
            obj.immutable_extensions = kOptionalExtensions;
        } else {
            obj.immutable_extensions = std::nullopt;
        }
        objects.push_back(obj);
        buffer << obj;
    }

    auto obj_out = messages::FetchObject{};
    size_t object_count = 0;
    StreamBuffer<uint8_t> in_buffer;
    for (size_t i = 0; i < buffer.size(); i++) {
        in_buffer.Push(buffer.at(i));
        bool done = in_buffer >> obj_out;
        if (!done) {
            continue;
        }
        CHECK_EQ(obj_out.group_id, objects[object_count].group_id);
        CHECK_EQ(obj_out.subgroup_id, objects[object_count].subgroup_id);
        CHECK_EQ(obj_out.object_id, objects[object_count].object_id);
        CHECK_EQ(obj_out.publisher_priority, objects[object_count].publisher_priority);
        if (empty_payload) {
            CHECK_EQ(obj_out.object_status, objects[object_count].object_status);
        } else {
            CHECK(obj_out.payload.size() > 0);
            CHECK_EQ(obj_out.payload, objects[object_count].payload);
        }
        CompareExtensions(objects[object_count].extensions,
                          obj_out.extensions,
                          extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kImmutable);
        CHECK_EQ(obj_out.immutable_extensions, objects[object_count].immutable_extensions);
        // got one object
        object_count++;
        obj_out = {};
        in_buffer.Pop(in_buffer.Size());
    }

    CHECK_EQ(object_count, objects.size());
}

TEST_CASE("Fetch Stream Message encode/decode")
{
    for (const auto ext :
         { ExtensionTest::kNone, ExtensionTest::kMutable, ExtensionTest::kImmutable, ExtensionTest::kBoth }) {
        FetchStreamEncodeDecode(ext, true);
        FetchStreamEncodeDecode(ext, false);
    }
}

TEST_CASE("Key Value Pair size")
{
    SUBCASE("ODD")
    {
        // Odd should be uintvar bytes of type + uintvar bytes of value's length + n value bytes.
        CAPTURE("ODD");
        KeyValuePair<std::uint64_t> kvp;
        kvp.value = { 0x01, 0x02, 0x03 };
        const UintVar kvp_type = 1;
        kvp.type = kvp_type.Get();
        const std::size_t expected_size = kvp_type.size() + UintVar(kvp.value.size()).size() + kvp.value.size();
        REQUIRE_EQ(expected_size, 5); // 1 byte for type, 1 byte for length, 3 bytes for value.
        CHECK_EQ(kvp.Size(), expected_size);
    }

    SUBCASE("1 Byte Value")
    {
        CAPTURE("Even - 1 byte");
        KeyValuePair<std::uint64_t> kvp;
        const UintVar kvp_type = 2;
        kvp.type = kvp_type.Get();
        kvp.value = kUint1ByteValue;
        CHECK_EQ(kvp.Size(), 2); // 1 byte for type, 1 byte for value.
    }

    SUBCASE("2 Byte Value")
    {
        CAPTURE("Even - 2 byte");
        KeyValuePair<std::uint64_t> kvp;
        const UintVar kvp_type = 2;
        kvp.type = kvp_type.Get();
        kvp.value = kUint2ByteValue;
        CHECK_EQ(kvp.Size(), 3); // 1 byte for type, 2 bytes for value.
    }

    SUBCASE("4 Byte Value")
    {
        CAPTURE("Even - 4 byte");
        KeyValuePair<std::uint64_t> kvp;
        const UintVar kvp_type = 2;
        kvp.type = kvp_type.Get();
        kvp.value = kUint4ByteValue;
        CHECK_EQ(kvp.Size(), 5); // 1 byte for type, 4 bytes for value.
    }

    SUBCASE("8 Byte Value")
    {
        CAPTURE("Even - 8 byte");
        KeyValuePair<std::uint64_t> kvp;
        const UintVar kvp_type = 2;
        kvp.type = kvp_type.Get();
        kvp.value = kUint8ByteValue;
        CHECK_EQ(kvp.Size(), 9); // 1 byte for type, 8 bytes for value.
    }
}

TEST_CASE("Immutable Extensions Nesting")
{
    Extensions nested_immutable = {
        { static_cast<std::uint64_t>(ExtensionHeaderType::kImmutable),
          { { 0xAA, 0xBB } } } // This should cause validation to fail
    };

    FetchObject msg;
    msg.group_id = 1;
    msg.subgroup_id = 2;
    msg.object_id = 3;
    msg.publisher_priority = 4;
    msg.immutable_extensions = nested_immutable;
    msg.payload_len = 0;
    msg.object_status = ObjectStatus::kAvailable;

    // Serialization should throw ProtocolViolationException
    Bytes buffer;
    CHECK_THROWS_AS(buffer << msg, ProtocolViolationException);
}

TEST_CASE("Extensions with duplicate keys")
{
    // Create extensions with multiple values for the same key
    Extensions extensions_with_duplicates;
    extensions_with_duplicates[0x1].push_back({ 0xAA });
    extensions_with_duplicates[0x1].push_back({ 0xBB });
    extensions_with_duplicates[0x1].push_back({ 0xCC });
    extensions_with_duplicates[0x3].push_back({ 0x11, 0x22 });

    // Serialize the extensions
    Bytes buffer;
    buffer << extensions_with_duplicates;

    // Deserialize back
    StreamBuffer<uint8_t> in_buffer;
    in_buffer.Push(buffer);
    std::optional<std::size_t> extension_headers_length;
    std::optional<Extensions> parsed_extensions;
    std::optional<Extensions> parsed_immutable_extensions;
    std::size_t extension_bytes_remaining = 0;
    std::optional<std::uint64_t> current_header;

    bool success = ParseExtensions(in_buffer,
                                   extension_headers_length,
                                   parsed_extensions,
                                   parsed_immutable_extensions,
                                   extension_bytes_remaining,
                                   current_header);

    REQUIRE(success);
    REQUIRE(parsed_extensions.has_value());
    CHECK_EQ(parsed_extensions->size(), 2); // Should have 2 keys

    // Verify all three values for key 0x1 are present
    REQUIRE(parsed_extensions->contains(0x1));
    const auto& values_for_key_1 = parsed_extensions->at(0x1);
    REQUIRE_EQ(values_for_key_1.size(), 3);
    CHECK_EQ(values_for_key_1[0], Bytes{ 0xAA });
    CHECK_EQ(values_for_key_1[1], Bytes{ 0xBB });
    CHECK_EQ(values_for_key_1[2], Bytes{ 0xCC });

    // Verify single value for key 0x3
    REQUIRE(parsed_extensions->contains(0x3));
    const auto& values_for_key_3 = parsed_extensions->at(0x3);
    REQUIRE_EQ(values_for_key_3.size(), 1);
    CHECK_EQ(values_for_key_3[0], Bytes{ 0x11, 0x22 });
}

TEST_CASE("Null extensions serialize to 0 length")
{
    Bytes bytes;
    std::optional<Extensions> extensions;
    std::optional<Extensions> immutable;
    SerializeExtensions(bytes, extensions, immutable);
    REQUIRE_EQ(bytes.size(), 1);
    CHECK_EQ(bytes[0], 0);
}

TEST_CASE("Immutable Extensions not length prefixed")
{
    // Test keys and values.
    constexpr uint64_t even_type_key = 0x02;
    constexpr uint64_t odd_type_key = 0x03;
    constexpr uint64_t varint_value = 0xC0;
    const Bytes bytes_value = { 0x42, 0x43 };

    // Create immutable extensions with known values.
    Extensions immutable_ext;
    immutable_ext[even_type_key].push_back({ varint_value, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });
    immutable_ext[odd_type_key].push_back(bytes_value);

    // Serialize
    Bytes buffer;
    std::optional<Extensions> mutable_ext;
    SerializeExtensions(buffer, mutable_ext, immutable_ext);

    // On the wire it should be:
    // - Total length of all extensions (varint)
    // - Type 0xB (immutable)
    // - Total length of immutable extension bytes (varint)
    // - Raw KVPs of immutable extensions:
    //   - First KVP: even type (varint value)
    //   - Second KVP: odd type (TLV byte array)
    // Total inner KVPs: 1 + 2 + 1 + 1 + 2 = 7 bytes

    // Overall: 1 byte for total block length + 9 bytes of immutable header blob = 10.
    REQUIRE_EQ(buffer.size(), 10);

    // Verify structure byte by byte.
    size_t idx = 0;

    // First byte is total following bytes of extensions = 9 bytes.
    CHECK_EQ(buffer[idx++], 0x09);

    // Next is the key for Immutable Extensions header.
    CHECK_EQ(buffer[idx++], static_cast<uint8_t>(ExtensionHeaderType::kImmutable));

    // Then the total length of all bytes of KVPs (7 bytes).
    CHECK_EQ(buffer[idx++], 0x07);

    // First KVP: even type key, varint value.
    CHECK_EQ(buffer[idx++], even_type_key);
    CHECK_EQ(buffer[idx++], 0x40); // Varint byte 1.
    CHECK_EQ(buffer[idx++], varint_value);

    // Second KVP: odd type key, byte array value.
    CHECK_EQ(buffer[idx++], odd_type_key);
    CHECK_EQ(buffer[idx++], bytes_value.size());
    CHECK_EQ(buffer[idx++], bytes_value[0]);
    CHECK_EQ(buffer[idx++], bytes_value[1]);

    // Round trip.
    StreamBuffer<uint8_t> in_buffer;
    in_buffer.Push(buffer);
    std::optional<std::size_t> extension_headers_length;
    std::optional<Extensions> parsed_extensions;
    std::optional<Extensions> parsed_immutable_extensions;
    std::size_t extension_bytes_remaining = 0;
    std::optional<std::uint64_t> current_header;

    bool success = ParseExtensions(in_buffer,
                                   extension_headers_length,
                                   parsed_extensions,
                                   parsed_immutable_extensions,
                                   extension_bytes_remaining,
                                   current_header);

    REQUIRE(success);
    REQUIRE(parsed_immutable_extensions.has_value());
    CHECK_EQ(parsed_immutable_extensions->size(), 2);

    // Verify even type key value.
    REQUIRE(parsed_immutable_extensions->contains(even_type_key));
    CHECK_EQ(parsed_immutable_extensions->at(even_type_key).size(), 1);
    CHECK_EQ(parsed_immutable_extensions->at(even_type_key)[0],
             Bytes{ varint_value, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 });

    // Verify odd type key value.
    REQUIRE(parsed_immutable_extensions->contains(odd_type_key));
    CHECK_EQ(parsed_immutable_extensions->at(odd_type_key).size(), 1);
    CHECK_EQ(parsed_immutable_extensions->at(odd_type_key)[0], bytes_value);
}
