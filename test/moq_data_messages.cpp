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

const Extensions kExampleExtensions = { { 0x1, { 0x1, 0x2 } }, // Raw bytes.
                                        { 0x2, kUint1ByteValue },
                                        { 0x4, kUint2ByteValue },
                                        { 0x6, kUint4ByteValue },
                                        { 0x8, kUint8ByteValue } };
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

static void
ObjectDatagramEncodeDecode(bool extensions, bool end_of_group)
{
    CAPTURE(extensions);
    CAPTURE(end_of_group);
    DatagramHeaderProperties expected = { end_of_group, extensions };
    const DatagramHeaderType expected_type = expected.GetType();

    Bytes buffer;
    auto object_datagram = messages::ObjectDatagram{};
    object_datagram.track_alias = uint64_t(kTrackAliasAliceVideo);
    object_datagram.group_id = 0x1000;
    object_datagram.object_id = 0xFF;
    object_datagram.priority = 0xA;
    object_datagram.extensions = extensions ? kOptionalExtensions : std::nullopt;
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
    CHECK_EQ(object_datagram.extensions, object_datagram_out.extensions);
    CHECK(object_datagram.payload.size() > 0);
    CHECK_EQ(object_datagram.payload, object_datagram_out.payload);
}

TEST_CASE("ObjectDatagram  Message encode/decode")
{
    ObjectDatagramEncodeDecode(false, false);
    ObjectDatagramEncodeDecode(false, true);
    ObjectDatagramEncodeDecode(true, false);
    ObjectDatagramEncodeDecode(true, true);
}

static void
ObjectDatagramStatusEncodeDecode(bool extensions)
{
    CAPTURE(extensions);
    auto expected_type =
      extensions ? messages::DatagramStatusType::kWithExtensions : messages::DatagramStatusType::kNoExtensions;

    Bytes buffer;
    auto object_datagram_status = ObjectDatagramStatus{};
    object_datagram_status.track_alias = uint64_t(kTrackAliasAliceVideo);
    object_datagram_status.group_id = 0x1000;
    object_datagram_status.object_id = 0xFF;
    object_datagram_status.priority = 0xA;
    object_datagram_status.status = ObjectStatus::kAvailable;
    object_datagram_status.extensions = extensions ? kOptionalExtensions : std::nullopt;
    REQUIRE_EQ(object_datagram_status.GetType(), expected_type);

    buffer << object_datagram_status;

    ObjectDatagramStatus object_datagram_status_out;
    CHECK(Verify(buffer, object_datagram_status_out));
    CHECK_EQ(object_datagram_status.track_alias, object_datagram_status_out.track_alias);
    CHECK_EQ(object_datagram_status.group_id, object_datagram_status_out.group_id);
    CHECK_EQ(object_datagram_status.object_id, object_datagram_status_out.object_id);
    CHECK_EQ(object_datagram_status.priority, object_datagram_status_out.priority);
    CHECK_EQ(object_datagram_status.status, object_datagram_status_out.status);
    CHECK_EQ(object_datagram_status.extensions, object_datagram_status.extensions);
    CHECK_EQ(object_datagram_status.GetType(), object_datagram_status_out.GetType());
}

TEST_CASE("ObjectDatagramStatus  Message encode/decode")
{
    ObjectDatagramStatusEncodeDecode(false);
    ObjectDatagramStatusEncodeDecode(true);
}

static void
StreamHeaderEncodeDecode(StreamHeaderType type)
{
    Bytes buffer;
    auto hdr = StreamHeaderSubGroup{};
    hdr.type = type;
    hdr.track_alias = static_cast<uint64_t>(kTrackAliasAliceVideo);
    hdr.group_id = 0x1000;
    hdr.subgroup_id = 0x5000;
    hdr.priority = 0xA;
    buffer << hdr;
    StreamHeaderSubGroup hdr_out;
    CHECK(Verify(buffer, hdr_out));
    CHECK_EQ(hdr.type, hdr_out.type);
    CHECK_EQ(hdr.track_alias, hdr_out.track_alias);
    CHECK_EQ(hdr.group_id, hdr_out.group_id);

    const auto hdr_properties = StreamHeaderProperties(type);
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
StreamPerSubGroupObjectEncodeDecode(StreamHeaderType type, bool extensions, bool empty_payload)
{
    Bytes buffer;
    auto hdr_grp = messages::StreamHeaderSubGroup{};
    hdr_grp.type = type;
    hdr_grp.track_alias = uint64_t(kTrackAliasAliceVideo);
    hdr_grp.group_id = 0x1000;
    hdr_grp.subgroup_id = 0x5000;
    hdr_grp.priority = 0xA;

    buffer << hdr_grp;

    messages::StreamHeaderSubGroup hdr_group_out;
    CHECK(Verify(buffer, hdr_group_out, 1));
    CHECK_EQ(hdr_grp.type, hdr_group_out.type);
    CHECK_EQ(hdr_grp.track_alias, hdr_group_out.track_alias);
    CHECK_EQ(hdr_grp.group_id, hdr_group_out.group_id);

    // stream all the objects
    buffer.clear();
    auto objects = std::vector<messages::StreamSubGroupObject>{};
    // send 10 objects
    for (size_t i = 0; i < 10; i++) {
        auto obj = messages::StreamSubGroupObject{};
        obj.stream_type = type;
        obj.object_id = 0x1234;

        if (empty_payload) {
            obj.object_status = ObjectStatus::kDoesNotExist;
        } else {
            obj.payload = { 0x1, 0x2, 0x3, 0x4, 0x5 };
        }

        // Only set extensions if the header type allows it.
        if (StreamHeaderProperties(type).may_contain_extensions) {
            obj.extensions = extensions ? kOptionalExtensions : std::nullopt;
        }
        objects.push_back(obj);
        buffer << obj;
    }

    const auto properties = StreamHeaderProperties(type);

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

        CHECK_EQ(obj_out.object_id, objects[object_count].object_id);
        if (empty_payload) {
            CHECK_EQ(obj_out.object_status, objects[object_count].object_status);
        } else {
            CHECK(obj_out.payload.size() > 0);
            CHECK_EQ(obj_out.payload, objects[object_count].payload);
        }

        if (properties.may_contain_extensions) {
            REQUIRE_EQ(obj_out.extensions, objects[object_count].extensions);
        } else {
            REQUIRE_EQ(obj_out.extensions, std::nullopt);
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
                                  StreamHeaderType::kSubgroup0EndOfGroupNoExtensions,
                                  StreamHeaderType::kSubgroup0EndOfGroupWithExtensions,
                                  StreamHeaderType::kSubgroupFirstObjectEndOfGroupNoExtensions,
                                  StreamHeaderType::kSubgroupFirstObjectEndOfGroupWithExtensions,
                                  StreamHeaderType::kSubgroupExplicitEndOfGroupNoExtensions,
                                  StreamHeaderType::kSubgroupExplicitEndOfGroupWithExtensions };
    for (const auto& type : stream_headers) {
        CAPTURE(type);
        for (bool extensions : { true, false }) {
            CAPTURE(extensions);
            for (bool empty_payload : { true, false }) {
                CAPTURE(empty_payload);
                StreamPerSubGroupObjectEncodeDecode(type, extensions, empty_payload);
            }
        }
    }
}

static void
FetchStreamEncodeDecode(bool extensions, bool empty_payload)
{
    CAPTURE(extensions);
    CAPTURE(empty_payload);

    Bytes buffer;
    auto fetch_header = messages::FetchHeader{};
    fetch_header.subscribe_id = 0x1234;

    buffer << fetch_header;

    messages::FetchHeader fetch_header_out;
    CHECK(Verify(buffer, fetch_header_out));
    CHECK_EQ(fetch_header.type, fetch_header_out.type);
    CHECK_EQ(fetch_header.type, FetchHeaderType::kFetchHeader);
    CHECK_EQ(fetch_header.subscribe_id, fetch_header_out.subscribe_id);

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

        obj.extensions = extensions ? kOptionalExtensions : std::nullopt;
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
        CHECK_EQ(obj_out.extensions, objects[object_count].extensions);
        // got one object
        object_count++;
        obj_out = {};
        in_buffer.Pop(in_buffer.Size());
    }

    CHECK_EQ(object_count, objects.size());
}

TEST_CASE("Fetch Stream Message encode/decode")
{
    FetchStreamEncodeDecode(false, true);
    FetchStreamEncodeDecode(false, false);
    FetchStreamEncodeDecode(true, true);
    FetchStreamEncodeDecode(true, false);
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
