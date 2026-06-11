// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/message.h"
#include "quicr/detail/messages.h"
#include "quicr/detail/parameters.h"

#include <any>
#include <doctest/doctest.h>
#include <functional>
#include <limits>
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
const Bytes kExampleBytes = {
    0x1, 0x2, 0x3, 0x4, 0x5,
};
const Bytes kUint1ByteValue = { 0x25 };
const Bytes kUint2ByteValue = { 0xBD, 0x3B };
const Bytes kUint4ByteValue = { 0x7D, 0x3E, 0x7F, 0x1D };
const Bytes kUint8ByteValue = { 0x8C, 0xE8, 0x14, 0xFF, 0x5E, 0x7C, 0x19, 0x02 };

// Note: Parameters must be in sorted order by type for delta encoding.
// ParameterType::kAuthorizationToken = 0x03
const auto kExampleParameters = Parameters{}
                                  .Add(static_cast<ParameterType>(2), kUint1ByteValue)
                                  .Add(static_cast<ParameterType>(3), kExampleBytes)
                                  .Add(static_cast<ParameterType>(4), kUint2ByteValue)
                                  .Add(static_cast<ParameterType>(6), kUint4ByteValue)
                                  .Add(static_cast<ParameterType>(8), kUint8ByteValue);

template<typename T>
bool
Verify(std::vector<uint8_t>& buffer, uint64_t message_type, T& message, [[maybe_unused]] size_t slice_depth = 1)
{
    // TODO: support Size_depth > 1, if needed
    StreamBuffer<uint8_t> in_buffer;
    in_buffer.InitAny<T>(); // Set parsed data to be of this type using out param

    std::optional<uint64_t> msg_type;
    bool done = false;

    for (auto& v : buffer) {
        auto& msg = in_buffer.GetAny<T>();
        in_buffer.Push(v);

        if (!msg_type) {
            msg_type = in_buffer.DecodeUintV();
            if (!msg_type) {
                continue;
            }
            CHECK_EQ(*msg_type, message_type);
            continue;
        }

        done = in_buffer >> msg;
        if (done) {
            // copy the working parsed data to out param.
            message = msg;
            break;
        }
    }

    return done;
}

TEST_CASE("GroupOrder encode rejects invalid values")
{
    Bytes buffer;

    CHECK_THROWS_AS(buffer << static_cast<GroupOrder>(0), ProtocolViolationException);
    CHECK_THROWS_AS(buffer << static_cast<GroupOrder>(3), ProtocolViolationException);
}

using TestKVP64 = KeyValuePair<std::uint64_t>;
enum class ExampleEnum : std::uint64_t
{
    kOdd = 1,
    kEven = 2,
};
using TestKVPEnum = KeyValuePair<ExampleEnum>;

TEST_CASE("Key Value Pair encode/decode")
{
    auto value = Bytes(sizeof(std::uint64_t));
    constexpr std::uint64_t one = 1;
    std::memcpy(value.data(), &one, value.size());
    {
        CAPTURE("UINT64_T");
        {
            CAPTURE("EVEN");
            std::uint64_t type = 2;
            TestKVP64 kvp{ type, value };
            Bytes serialized;
            SerializeKvp(serialized, kvp, {});
            CHECK_EQ(serialized.size(), 2); // Minimal size, 1 byte for type and 1 byte for value.

            TestKVP64 out;
            BytesSpan span = serialized;
            ParseKvp(span, out, {});
            CHECK_EQ(out.type, type);
            std::uint64_t reconstructed_value = 0;
            std::memcpy(&reconstructed_value, out.value.data(), out.value.size());
            CHECK_EQ(reconstructed_value, one);
        }
        {
            CAPTURE("ODD");
            std::uint64_t type = 1;
            TestKVP64 kvp{ type, value };
            Bytes serialized;
            SerializeKvp(serialized, kvp, {});
            CHECK_EQ(serialized.size(),
                     value.size() + 1 + 1); // 1 byte for type, 1 byte for length, and the value bytes.

            TestKVP64 out;
            BytesSpan span = serialized;
            ParseKvp(span, out, {});
            CHECK_EQ(out.type, type);
            CHECK_EQ(out.value, value);
        }
    }
    {
        CAPTURE("ENUM");
        {
            CAPTURE("EVEN");
            auto type = ExampleEnum::kEven;
            TestKVPEnum kvp{ type, value };
            Bytes serialized;
            SerializeKvp(serialized, kvp, type);
            CHECK_EQ(serialized.size(), 2); // Minimal size, 1 byte for type and 1 byte for value.

            TestKVPEnum out;
            BytesSpan span = serialized;
            ParseKvp(span, out, type);
            CHECK_EQ(out.type, type);
            std::uint64_t reconstructed_value = 0;
            std::memcpy(&reconstructed_value, out.value.data(), out.value.size());
            CHECK_EQ(reconstructed_value, one);
        }
        {
            CAPTURE("ODD");
            auto type = ExampleEnum::kOdd;
            TestKVPEnum kvp{ type, value };
            Bytes serialized;
            SerializeKvp(serialized, kvp, type);
            CHECK_EQ(serialized.size(),
                     value.size() + 1 + 1); // 1 byte for type, 1 byte for length, and the value bytes.

            TestKVPEnum out;
            BytesSpan span = serialized;
            ParseKvp(span, out, type);
            CHECK_EQ(out.type, type);
            CHECK_EQ(out.value, value);
        }
    }
}

TEST_CASE("UInt16 Encode/decode")
{
    std::uint16_t value = 65535;
    Bytes buffer;
    buffer << value;
    std::uint16_t reconstructed_value = 0;
    buffer >> reconstructed_value;
    CHECK_EQ(reconstructed_value, value);
}

TEST_CASE("Location Equality / Comparison")
{
    // Test equality
    Location loc1{ 1, 2 };
    Location loc2{ 1, 2 };
    Location loc3{ 1, 3 };
    Location loc4{ 2, 1 };

    // Test equality operator
    CHECK(loc1 == loc2);
    CHECK_FALSE(loc1 == loc3);
    CHECK_FALSE(loc1 == loc4);

    // Test inequality operator
    CHECK_FALSE(loc1 != loc2);
    CHECK(loc1 != loc3);
    CHECK(loc1 != loc4);

    // Test less than operator
    // Same group, different objects
    CHECK(loc1 < loc3);       // {1,2} < {1,3}
    CHECK_FALSE(loc3 < loc1); // {1,3} not < {1,2}

    // Different groups
    CHECK(loc1 < loc4);       // {1,2} < {2,1}
    CHECK_FALSE(loc4 < loc1); // {2,1} not < {1,2}

    // Test greater than operator
    CHECK(loc3 > loc1);       // {1,3} > {1,2}
    CHECK_FALSE(loc1 > loc3); // {1,2} not > {1,3}

    CHECK(loc4 > loc1);       // {2,1} > {1,2}
    CHECK_FALSE(loc1 > loc4); // {1,2} not > {2,1}

    // Test less than or equal
    CHECK(loc1 <= loc2);       // Equal case
    CHECK(loc1 <= loc3);       // Less than case
    CHECK_FALSE(loc3 <= loc1); // Greater than case

    // Test greater than or equal
    CHECK(loc1 >= loc2);       // Equal case
    CHECK(loc3 >= loc1);       // Greater than case
    CHECK_FALSE(loc1 >= loc3); // Less than case

    // Test edge cases with zero values
    Location loc_zero{ 0, 0 };
    Location loc_group_zero{ 0, 1 };
    Location loc_object_zero{ 1, 0 };

    CHECK(loc_zero < loc_group_zero);        // {0,0} < {0,1}
    CHECK(loc_zero < loc_object_zero);       // {0,0} < {1,0}
    CHECK(loc_group_zero < loc_object_zero); // {0,1} < {1,0}

    // Test comparison with large values
    Location loc_large1{ UINT64_MAX, UINT64_MAX };
    Location loc_large2{ UINT64_MAX, UINT64_MAX - 1 };

    CHECK(loc_large2 < loc_large1);
    CHECK(loc_large1 > loc_large2);
    CHECK_FALSE(loc_large1 == loc_large2);
}

TEST_CASE("Parameters encode/decode")
{
    const auto params = kExampleParameters;
    Bytes buffer;
    buffer << params;
    Parameters out;
    buffer >> out;
    CHECK_EQ(out, params);
}

TEST_CASE("Parameters wire encoding")
{
    SUBCASE("raw uint8")
    {
        auto params = Parameters{}.Add(ParameterType::kForward, std::uint8_t{ 1 });
        Bytes buffer;
        buffer << params;
        CHECK_EQ(buffer,
                 Bytes{ 0x01,    // Parameter count of 1.
                        0x10,    // Type of 10.
                        0x01 }); // 1 byte value of 1.

        Parameters out;
        buffer >> out;
        CHECK_EQ(out.Get<std::uint8_t>(ParameterType::kForward), 1);
    }

    SUBCASE("uint8 enum")
    {
        auto params = Parameters{}.Add(ParameterType::kGroupOrder, GroupOrder::kAscending);
        Bytes buffer;
        buffer << params;
        CHECK_EQ(buffer,
                 Bytes{ 0x01,    // Parameter count of 1.
                        0x22,    // Type of 22.
                        0x01 }); // 1 byte value of 1.

        Parameters out;
        buffer >> out;
        CHECK_EQ(out.Get<std::uint8_t>(ParameterType::kGroupOrder), 1);
    }

    SUBCASE("varint")
    {
        auto params = Parameters{}.Add(ParameterType::kDeliveryTimeout, std::uint64_t{ 15293 });
        Bytes buffer;
        buffer << params;
        CHECK_EQ(buffer,
                 Bytes{ 0x01, // Parameter count of 1.
                        0x02, // Parameter type of 2.
                        0xbb, // Varint bytes of 15293.
                        0xbd });

        Parameters out;
        buffer >> out;
        CHECK_EQ(out.Get<std::uint64_t>(ParameterType::kDeliveryTimeout), 15293);
    }

    SUBCASE("Location")
    {
        constexpr std::uint64_t group = 226442877;
        constexpr std::uint64_t object = 2893212287960;
        auto params = Parameters{}.Add(ParameterType::kLargestObject, Location{ group, object });
        Bytes buffer;
        buffer << params;
        CHECK_EQ(buffer,
                 Bytes{ 0x01, // Parameter count varint of 1.
                        0x09, // Parameter type varint of 9.
                        0xed, // Bytes of the 2 varints, { group, object }.
                        0x7f,
                        0x3e,
                        0x7d,
                        0xfa,
                        0xa1,
                        0xa0,
                        0xe4,
                        0x03,
                        0xd8 });

        Parameters out;
        buffer >> out;
        CHECK_EQ(out.Get<Location>(ParameterType::kLargestObject), Location{ group, object });
    }

    SUBCASE("length-prefixed bytes")
    {
        const Bytes value{ 0xAA, 0xBB, 0xCC };
        auto params = Parameters{}.Add(ParameterType::kAuthorizationToken, value);
        Bytes buffer;
        buffer << params;
        CHECK_EQ(buffer,
                 Bytes{ 0x01,    // Parameter count varint of 1.
                        0x03,    // Parameter type varint of 3.
                        0x03,    // Byte length prefix of 3.
                        0xAA,    // Byte 1.
                        0xBB,    // Byte 2.
                        0xCC }); // Byte 3.

        Parameters out;
        buffer >> out;
        CHECK_EQ(out.Get<Bytes>(ParameterType::kAuthorizationToken), value);
    }

    SUBCASE("multiple length-prefixed bytes")
    {
        const Bytes auth{ 0xAA, 0xBB, 0xCC };
        const Bytes prefix{ 0xDD, 0xEE };
        auto params = Parameters{}
                        .Add(ParameterType::kAuthorizationToken, auth)      // type 0x03
                        .Add(ParameterType::kTrackNamespacePrefix, prefix); // type 0x34
        Bytes buffer;
        buffer << params;
        CHECK_EQ(buffer,
                 Bytes{ 0x02,    // Parameter count varint of 2.
                        0x03,    // Type delta to 0x03 (first parameter).
                        0x03,    // Length prefix of 3.
                        0xAA,    // Auth byte 1.
                        0xBB,    // Auth byte 2.
                        0xCC,    // Auth byte 3.
                        0x31,    // Type delta from 0x03 to 0x34 = 0x31.
                        0x02,    // Length prefix of 2.
                        0xDD,    // Prefix byte 1.
                        0xEE }); // Prefix byte 2.

        Parameters out;
        buffer >> out;
        CHECK_EQ(out.Get<Bytes>(ParameterType::kAuthorizationToken), auth);
        CHECK_EQ(out.Get<Bytes>(ParameterType::kTrackNamespacePrefix), prefix);
    }
}

// Param byte encoding static checks.
namespace {
    enum struct ByteBackedEnum : std::uint8_t
    {
    };
    enum struct WideBackedEnum : std::uint64_t
    {
    };
    // Bytes and uint8_t enums are allowed.
    static_assert(ByteParameter<std::uint8_t>);
    static_assert(ByteParameter<ByteBackedEnum>);
    // uint64_t enum not allowed.
    static_assert(!ByteParameter<WideBackedEnum>);
}

TEST_CASE("KVP Value Equality")
{
    SUBCASE("Even type - varint compression")
    {
        KeyValuePair<std::uint64_t> kvp;
        kvp.type = 2;             // Even type
        kvp.value = { 0x1, 0x0 }; // Will be compressed to {0x1}
        Bytes buffer;
        SerializeKvp(buffer, kvp, {});
        KeyValuePair<std::uint64_t> out;
        BytesSpan span = buffer;
        ParseKvp(span, out, {});
        CHECK_EQ(out, kvp);
    }

    SUBCASE("Even type - direct comparison")
    {
        KeyValuePair<std::uint64_t> kvp1, kvp2;
        kvp1.type = kvp2.type = 2; // Even type
        kvp1.value = { 0x1, 0x0, 0x0 };
        kvp2.value = { 0x1 };
        CHECK_EQ(kvp1, kvp2); // Should be equal (same numeric value)
    }

    SUBCASE("Even type - different values")
    {
        KeyValuePair<std::uint64_t> kvp1, kvp2;
        kvp1.type = kvp2.type = 2; // Even type
        kvp1.value = { 0x1 };
        kvp2.value = { 0x2 };
        CHECK_FALSE(kvp1 == kvp2); // Should be different
    }

    SUBCASE("Even type - non-zero padding")
    {
        KeyValuePair<std::uint64_t> kvp1, kvp2;
        kvp1.type = kvp2.type = 2; // Even type
        kvp1.value = { 0x1 };
        kvp2.value = { 0x1, 0x1 }; // Non-zero padding
        CHECK_FALSE(kvp1 == kvp2); // Should be different
    }

    SUBCASE("Odd type - byte equality")
    {
        KeyValuePair<std::uint64_t> kvp1, kvp2;
        kvp1.type = kvp2.type = 1; // Odd type
        kvp1.value = { 0x1, 0x0 };
        kvp2.value = { 0x1, 0x0 };
        CHECK_EQ(kvp1, kvp2); // Should be equal (exact byte match)
    }

    SUBCASE("Odd type - different bytes")
    {
        KeyValuePair<std::uint64_t> kvp1, kvp2;
        kvp1.type = kvp2.type = 1; // Odd type
        kvp1.value = { 0x1, 0x0 };
        kvp2.value = { 0x1 };      // Different size
        CHECK_FALSE(kvp1 == kvp2); // Should be different (exact byte comparison)
    }
}

template<typename T>
concept Numeric = requires { std::numeric_limits<T>::is_specialized; };
template<Numeric T>
void
IntegerEncodeDecode(bool exhaustive)
{
    using Limits = std::numeric_limits<T>;
    if (exhaustive) {
        static_assert(sizeof(size_t) > sizeof(T));
        for (std::size_t value = Limits::min(); value <= Limits::max(); ++value) {
            Bytes buffer;
            buffer << static_cast<T>(value);
            T out;
            buffer >> out;
            REQUIRE_EQ(out, value);
        }
    } else {
        const std::array<T, 3> values = { Limits::min(), Limits::max(), Limits::max() / static_cast<T>(2) };
        for (const auto value : values) {
            Bytes buffer;
            buffer << value;
            T out;
            buffer >> out;
            CHECK_EQ(out, value);
        }
    }

    // A buffer that's not big enough should throw.
    for (std::size_t size = 0; size < sizeof(T); ++size) {
        const auto buffer = Bytes(size);
        T out;
        CHECK_THROWS(buffer >> out);
    }

    // A buffer that's too big is fine.
    auto buffer = Bytes(sizeof(T) + 1);
    memset(buffer.data(), 0xFF, buffer.size());
    T out;
    buffer >> out;
    CHECK_EQ(out, Limits::max());
    memset(buffer.data(), 0, sizeof(T));
    buffer >> out;
    CHECK_EQ(out, 0);
}

TEST_CASE("uint8_t encode/decode")
{
    IntegerEncodeDecode<std::uint8_t>(true);
}

TEST_CASE("uint16_t encode/decode")
{
    IntegerEncodeDecode<std::uint16_t>(true);
}

TEST_CASE("KeyValuePair even-type round-trip preserves values")
{
    const std::vector<std::uint64_t> test_values = {
        0,       1,
        127, // Max 1-byte varint
        128, // Min 2-byte varint
        255,
        16383,   // Max 2-byte varint
        16384,   // Min 3-byte varint
        2097151, // Max 3-byte varint
        2097152, // Min 4-byte varint
        100000,  std::numeric_limits<std::uint64_t>::max(),
    };

    for (const auto value : test_values) {
        CAPTURE(value);

        Parameters params;
        params.Add(ParameterType::kDeliveryTimeout, value);

        Bytes buffer;
        buffer << params;

        // Should have encoded as uintvar.
        UintVar expected(value);
        Bytes expected_bytes{ expected.begin(), expected.end() };
        REQUIRE(buffer.size() >= expected_bytes.size());
        Bytes tail(buffer.end() - expected_bytes.size(), buffer.end());
        CHECK_EQ(tail, expected_bytes);

        Parameters out;
        BytesSpan span{ buffer };
        span >> out;

        // Roundtrip.
        CHECK_NOTHROW(out.Get<std::uint64_t>(ParameterType::kDeliveryTimeout));
        CHECK_EQ(out.Get<std::uint64_t>(ParameterType::kDeliveryTimeout), value);
    }
}

TEST_CASE("TrackExtensions even-type round-trip preserves values")
{
    const std::vector<std::uint64_t> test_values = {
        0,       1,
        127, // Max 1-byte varint
        128, // Min 2-byte varint
        255,
        16383,   // Max 2-byte varint
        16384,   // Min 3-byte varint
        2097151, // Max 3-byte varint
        2097152, // Min 4-byte varint
        100000,  std::numeric_limits<std::uint64_t>::max(),
    };

    for (const auto value : test_values) {
        CAPTURE(value);

        TrackExtensions ext;
        ext.Add(ExtensionType::kDeliveryTimeout, value);

        Bytes buffer;
        buffer << ext;

        // Should have been encoded as uintvar.
        UintVar expected(value);
        Bytes expected_bytes{ expected.begin(), expected.end() };
        REQUIRE(buffer.size() >= expected_bytes.size());
        Bytes tail(buffer.end() - expected_bytes.size(), buffer.end());
        CHECK_EQ(tail, expected_bytes);

        TrackExtensions out;
        BytesSpan span{ buffer };
        span >> out;

        // Roundtrip.
        CHECK_NOTHROW(out.Get<std::uint64_t>(ExtensionType::kDeliveryTimeout));
        CHECK_EQ(out.Get<std::uint64_t>(ExtensionType::kDeliveryTimeout), value);
    }
}

TEST_CASE("TrackExtensions GetOptional returns value when present and nullopt when absent")
{
    TrackExtensions ext;
    ext.Add(ExtensionType::kDeliveryTimeout, std::uint64_t{ 42 });
    ext.AddImmutable(ExtensionType::kDefaultPublisherGroupOrder, GroupOrder::kAscending);

    // Present keys return the value.
    auto result = ext.GetOptional<std::uint64_t>(ExtensionType::kDeliveryTimeout);
    REQUIRE(result.has_value());
    CHECK_EQ(result.value(), 42);

    auto immutable_result = ext.GetImmutableOptional<GroupOrder>(ExtensionType::kDefaultPublisherGroupOrder);
    REQUIRE(immutable_result.has_value());
    CHECK_EQ(immutable_result.value(), GroupOrder::kAscending);

    ext.AddImmutable(ExtensionType::kDynamicGroups, true);
    auto bool_result = ext.GetImmutableOptional<bool>(ExtensionType::kDynamicGroups);
    REQUIRE(bool_result.has_value());
    CHECK(bool_result.value());

    // Absent keys return nullopt.
    CHECK_FALSE(ext.GetOptional<std::uint64_t>(ExtensionType::kMaxCacheDuration).has_value());
    CHECK_FALSE(ext.GetImmutableOptional<bool>(ExtensionType::kDefaultPublisherPriority).has_value());
}

TEST_CASE("Parameters")
{
    Parameters params;

    params.Add(ParameterType::kDeliveryTimeout, std::uint64_t(5000));
    CHECK(params.Contains(ParameterType::kDeliveryTimeout));

    std::optional<Location> location;
    params.AddOptional(ParameterType::kLargestObject, location);
    CHECK_FALSE(params.Contains(ParameterType::kLargestObject));

    location = { 1, 2 };
    params.AddOptional(ParameterType::kLargestObject, location);
    CHECK(params.Contains(ParameterType::kLargestObject));

    CHECK_EQ(params.Get<std::uint64_t>(ParameterType::kDeliveryTimeout), std::uint64_t(5000));
}

TEST_CASE("ResolveSubscriberPriority")
{
    CHECK_EQ(ResolveSubscriberPriority(Parameters{}), std::uint8_t(128));

    auto params = Parameters{}.Add(ParameterType::kSubscriberPriority, std::uint8_t(7));
    CHECK_EQ(ResolveSubscriberPriority(params), std::uint8_t(7));
}

TEST_CASE("ResolveRendezvousTimeout")
{
    CHECK_FALSE(ResolveRendezvousTimeout(Parameters{}).has_value());

    auto params = Parameters{}.Add(ParameterType::kRendezvousTimeout, std::uint64_t(3000));
    CHECK_EQ(ResolveRendezvousTimeout(params).value(), std::uint64_t(3000));
}

TEST_CASE("SUBSCRIBE parameter validation rejects stray parameters")
{
    const std::initializer_list<ParameterType> allowed = { ParameterType::kAuthorizationToken,
                                                           ParameterType::kDeliveryTimeout,
                                                           ParameterType::kSubgroupDeliveryTimeout,
                                                           ParameterType::kRendezvousTimeout,
                                                           ParameterType::kSubscriberPriority,
                                                           ParameterType::kGroupOrder,
                                                           ParameterType::kForward,
                                                           ParameterType::kNewGroupRequest };

    auto ok = Parameters{}.Add(ParameterType::kSubscriberPriority, std::uint8_t(5));
    CHECK_NOTHROW(ValidateParameters(ok, allowed));

    // EXPIRES is valid in SUBSCRIBE_OK, not SUBSCRIBE.
    auto stray = Parameters{}.Add(ParameterType::kExpires, std::uint64_t(1000));
    CHECK_THROWS_AS(ValidateParameters(stray, allowed), ProtocolViolationException);
}

TEST_CASE("Parameters - typed encodings round-trip")
{
    SUBCASE("single byte encoding")
    {
        for (const std::uint8_t priority : { static_cast<std::uint8_t>(0), static_cast<std::uint8_t>(255) }) {
            CAPTURE(priority);
            Parameters params;
            params.Add(ParameterType::kSubscriberPriority, priority);

            Bytes buffer;
            buffer << params;

            CHECK_EQ(buffer.size(), 3);
            CHECK_EQ(buffer.back(), priority);
            Parameters out;
            BytesSpan{ buffer } >> out;
            CHECK_EQ(out.Get<std::uint8_t>(ParameterType::kSubscriberPriority), priority);
        }
    }

    SUBCASE("Location encodes as two varints")
    {
        constexpr Location location{ 300, 5 };
        Parameters params;
        params.Add(ParameterType::kLargestObject, location);

        Bytes buffer;
        buffer << params;

        Parameters out;
        BytesSpan{ buffer } >> out;
        CHECK_EQ(out.Get<Location>(ParameterType::kLargestObject), location);
    }

    SUBCASE("Track Namespace prefix encodes as length-prefixed bytes")
    {
        const TrackNamespace prefix{ Bytes{ 'a', 'b' }, Bytes{ 'c' } };
        Parameters params;
        params.Add(ParameterType::kTrackNamespacePrefix, prefix);

        Bytes buffer;
        buffer << params;

        Parameters out;
        BytesSpan{ buffer } >> out;
        CHECK_EQ(out.Get<TrackNamespace>(ParameterType::kTrackNamespacePrefix), prefix);
    }
}

TEST_CASE("Parameters - unknown type is a protocol violation")
{
    Bytes buffer;
    buffer << UintVar(std::uint64_t{ 1 });    // One parameter.
    buffer << UintVar(std::uint64_t{ 0x7F }); // Type delta to an unknown type.
    buffer << UintVar(std::uint64_t{ 0 });    // Some value.

    Parameters out;
    CHECK_THROWS_AS(BytesSpan{ buffer } >> out, ProtocolViolationException);
}

TEST_CASE("Filters")
{
    static_assert(HasByteStreamOperators<Filter>);

    const auto serialise_filter = [](FilterType type, const Filter& filter) {
        auto [param_type, bytes] = SerializeFilter(type, filter);
        CHECK_EQ(param_type, ToParameterFilterType(type));
        if (type == FilterType::kNone) {
            CHECK(bytes.empty());
        } else {
            CHECK_FALSE(bytes.empty());
        }

        auto deserialised_filter = DeserializeFilter(type, bytes);
        CHECK_EQ(deserialised_filter, filter);
    };

    Filter filter;

    {
        Bytes bytes{};
        CHECK_NOTHROW(bytes << filter);
        CHECK_THROWS(BytesSpan{} >> filter);
    }

    serialise_filter(FilterType::kNone, filter);

    filter = TrackFilter{
        .property_type = 1,
        .max_tracks_selected = 2,
        .timeout = 3,
    };
    serialise_filter(FilterType::kTrackFilter, filter);

    filter = LocationFilter{
        { .start = 1 },
    };
    serialise_filter(FilterType::kSubscriptionFilter, filter);

    filter = LocationFilter{
        {
          .start = 1,
          .end = 2,
        },
    };
    serialise_filter(FilterType::kSubscriptionFilter, filter);
}

TEST_CASE("Parameters - Filters")
{
    Filter filter = LocationFilter{
        {
          .start = 1,
          .end = 2,
        },
    };

    auto params = Parameters{}.Add(ParameterType::kSubscriptionFilter, filter);

    Bytes bytes;
    bytes << params;

    CHECK_FALSE(bytes.empty());

    Parameters recv_params;
    bytes >> recv_params;

    auto recv_filter = recv_params.GetFilter(FilterType::kSubscriptionFilter);
    CHECK_EQ(recv_filter, filter);
}
