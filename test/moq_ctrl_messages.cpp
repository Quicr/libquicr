// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/control_messages.h"
#include "quicr/detail/message.h"
#include "quicr/detail/messages.h"

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

TEST_CASE("Token round-trips for each alias type")
{
    using namespace quicr::messages;

    SUBCASE("USE_VALUE carries type and value")
    {
        const Token in{ .alias_type = Token::AliasType::kUseValue,
                        .token_alias = std::nullopt,
                        .token_type = 7,
                        .token_value = FromASCII("secret") };
        Bytes buffer;
        buffer << in;
        BytesSpan span{ buffer };
        Token out{};
        span = span >> out;
        CHECK(span.empty());
        CHECK(out == in);
    }

    SUBCASE("REGISTER carries alias, type and value")
    {
        const Token in{ .alias_type = Token::AliasType::kRegister,
                        .token_alias = 3,
                        .token_type = 7,
                        .token_value = FromASCII("secret") };
        Bytes buffer;
        buffer << in;
        BytesSpan span{ buffer };
        Token out{};
        span = span >> out;
        CHECK(span.empty());
        CHECK(out == in);
    }

    SUBCASE("DELETE carries only an alias")
    {
        const Token in{ .alias_type = Token::AliasType::kDelete, .token_alias = 3 };
        Bytes buffer;
        buffer << in;
        BytesSpan span{ buffer };
        Token out{};
        span = span >> out;
        CHECK(span.empty());
        CHECK(out == in);
    }

    SUBCASE("USE_ALIAS carries only an alias")
    {
        const Token in{ .alias_type = Token::AliasType::kUseAlias, .token_alias = 5 };
        Bytes buffer;
        buffer << in;
        BytesSpan span{ buffer };
        Token out{};
        span = span >> out;
        CHECK(span.empty());
        CHECK(out == in);
    }
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
const Parameters kExampleParameters = {
    { static_cast<ParameterType>(2), kUint1ByteValue },
    { ParameterType::kAuthorizationToken, kExampleBytes }, // type 0x03
    { static_cast<ParameterType>(4), kUint2ByteValue },
    { static_cast<ParameterType>(6), kUint4ByteValue },
    { static_cast<ParameterType>(8), kUint8ByteValue },
};

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
    serialise_filter(FilterType::kLocationFilter, filter);

    filter = LocationFilter{
        {
          .start = 1,
          .end = 2,
        },
    };
    serialise_filter(FilterType::kLocationFilter, filter);
}

TEST_CASE("Parameters - Filters")
{
    Filter filter = TrackFilter{
        .property_type = 1,
        .max_tracks_selected = 2,
        .timeout = 3,
    };

    auto params = Parameters{}.Add(ParameterType::kTrackFilter, filter);

    Bytes bytes;
    bytes << params;

    CHECK_FALSE(bytes.empty());

    Parameters recv_params;
    bytes >> recv_params;

    auto recv_filter = recv_params.GetFilter(FilterType::kTrackFilter);
    CHECK_EQ(recv_filter, filter);
}

TEST_CASE("ValidateParameters enforces the allow-list")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    SUBCASE("a parameter outside the allow-list throws")
    {
        const Parameters params{ { ParameterType::kForward, Bytes{ 0x1 } } };
        CHECK_THROWS_AS(ValidateParameters(params, { ParameterType::kExpires }), ProtocolViolationException);
    }

    SUBCASE("an unexpected duplicate throws")
    {
        const Parameters params{ { ParameterType::kForward, Bytes{ 0x1 } }, { ParameterType::kForward, Bytes{ 0x1 } } };
        CHECK_THROWS_AS(ValidateParameters(params, { ParameterType::kForward }), ProtocolViolationException);
    }

    SUBCASE("a repeated AUTHORIZATION_TOKEN is allowed")
    {
        const Parameters params{ { ParameterType::kAuthorizationToken, Bytes{ 0x3, 0x1 } },
                                 { ParameterType::kAuthorizationToken, Bytes{ 0x3, 0x2 } } };
        CHECK_NOTHROW(ValidateParameters(params, { ParameterType::kAuthorizationToken }));
    }
}

TEST_CASE("Subscribe resolves parameters and defaults")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    auto make_payload = [](const Parameters& params) {
        Bytes payload;
        payload << RequestID{ 1 };
        payload << kTrackNamespaceConf;
        payload << kTrackNameAliceVideo;
        payload << params;
        return payload;
    };

    SUBCASE("defaults apply when parameters omitted")
    {
        const auto payload = make_payload(Parameters{});
        const auto msg = Subscribe::Decode(BytesSpan{ payload });
        CHECK(msg.rendezvous_timeout == 0);
        CHECK(msg.subscriber_priority == 128);
        CHECK(msg.forward == true);
        CHECK_FALSE(msg.group_order.has_value());
        CHECK_FALSE(msg.object_delivery_timeout.has_value());
        CHECK_FALSE(msg.new_group_request.has_value());
        CHECK(msg.auth_tokens.empty());
    }

    SUBCASE("present scalar parameters are resolved")
    {
        Parameters params;
        params.Add(ParameterType::kRendezvousTimeout, std::uint64_t{ 50 });
        params.Add(ParameterType::kNewGroupRequest, std::uint64_t{ 9 });
        const auto payload = make_payload(params);
        const auto msg = Subscribe::Decode(BytesSpan{ payload });
        CHECK(msg.rendezvous_timeout == 50);
        REQUIRE(msg.new_group_request.has_value());
        CHECK(msg.new_group_request.value() == 9);
    }

    SUBCASE("a FETCH-only parameter is rejected")
    {
        Parameters params;
        params.Add(ParameterType::kFillTimeout, std::uint64_t{ 10 });
        const auto payload = make_payload(params);
        CHECK_THROWS_AS(Subscribe::Decode(BytesSpan{ payload }), ProtocolViolationException);
    }
}

TEST_CASE("Subscribe round-trips through Encode and Decode")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    auto check_round_trip = [](const Subscribe& sent) {
        const auto bytes = sent.Encode();
        const auto received = Subscribe::Decode(BytesSpan{ bytes });
        CHECK(received.request_id == sent.request_id);
        CHECK(received.track_namespace == sent.track_namespace);
        CHECK(received.track_name == sent.track_name);
        CHECK(received.object_delivery_timeout == sent.object_delivery_timeout);
        CHECK(received.subgroup_delivery_timeout == sent.subgroup_delivery_timeout);
        CHECK(received.rendezvous_timeout == sent.rendezvous_timeout);
        CHECK(received.subscriber_priority == sent.subscriber_priority);
        CHECK(received.group_order == sent.group_order);
        CHECK(received.subscription_filter == sent.subscription_filter);
        CHECK(received.forward == sent.forward);
        CHECK(received.new_group_request == sent.new_group_request);
    };

    SUBCASE("all-default fields survive omit-on-default")
    {
        check_round_trip(Subscribe{
          .request_id = RequestID{ 7 },
          .track_namespace = kTrackNamespaceConf,
          .track_name = kTrackNameAliceVideo,
          .rendezvous_timeout = 0,
          .subscriber_priority = 128,
          .subscription_filter = std::monostate{},
          .forward = true,
        });
    }

    SUBCASE("non-default fields are carried explicitly")
    {
        check_round_trip(Subscribe{
          .request_id = RequestID{ 7 },
          .track_namespace = kTrackNamespaceConf,
          .track_name = kTrackNameAliceVideo,
          .object_delivery_timeout = std::uint64_t{ 100 },
          .subgroup_delivery_timeout = std::uint64_t{ 200 },
          .rendezvous_timeout = 50,
          .subscriber_priority = 64,
          .group_order = GroupOrder::kDescending,
          .subscription_filter = TrackFilter{ .property_type = 1, .max_tracks_selected = 2, .timeout = 3 },
          .forward = false,
          .new_group_request = std::uint64_t{ 9 },
        });
    }
}

TEST_CASE("SubscribeTracks resolves FORWARD and auth tokens")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    auto make_payload = [](const Parameters& params) {
        Bytes payload;
        payload << RequestID{ 1 };
        payload << kTrackNamespaceConf;
        payload << params;
        return payload;
    };

    SUBCASE("FORWARD defaults to true")
    {
        const auto payload = make_payload(Parameters{});
        const auto msg = SubscribeTracks::Decode(BytesSpan{ payload });
        CHECK(msg.forward == true);
        CHECK(msg.auth_tokens.empty());
    }

    SUBCASE("a non-token, non-forward parameter is rejected")
    {
        Parameters params;
        params.Add(ParameterType::kExpires, std::uint64_t{ 1 });
        const auto payload = make_payload(params);
        CHECK_THROWS_AS(SubscribeTracks::Decode(BytesSpan{ payload }), ProtocolViolationException);
    }
}

TEST_CASE("Publish resolves parameters and defaults")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    auto make_payload = [](const Parameters& params) {
        Bytes payload;
        payload << RequestID{ 1 };
        payload << kTrackNamespaceConf;
        payload << kTrackNameAliceVideo;
        payload << TrackAlias{ 0xA11CE };
        payload << params;
        payload << TrackExtensions{}; // track_properties, empty
        return payload;
    };

    SUBCASE("defaults apply when omitted")
    {
        const auto payload = make_payload(Parameters{});
        const auto msg = Publish::Decode(BytesSpan{ payload });
        CHECK(msg.forward == true);
        CHECK_FALSE(msg.expires.has_value());
        CHECK_FALSE(msg.largest_object.has_value());
        CHECK(msg.auth_tokens.empty());
    }

    SUBCASE("EXPIRES of 0 resolves to no expiry")
    {
        Parameters params;
        params.Add(ParameterType::kExpires, std::uint64_t{ 0 });
        const auto payload = make_payload(params);
        const auto msg = Publish::Decode(BytesSpan{ payload });
        CHECK_FALSE(msg.expires.has_value());
    }

    SUBCASE("non-zero EXPIRES is resolved")
    {
        Parameters params;
        params.Add(ParameterType::kExpires, std::uint64_t{ 5000 });
        const auto payload = make_payload(params);
        const auto msg = Publish::Decode(BytesSpan{ payload });
        REQUIRE(msg.expires.has_value());
        CHECK(msg.expires.value() == 5000);
    }

    SUBCASE("LARGEST_OBJECT is resolved")
    {
        Parameters params;
        Location loc{ .group = 10, .object = 20 };
        params.Add(ParameterType::kLargestObject, loc);
        const auto payload = make_payload(params);
        const auto msg = Publish::Decode(BytesSpan{ payload });
        REQUIRE(msg.largest_object.has_value());
        CHECK(msg.largest_object.value().group == 10);
        CHECK(msg.largest_object.value().object == 20);
    }

    SUBCASE("FORWARD is resolved")
    {
        Parameters params;
        params.Add(ParameterType::kForward, std::uint8_t{ 0 });
        const auto payload = make_payload(params);
        const auto msg = Publish::Decode(BytesSpan{ payload });
        CHECK(msg.forward == false);
    }

    SUBCASE("a SUBSCRIBE-only parameter is rejected")
    {
        Parameters params;
        params.Add(ParameterType::kRendezvousTimeout, std::uint64_t{ 10 });
        const auto payload = make_payload(params);
        CHECK_THROWS_AS(Publish::Decode(BytesSpan{ payload }), ProtocolViolationException);
    }
}

TEST_CASE("StandaloneFetch resolves parameters and defaults")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    auto make_payload = [](const Parameters& params) {
        Bytes payload;
        payload << RequestID{ 1 };
        payload << FetchType::kStandalone;
        payload << kTrackNamespaceConf;
        payload << kTrackNameAliceVideo;
        payload << Location{ 0, 0 };
        payload << Location{ 1, 0 };
        payload << params;
        return payload;
    };

    SUBCASE("defaults apply when omitted")
    {
        const auto payload = make_payload(Parameters{});
        const auto msg = StandaloneFetch::Decode(BytesSpan{ payload });
        CHECK(msg.subscriber_priority == 128);
        CHECK(msg.group_order == GroupOrder::kAscending);
        CHECK_FALSE(msg.fill_timeout.has_value());
        CHECK(msg.auth_tokens.empty());
    }

    SUBCASE("FILL_TIMEOUT is resolved")
    {
        Parameters params;
        params.Add(ParameterType::kFillTimeout, std::uint64_t{ 250 });
        const auto payload = make_payload(params);
        const auto msg = StandaloneFetch::Decode(BytesSpan{ payload });
        REQUIRE(msg.fill_timeout.has_value());
        CHECK(msg.fill_timeout.value() == 250);
    }

    SUBCASE("SUBSCRIBER_PRIORITY is resolved")
    {
        Parameters params;
        params.Add(ParameterType::kSubscriberPriority, std::uint8_t{ 64 });
        const auto payload = make_payload(params);
        const auto msg = StandaloneFetch::Decode(BytesSpan{ payload });
        CHECK(msg.subscriber_priority == 64);
    }

    SUBCASE("GROUP_ORDER is resolved")
    {
        Parameters params;
        params.Add(ParameterType::kGroupOrder, std::uint8_t{ static_cast<std::uint8_t>(GroupOrder::kDescending) });
        const auto payload = make_payload(params);
        const auto msg = StandaloneFetch::Decode(BytesSpan{ payload });
        CHECK(msg.group_order == GroupOrder::kDescending);
    }

    SUBCASE("AUTH_TOKEN is collected")
    {
        Parameters params;
        Token token{ .alias_type = Token::AliasType::kUseValue, .token_type = 7, .token_value = FromASCII("secret") };
        params.Add(ParameterType::kAuthorizationToken, token);
        const auto payload = make_payload(params);
        const auto msg = StandaloneFetch::Decode(BytesSpan{ payload });
        REQUIRE(msg.auth_tokens.size() == 1);
        CHECK(msg.auth_tokens[0] == token);
    }
}

TEST_CASE("JoiningFetch resolves parameters and defaults")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    auto make_payload = [](const Parameters& params) {
        Bytes payload;
        payload << RequestID{ 1 };
        payload << FetchType::kRelativeJoiningFetch;
        payload << RequestID{ 2 };
        payload << std::uint64_t{ 5 };
        payload << params;
        return payload;
    };

    SUBCASE("defaults apply when omitted")
    {
        const auto payload = make_payload(Parameters{});
        const auto msg = JoiningFetch::Decode(BytesSpan{ payload });
        CHECK(msg.subscriber_priority == 128);
        CHECK(msg.group_order == GroupOrder::kAscending);
        CHECK_FALSE(msg.fill_timeout.has_value());
        CHECK(msg.auth_tokens.empty());
    }

    SUBCASE("present parameters are resolved")
    {
        Parameters params;
        params.Add(ParameterType::kFillTimeout, std::uint64_t{ 250 });
        params.Add(ParameterType::kGroupOrder, std::uint8_t{ static_cast<std::uint8_t>(GroupOrder::kDescending) });
        const auto payload = make_payload(params);
        const auto msg = JoiningFetch::Decode(BytesSpan{ payload });
        REQUIRE(msg.fill_timeout.has_value());
        CHECK(msg.fill_timeout.value() == 250);
        CHECK(msg.group_order == GroupOrder::kDescending);
    }
}

TEST_CASE("RequestUpdate leaves omitted parameters unset")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    auto make_payload = [](const Parameters& params) {
        Bytes payload;
        payload << RequestID{ 1 };
        payload << params;
        return payload;
    };

    SUBCASE("all parameters optional when omitted")
    {
        const auto payload = make_payload(Parameters{});
        const auto msg = RequestUpdate::Decode(BytesSpan{ payload });
        CHECK_FALSE(msg.subscriber_priority.has_value());
        CHECK_FALSE(msg.forward.has_value());
        CHECK_FALSE(msg.new_group_request.has_value());
        CHECK_FALSE(msg.track_namespace_prefix.has_value());
        CHECK(msg.auth_tokens.empty());
    }

    SUBCASE("present FORWARD resolves to a set optional")
    {
        Parameters params;
        params.Add(ParameterType::kForward, std::uint8_t{ 0 });
        const auto payload = make_payload(params);
        const auto msg = RequestUpdate::Decode(BytesSpan{ payload });
        REQUIRE(msg.forward.has_value());
        CHECK(msg.forward.value() == false);
    }

    SUBCASE("present scalar parameters resolve to set optionals")
    {
        Parameters params;
        params.Add(ParameterType::kDeliveryTimeout, std::uint64_t{ 1500 });
        params.Add(ParameterType::kSubscriberPriority, std::uint8_t{ 64 });
        const auto payload = make_payload(params);
        const auto msg = RequestUpdate::Decode(BytesSpan{ payload });
        REQUIRE(msg.object_delivery_timeout.has_value());
        CHECK(msg.object_delivery_timeout.value() == 1500);
        REQUIRE(msg.subscriber_priority.has_value());
        CHECK(msg.subscriber_priority.value() == 64);
    }
}

TEST_CASE("SubscribeOk resolves EXPIRES and LARGEST_OBJECT")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    auto make_payload = [](const Parameters& params) {
        Bytes payload;
        payload << TrackAlias{ 0xA11CE };
        payload << params;
        payload << TrackExtensions{};
        return payload;
    };

    SUBCASE("omitted parameters are unset")
    {
        const auto payload = make_payload(Parameters{});
        const auto msg = SubscribeOk::Decode(BytesSpan{ payload });
        CHECK_FALSE(msg.expires.has_value());
        CHECK_FALSE(msg.largest_object.has_value());
    }

    SUBCASE("non-zero EXPIRES is resolved")
    {
        Parameters params;
        params.Add(ParameterType::kExpires, std::uint64_t{ 1234 });
        const auto payload = make_payload(params);
        const auto msg = SubscribeOk::Decode(BytesSpan{ payload });
        REQUIRE(msg.expires.has_value());
        CHECK(msg.expires.value() == 1234);
    }

    SUBCASE("EXPIRES of 0 resolves to no expiry")
    {
        Parameters params;
        params.Add(ParameterType::kExpires, std::uint64_t{ 0 });
        const auto payload = make_payload(params);
        const auto msg = SubscribeOk::Decode(BytesSpan{ payload });
        CHECK_FALSE(msg.expires.has_value());
    }

    SUBCASE("LARGEST_OBJECT is resolved")
    {
        Parameters params;
        Location loc{ .group = 99, .object = 123 };
        params.Add(ParameterType::kLargestObject, loc);
        const auto payload = make_payload(params);
        const auto msg = SubscribeOk::Decode(BytesSpan{ payload });
        REQUIRE(msg.largest_object.has_value());
        CHECK(msg.largest_object.value().group == 99);
        CHECK(msg.largest_object.value().object == 123);
    }

    SUBCASE("invalid parameter is rejected")
    {
        Parameters params;
        params.Add(ParameterType::kForward, std::uint8_t{ 1 });
        const auto payload = make_payload(params);
        CHECK_THROWS_AS(SubscribeOk::Decode(BytesSpan{ payload }), ProtocolViolationException);
    }
}

TEST_CASE("Auth-token-only messages collect tokens and reject others")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    Parameters with_token;
    Token token{ .alias_type = Token::AliasType::kUseValue, .token_type = 7, .token_value = FromASCII("sec") };
    with_token.Add(ParameterType::kAuthorizationToken, token);

    SUBCASE("TrackStatus collects the token")
    {
        Bytes payload;
        payload << RequestID{ 1 } << kTrackNamespaceConf << kTrackNameAliceVideo << with_token;
        const auto msg = TrackStatus::Decode(BytesSpan{ payload });
        REQUIRE(msg.auth_tokens.size() == 1);
        CHECK(msg.auth_tokens[0] == token);
    }

    SUBCASE("PublishNamespace rejects a non-token parameter")
    {
        Parameters bad;
        bad.Add(ParameterType::kForward, std::uint8_t{ 1 });
        Bytes payload;
        payload << RequestID{ 1 } << kTrackNamespaceConf << bad;
        CHECK_THROWS_AS(PublishNamespace::Decode(BytesSpan{ payload }), ProtocolViolationException);
    }

    SUBCASE("SubscribeNamespace collects the token")
    {
        Bytes payload;
        payload << RequestID{ 1 } << kTrackNamespaceConf << with_token;
        const auto msg = SubscribeNamespace::Decode(BytesSpan{ payload });
        REQUIRE(msg.auth_tokens.size() == 1);
        CHECK(msg.auth_tokens[0] == token);
    }
}

TEST_CASE("OK messages have distinct parameter allow-lists")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    auto make_payload = [](const Parameters& params) {
        Bytes payload;
        payload << params;
        payload << TrackExtensions{};
        return payload;
    };

    SUBCASE("PublishOk resolves its defaults")
    {
        const auto payload = make_payload(Parameters{});
        const auto msg = PublishOk::Decode(BytesSpan{ payload });
        CHECK(msg.subscriber_priority == 128);
        CHECK(msg.forward == true);
        CHECK_FALSE(msg.expires.has_value());
    }

    SUBCASE("TrackStatusOk rejects EXPIRES")
    {
        Parameters params;
        params.Add(ParameterType::kExpires, std::uint64_t{ 1 });
        const auto payload = make_payload(params);
        CHECK_THROWS_AS(TrackStatusOk::Decode(BytesSpan{ payload }), ProtocolViolationException);
    }

    SUBCASE("PublishNamespaceOk accepts no parameters")
    {
        Parameters params;
        params.Add(ParameterType::kForward, std::uint8_t{ 1 });
        const auto payload = make_payload(params);
        CHECK_THROWS_AS(PublishNamespaceOk::Decode(BytesSpan{ payload }), ProtocolViolationException);
    }

    SUBCASE("RequestUpdateOk resolves EXPIRES and LARGEST_OBJECT")
    {
        Parameters params;
        params.Add(ParameterType::kExpires, std::uint64_t{ 42 });
        const auto payload = make_payload(params);
        const auto msg = RequestUpdateOk::Decode(BytesSpan{ payload });
        REQUIRE(msg.expires.has_value());
        CHECK(msg.expires.value() == 42);
        CHECK_FALSE(msg.largest_object.has_value());
    }
}

TEST_CASE("REQUEST_OK messages that MUST be empty reject Track Properties (§10.5)")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    // A REQUEST_OK with empty parameters followed by a non-empty Track Properties block.
    Bytes payload;
    payload << Parameters{};
    payload << TrackExtensions{}.Add(ExtensionType::kDeliveryTimeout, std::uint64_t{ 0 });

    // Only TRACK_STATUS_OK is permitted to carry properties; every other REQUEST_OK must reject them.
    CHECK_NOTHROW(TrackStatusOk::Decode(BytesSpan{ payload }));
    CHECK_THROWS_AS(PublishOk::Decode(BytesSpan{ payload }), ProtocolViolationException);
    CHECK_THROWS_AS(RequestUpdateOk::Decode(BytesSpan{ payload }), ProtocolViolationException);
    CHECK_THROWS_AS(PublishNamespaceOk::Decode(BytesSpan{ payload }), ProtocolViolationException);
    CHECK_THROWS_AS(SubscribeNamespaceOk::Decode(BytesSpan{ payload }), ProtocolViolationException);
    CHECK_THROWS_AS(SubscribeTracksOk::Decode(BytesSpan{ payload }), ProtocolViolationException);
}

TEST_CASE("AUTHORIZATION_TOKEN may repeat in a single message")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    const Token first{ .alias_type = Token::AliasType::kUseValue, .token_type = 7, .token_value = FromASCII("a") };
    const Token second{ .alias_type = Token::AliasType::kUseValue, .token_type = 7, .token_value = FromASCII("b") };

    Parameters params;
    params.Add(ParameterType::kAuthorizationToken, first);
    params.Add(ParameterType::kAuthorizationToken, second);

    Bytes payload;
    payload << RequestID{ 1 } << kTrackNamespaceConf << params;
    const auto msg = SubscribeNamespace::Decode(BytesSpan{ payload });
    REQUIRE(msg.auth_tokens.size() == 2);
    CHECK(msg.auth_tokens[0] == first);
    CHECK(msg.auth_tokens[1] == second);
}

TEST_CASE("Control messages round-trip through Encode and Decode")
{
    using namespace quicr::messages;
    using namespace quicr::messages::control;

    const Token token{ .alias_type = Token::AliasType::kUseValue, .token_type = 7, .token_value = FromASCII("secret") };
    const Filter filter = TrackFilter{ .property_type = 1, .max_tracks_selected = 2, .timeout = 3 };

    // Decoding then re-encoding must reproduce the exact bytes Encode produced: this exercises
    // both directions including omit-on-default, since the source bytes are already canonical.
    auto is_idempotent = [](const auto& sent) {
        const auto bytes = sent.Encode();
        const auto round_tripped = std::remove_cvref_t<decltype(sent)>::Decode(BytesSpan{ bytes }).Encode();
        return bytes == round_tripped;
    };

    SUBCASE("Publish")
    {
        CHECK(is_idempotent(Publish{
          .request_id = RequestID{ 7 },
          .track_namespace = kTrackNamespaceConf,
          .track_name = kTrackNameAliceVideo,
          .track_alias = TrackAlias{ 0xA11CE },
          .auth_tokens = { token },
          .expires = std::uint64_t{ 1234 },
          .largest_object = Location{ .group = 9, .object = 4 },
          .forward = false,
          .track_properties = TrackExtensions{},
        }));
    }

    SUBCASE("StandaloneFetch")
    {
        CHECK(is_idempotent(StandaloneFetch{
          .request_id = RequestID{ 7 },
          .fetch_type = FetchType::kStandalone,
          .track_namespace = kTrackNamespaceConf,
          .track_name = kTrackNameAliceVideo,
          .start = Location{ .group = 0, .object = 0 },
          .end = Location{ .group = 1, .object = 0 },
          .auth_tokens = { token },
          .fill_timeout = std::uint64_t{ 250 },
          .subscriber_priority = 64,
          .group_order = GroupOrder::kDescending,
        }));
    }

    SUBCASE("JoiningFetch")
    {
        CHECK(is_idempotent(JoiningFetch{
          .request_id = RequestID{ 7 },
          .fetch_type = FetchType::kRelativeJoiningFetch,
          .joining_request_id = RequestID{ 2 },
          .joining_start = 5,
          .auth_tokens = { token },
          .fill_timeout = std::uint64_t{ 250 },
          .subscriber_priority = 64,
          .group_order = GroupOrder::kDescending,
        }));
    }

    SUBCASE("RequestUpdate carries the three-state FORWARD and filter")
    {
        CHECK(is_idempotent(RequestUpdate{
          .request_id = RequestID{ 7 },
          .auth_tokens = { token },
          .object_delivery_timeout = std::uint64_t{ 1500 },
          .subscriber_priority = std::uint8_t{ 64 },
          .subscription_filter = filter,
          .forward = false,
          .new_group_request = std::uint64_t{ 9 },
        }));

        // FORWARD omitted means "unchanged" and must stay absent through a round-trip.
        const auto decoded = RequestUpdate::Decode(BytesSpan{ RequestUpdate{ .request_id = RequestID{ 7 } }.Encode() });
        CHECK_FALSE(decoded.forward.has_value());
        CHECK_FALSE(decoded.subscription_filter.has_value());
    }

    SUBCASE("SubscribeOk")
    {
        CHECK(is_idempotent(SubscribeOk{
          .track_alias = TrackAlias{ 0xA11CE },
          .expires = std::uint64_t{ 1234 },
          .largest_object = Location{ .group = 99, .object = 123 },
          .track_properties = TrackExtensions{},
        }));
    }

    SUBCASE("PublishOk")
    {
        CHECK(is_idempotent(PublishOk{
          .object_delivery_timeout = std::uint64_t{ 100 },
          .subscriber_priority = 64,
          .group_order = GroupOrder::kDescending,
          .subscription_filter = filter,
          .expires = std::uint64_t{ 1234 },
          .forward = false,
          .new_group_request = std::uint64_t{ 9 },
        }));
    }

    SUBCASE("RequestUpdateOk")
    {
        CHECK(is_idempotent(RequestUpdateOk{
          .expires = std::uint64_t{ 42 },
          .largest_object = Location{ .group = 1, .object = 2 },
        }));
    }

    SUBCASE("TrackStatusOk carries Track Properties")
    {
        CHECK(is_idempotent(TrackStatusOk{
          .largest_object = Location{ .group = 1, .object = 2 },
          .track_properties = TrackExtensions{},
        }));
    }

    SUBCASE("auth-token-only messages")
    {
        CHECK(is_idempotent(PublishNamespace{
          .request_id = RequestID{ 7 }, .track_namespace = kTrackNamespaceConf, .auth_tokens = { token } }));
        CHECK(is_idempotent(SubscribeNamespace{
          .request_id = RequestID{ 7 }, .track_namespace_prefix = kTrackNamespaceConf, .auth_tokens = { token } }));
        CHECK(is_idempotent(TrackStatus{ .request_id = RequestID{ 7 },
                                         .track_namespace = kTrackNamespaceConf,
                                         .track_name = kTrackNameAliceVideo,
                                         .auth_tokens = { token } }));
        CHECK(is_idempotent(SubscribeTracks{ .request_id = RequestID{ 7 },
                                             .track_namespace_prefix = kTrackNamespaceConf,
                                             .auth_tokens = { token },
                                             .forward = false }));
    }

    SUBCASE("empty OK messages")
    {
        CHECK(is_idempotent(PublishNamespaceOk{}));
        CHECK(is_idempotent(SubscribeNamespaceOk{}));
        CHECK(is_idempotent(SubscribeTracksOk{}));
    }

    SUBCASE("Setup")
    {
        KeyValuePairs setup_options;
        setup_options.Add(SetupOptionType::kEndpointId, std::string{ "alice" });
        CHECK(is_idempotent(Setup{ .setup_options = std::move(setup_options) }));
    }

    SUBCASE("GOAWAY variants")
    {
        CHECK(is_idempotent(RequestGoaway{ .new_session_uri = FromASCII("moq://relay"), .timeout = 5000 }));
        CHECK(is_idempotent(
          ControlGoaway{ .new_session_uri = FromASCII("moq://relay"), .timeout = 5000, .request_id = RequestID{ 3 } }));
    }

    SUBCASE("FetchOk")
    {
        CHECK(is_idempotent(FetchOk{
          .end_of_track = 1,
          .end_location = Location{ .group = 9, .object = 4 },
          .parameters = Parameters{},
          .track_properties = TrackExtensions{},
        }));
    }

    SUBCASE("PublishDone")
    {
        CHECK(is_idempotent(PublishDone{
          .status_code = PublishDoneStatus::kTrackEnded,
          .stream_count = 12,
          .error_reason = FromASCII("done"),
        }));
    }

    SUBCASE("PublishBlocked, Namespace, NamespaceDone")
    {
        CHECK(is_idempotent(
          PublishBlocked{ .track_namespace_suffix = kTrackNamespaceConf, .track_name = kTrackNameAliceVideo }));
        CHECK(is_idempotent(Namespace{ .track_namespace_suffix = kTrackNamespaceConf }));
        CHECK(is_idempotent(NamespaceDone{ .track_namespace_suffix = kTrackNamespaceConf }));
    }

    SUBCASE("RequestError without and with a redirect")
    {
        CHECK(is_idempotent(RequestError{
          .error_code = ErrorCode::kDoesNotExist,
          .retry_interval = 0,
          .error_reason = FromASCII("nope"),
          .redirect = std::nullopt,
        }));
        CHECK(is_idempotent(RequestError{
          .error_code = static_cast<ErrorCode>(RequestError::kRedirectErrorCode),
          .retry_interval = 0,
          .error_reason = FromASCII("over there"),
          .redirect = Redirect{ .connect_uri = FromASCII("moq://other"),
                                .track_namespace = kTrackNamespaceConf,
                                .track_name = kTrackNameAliceVideo },
        }));
    }
}
