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
    constexpr auto key = static_cast<std::uint64_t>(ExtensionType::kImmutable);
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
ObjectDatagramEncodeDecode(ExtensionTest extensions, bool end_of_group, bool non_zero_object_id)
{
    CAPTURE(extensions);
    CAPTURE(end_of_group);
    CAPTURE(non_zero_object_id);
    const auto expected =
      DatagramHeaderProperties(extensions != ExtensionTest::kNone, end_of_group, !non_zero_object_id, false, false);
    const std::uint64_t expected_type = expected.GetType();

    Bytes buffer;
    auto object_datagram = messages::ObjectDatagram{};
    object_datagram.track_alias = uint64_t(kTrackAliasAliceVideo);
    object_datagram.group_id = 0x1000;
    object_datagram.object_id = non_zero_object_id ? 0xFF : 0;
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
    REQUIRE_EQ(object_datagram.GetProperties().GetType(), expected_type);

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
        ObjectDatagramEncodeDecode(ext, false, false);
        ObjectDatagramEncodeDecode(ext, true, false);
        ObjectDatagramEncodeDecode(ext, false, true);
        ObjectDatagramEncodeDecode(ext, true, true);
    }
}

static void
ObjectDatagramStatusEncodeDecode(ExtensionTest extensions)
{
    CAPTURE(extensions);
    const auto expected_type =
      DatagramHeaderProperties(extensions != ExtensionTest::kNone, false, false, false, true).GetType();

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
    StreamBuffer<uint8_t> sbuf;
    sbuf.Push(buffer);
    sbuf >> object_datagram_status_out;
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

TEST_CASE("DatagramHeaderProperties encoding")
{
    // Bit layout per draft-ietf-moq-transport:
    // bit 0 (0x01): EXTENSIONS - when 1, extensions field present
    // bit 1 (0x02): END_OF_GROUP - when 1, end of group
    // bit 2 (0x04): ZERO_OBJECT_ID - when 1, object ID is omitted (assumed 0)
    // bit 3 (0x08): DEFAULT_PRIORITY - when 1, priority is omitted
    // bit 5 (0x20): STATUS - when 1, status message (no payload)

    SUBCASE("Type to properties - basic types")
    {
        // Type 0x00: all bits clear
        auto props0 = DatagramHeaderProperties(0x00);
        CHECK_EQ(props0.extensions, false);
        CHECK_EQ(props0.end_of_group, false);
        CHECK_EQ(props0.zero_object_id, false);
        CHECK_EQ(props0.default_priority, false);
        CHECK_EQ(props0.status, false);

        // Type 0x01: extensions bit set
        auto props1 = DatagramHeaderProperties(0x01);
        CHECK_EQ(props1.extensions, true);
        CHECK_EQ(props1.end_of_group, false);
        CHECK_EQ(props1.zero_object_id, false);

        // Type 0x02: end_of_group bit set
        auto props2 = DatagramHeaderProperties(0x02);
        CHECK_EQ(props2.extensions, false);
        CHECK_EQ(props2.end_of_group, true);
        CHECK_EQ(props2.zero_object_id, false);

        // Type 0x03: extensions and end_of_group
        auto props3 = DatagramHeaderProperties(0x03);
        CHECK_EQ(props3.extensions, true);
        CHECK_EQ(props3.end_of_group, true);
        CHECK_EQ(props3.zero_object_id, false);

        // Type 0x04: zero_object_id bit set (object ID omitted)
        auto props4 = DatagramHeaderProperties(0x04);
        CHECK_EQ(props4.extensions, false);
        CHECK_EQ(props4.end_of_group, false);
        CHECK_EQ(props4.zero_object_id, true);

        // Type 0x08: default_priority bit set
        auto props8 = DatagramHeaderProperties(0x08);
        CHECK_EQ(props8.default_priority, true);

        // Type 0x20: status bit set
        auto props_status = DatagramHeaderProperties(0x20);
        CHECK_EQ(props_status.status, true);
        CHECK_EQ(props_status.end_of_group, false);
    }

    SUBCASE("Properties to type")
    {
        // extensions=false, end_of_group=false, zero_object_id=false, default_priority=false, status=false
        CHECK_EQ(DatagramHeaderProperties(false, false, false, false, false).GetType(), 0x00);

        // extensions=true
        CHECK_EQ(DatagramHeaderProperties(true, false, false, false, false).GetType(), 0x01);

        // end_of_group=true
        CHECK_EQ(DatagramHeaderProperties(false, true, false, false, false).GetType(), 0x02);

        // zero_object_id=true (object ID omitted)
        CHECK_EQ(DatagramHeaderProperties(false, false, true, false, false).GetType(), 0x04);

        // default_priority=true
        CHECK_EQ(DatagramHeaderProperties(false, false, false, true, false).GetType(), 0x08);

        // status=true
        CHECK_EQ(DatagramHeaderProperties(false, false, false, false, true).GetType(), 0x20);

        // Combined: extensions + end_of_group + zero_object_id
        CHECK_EQ(DatagramHeaderProperties(true, true, true, false, false).GetType(), 0x07);

        // Combined: all payload bits
        CHECK_EQ(DatagramHeaderProperties(true, true, true, true, false).GetType(), 0x0F);
    }

    SUBCASE("Round trip valid types")
    {
        // Valid types are 0x00-0x0F (payload) and 0x20-0x2F (status, except those with END_OF_GROUP)
        // Test payload types (0x00-0x0F)
        for (uint8_t i = 0x00; i <= 0x0F; i++) {
            CAPTURE(i);
            auto props = DatagramHeaderProperties(i);
            CHECK_EQ(props.GetType(), i);
        }

        // Test status types without END_OF_GROUP (status=1, end_of_group=0)
        // Valid status types: 0x20, 0x21, 0x24, 0x25, 0x28, 0x29, 0x2C, 0x2D
        for (uint8_t i = 0x20; i <= 0x2F; i++) {
            bool has_end_of_group = (i & 0x02) != 0;
            if (!has_end_of_group) {
                CAPTURE(i);
                auto props = DatagramHeaderProperties(i);
                CHECK_EQ(props.GetType(), i);
            }
        }
    }

    SUBCASE("Invalid types throw")
    {
        // Bit 4 must be 0 (invalid: 0x10-0x1F outside subgroup range)
        CHECK_THROWS_AS(DatagramHeaderProperties(0x10), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0x1F), ProtocolViolationException);

        // Bits 6-7 must be 0
        CHECK_THROWS_AS(DatagramHeaderProperties(0x40), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0x80), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0xC0), ProtocolViolationException);

        // STATUS + END_OF_GROUP is invalid (0x22, 0x23, 0x26, 0x27, 0x2A, 0x2B, 0x2E, 0x2F)
        CHECK_THROWS_AS(DatagramHeaderProperties(0x22), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0x23), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0x26), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0x27), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0x2A), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0x2B), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0x2E), ProtocolViolationException);
        CHECK_THROWS_AS(DatagramHeaderProperties(0x2F), ProtocolViolationException);
    }
}

static void
StreamHeaderEncodeDecode(StreamHeaderProperties type)
{
    Bytes buffer;
    auto hdr = StreamHeaderSubGroup{};
    hdr.properties.emplace(type);
    hdr.track_alias = static_cast<uint64_t>(kTrackAliasAliceVideo);
    hdr.group_id = 0x1000;
    if (hdr.properties->subgroup_id_mode == SubgroupIdType::kExplicit) {
        hdr.subgroup_id = 0x5000;
    }
    hdr.priority = 0xA;
    buffer << hdr;

    StreamHeaderSubGroup hdr_out;
    StreamBuffer<uint8_t> sbuf;
    sbuf.Push(buffer);
    bool parsed = sbuf >> hdr_out;
    CHECK(parsed);

    CHECK(hdr.properties.has_value());
    CHECK(hdr_out.properties.has_value());
    CHECK_EQ(hdr.properties->GetType(), hdr_out.properties->GetType());
    CHECK_EQ(hdr.track_alias, hdr_out.track_alias);
    CHECK_EQ(hdr.group_id, hdr_out.group_id);

    switch (hdr.properties->subgroup_id_mode) {
        case SubgroupIdType::kIsZero:
            CHECK_EQ(hdr_out.subgroup_id, 0);
            return;
        case SubgroupIdType::kSetFromFirstObject:
            CHECK_EQ(hdr_out.subgroup_id, std::nullopt);
            return;
        case SubgroupIdType::kExplicit:
            CHECK_EQ(hdr_out.subgroup_id, hdr.subgroup_id);
            return;
        default:
            break;
    }
    FAIL("Bad subgroup id type in StreamHeader");
}

TEST_CASE("StreamHeader Message encode/decode")
{
    const std::vector stream_headers = {
        // subgroup_id_mode = kIsZero
        StreamHeaderProperties(false, SubgroupIdType::kIsZero, false, false),
        StreamHeaderProperties(true, SubgroupIdType::kIsZero, false, false),
        StreamHeaderProperties(false, SubgroupIdType::kIsZero, true, false),
        StreamHeaderProperties(true, SubgroupIdType::kIsZero, true, false),
        // subgroup_id_mode = kSetFromFirstObject
        StreamHeaderProperties(false, SubgroupIdType::kSetFromFirstObject, false, false),
        StreamHeaderProperties(true, SubgroupIdType::kSetFromFirstObject, false, false),
        StreamHeaderProperties(false, SubgroupIdType::kSetFromFirstObject, true, false),
        StreamHeaderProperties(true, SubgroupIdType::kSetFromFirstObject, true, false),
        // subgroup_id_mode = kExplicit
        StreamHeaderProperties(false, SubgroupIdType::kExplicit, false, false),
        StreamHeaderProperties(true, SubgroupIdType::kExplicit, false, false),
        StreamHeaderProperties(false, SubgroupIdType::kExplicit, true, false),
        StreamHeaderProperties(true, SubgroupIdType::kExplicit, true, false),
    };
    for (const auto& props : stream_headers) {
        CAPTURE(props.GetType());
        StreamHeaderEncodeDecode(props);
    }
}

static void
StreamPerSubGroupObjectEncodeDecode(StreamHeaderProperties properties, ExtensionTest extensions, bool empty_payload)
{
    Bytes buffer;
    auto hdr_grp = messages::StreamHeaderSubGroup{};
    hdr_grp.properties.emplace(properties);
    hdr_grp.track_alias = uint64_t(kTrackAliasAliceVideo);
    hdr_grp.group_id = 0x1000;
    if (properties.subgroup_id_mode == SubgroupIdType::kExplicit) {
        hdr_grp.subgroup_id = 0x5000;
    }
    hdr_grp.priority = 0xA;

    buffer << hdr_grp;

    messages::StreamHeaderSubGroup hdr_group_out;
    StreamBuffer<uint8_t> hdr_sbuf;
    hdr_sbuf.Push(buffer);
    bool hdr_parsed = hdr_sbuf >> hdr_group_out;
    CHECK(hdr_parsed);

    CHECK(hdr_grp.properties.has_value());
    CHECK(hdr_group_out.properties.has_value());
    CHECK_EQ(hdr_grp.properties->GetType(), hdr_group_out.properties->GetType());
    CHECK_EQ(hdr_grp.track_alias, hdr_group_out.track_alias);
    CHECK_EQ(hdr_grp.group_id, hdr_group_out.group_id);
    switch (properties.subgroup_id_mode) {
        case SubgroupIdType::kIsZero:
            CHECK_EQ(hdr_group_out.subgroup_id, 0);
            break;
        case SubgroupIdType::kSetFromFirstObject:
            CHECK_EQ(hdr_group_out.subgroup_id, std::nullopt);
            break;
        case SubgroupIdType::kExplicit:
            CHECK_EQ(hdr_group_out.subgroup_id, hdr_grp.subgroup_id);
            break;
        default:
            FAIL("Bad subgroup mode");
            break;
    }

    // stream all the objects
    buffer.clear();
    auto objects = std::vector<messages::StreamSubGroupObject>{};
    // send 10 objects
    for (size_t i = 0; i < 10; i++) {
        auto obj = messages::StreamSubGroupObject{};
        obj.properties.emplace(properties);
        obj.object_delta = 0x1234;

        if (empty_payload) {
            obj.object_status = ObjectStatus::kDoesNotExist;
        } else {
            obj.payload = { 0x1, 0x2, 0x3, 0x4, 0x5 };
        }

        // Only set extensions if the header type allows it.
        if (properties.extensions) {
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

    StreamSubGroupObject obj_out;
    obj_out.properties.emplace(properties);
    size_t object_count = 0;
    StreamBuffer<uint8_t> in_buffer;
    for (size_t i = 0; i < buffer.size(); i++) {
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

        if (properties.extensions) {
            CompareExtensions(objects[object_count].extensions,
                              obj_out.extensions,
                              extensions == ExtensionTest::kBoth || extensions == ExtensionTest::kImmutable);
            CHECK_EQ(obj_out.immutable_extensions, objects[object_count].immutable_extensions);
        } else {
            CHECK_EQ(obj_out.extensions, std::nullopt);
            CHECK_EQ(obj_out.immutable_extensions, std::nullopt);
        }
        // got one object
        object_count++;
        std::construct_at(&obj_out);
        obj_out.properties.emplace(properties);
        in_buffer.Pop(in_buffer.Size());
    }

    CHECK_EQ(object_count, objects.size());
}

TEST_CASE("StreamPerSubGroup Object Message encode/decode")
{
    // Test all valid combinations of StreamHeaderProperties:
    // - extensions: true/false
    // - subgroup_id_mode: kIsZero, kSetFromFirstObject, kExplicit (not kReserved)
    // - end_of_group: true/false
    // - default_priority: false (for now, matching original tests)
    const std::vector stream_headers = {
        // subgroup_id_mode = kIsZero
        StreamHeaderProperties(false, SubgroupIdType::kIsZero, false, false),
        StreamHeaderProperties(true, SubgroupIdType::kIsZero, false, false),
        StreamHeaderProperties(false, SubgroupIdType::kIsZero, true, false),
        StreamHeaderProperties(true, SubgroupIdType::kIsZero, true, false),
        // subgroup_id_mode = kSetFromFirstObject
        StreamHeaderProperties(false, SubgroupIdType::kSetFromFirstObject, false, false),
        StreamHeaderProperties(true, SubgroupIdType::kSetFromFirstObject, false, false),
        StreamHeaderProperties(false, SubgroupIdType::kSetFromFirstObject, true, false),
        StreamHeaderProperties(true, SubgroupIdType::kSetFromFirstObject, true, false),
        // subgroup_id_mode = kExplicit
        StreamHeaderProperties(false, SubgroupIdType::kExplicit, false, false),
        StreamHeaderProperties(true, SubgroupIdType::kExplicit, false, false),
        StreamHeaderProperties(false, SubgroupIdType::kExplicit, true, false),
        StreamHeaderProperties(true, SubgroupIdType::kExplicit, true, false),
    };
    for (const auto& props : stream_headers) {
        CAPTURE(props.GetType());
        for (const auto ext :
             { ExtensionTest::kNone, ExtensionTest::kMutable, ExtensionTest::kImmutable, ExtensionTest::kBoth }) {
            CAPTURE(ext);
            for (bool empty_payload : { true, false }) {
                CAPTURE(empty_payload);
                StreamPerSubGroupObjectEncodeDecode(props, ext, empty_payload);
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
        { static_cast<std::uint64_t>(ExtensionType::kImmutable),
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
    CHECK_EQ(buffer[idx++], static_cast<uint8_t>(ExtensionType::kImmutable));

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
