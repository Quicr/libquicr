// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/messages.h"

#include <any>
#include <doctest/doctest.h>
#include <functional>
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

template<typename T>
bool
VerifyCtrl(BytesSpan buffer, uint64_t message_type, T& message)
{
    ControlMessage ctrl_message;
    buffer = buffer >> ctrl_message;

    CHECK_EQ(ctrl_message.type, message_type);

    ctrl_message.payload >> message;

    return true;
}

TEST_CASE("RequestOk Message encode/decode")
{
    auto request_ok = RequestOk{ 0x1234, {} };
    Bytes buffer;
    buffer << request_ok;

    RequestOk request_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kRequestOk), request_ok_out));
    CHECK_EQ(0x1234, request_ok_out.request_id);
}

TEST_CASE("RequestError Message encode/decode")
{
    Bytes buffer;

    auto request_err = RequestError{};
    request_err.request_id = 0x1234;
    request_err.error_code = quicr::messages::ErrorCode::kNotSupported;
    request_err.error_reason = Bytes{ 0x1, 0x2, 0x3 };
    buffer << request_err;

    RequestError request_err_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kRequestError), request_err_out));
    CHECK_EQ(0x1234, request_err_out.request_id);
    CHECK_EQ(request_err_out.error_code, request_err_out.error_code);
    CHECK_EQ(request_err_out.error_reason, request_err_out.error_reason);
}

TEST_CASE("PublishNamespace Message encode/decode")
{
    Bytes buffer;

    auto publish_namespace = PublishNamespace{};
    publish_namespace.track_namespace = kTrackNamespaceConf;
    publish_namespace.parameters = kExampleParameters;
    buffer << publish_namespace;

    PublishNamespace publish_namespace_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kPublishNamespace), publish_namespace_out));
    CHECK_EQ(kTrackNamespaceConf, publish_namespace_out.track_namespace);
    CHECK_EQ(publish_namespace.parameters, publish_namespace_out.parameters);
}

TEST_CASE("PublishNamespaceDoneMessage encode/decode")
{
    Bytes buffer;

    auto publish_namespace_done = PublishNamespaceDone{};
    publish_namespace_done.request_id = 0x1234;
    buffer << publish_namespace_done;

    PublishNamespaceDone publish_namespace_done_out;
    CHECK(
      VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kPublishNamespaceDone), publish_namespace_done_out));
    CHECK_EQ(0x1234, publish_namespace_done_out.request_id);
}

TEST_CASE("PublishNamespaceCancel Message encode/decode")
{
    Bytes buffer;

    auto publish_namespace_cancel = PublishNamespaceCancel{};
    publish_namespace_cancel.request_id = 0x1234;
    publish_namespace_cancel.error_code = ErrorCode::kInternalError;
    buffer << publish_namespace_cancel;

    PublishNamespaceCancel publish_namespace_cancel_out;
    CHECK(VerifyCtrl(
      buffer, static_cast<uint64_t>(ControlMessageType::kPublishNamespaceCancel), publish_namespace_cancel_out));
    CHECK_EQ(publish_namespace_cancel.request_id, publish_namespace_cancel_out.request_id);
    CHECK_EQ(publish_namespace_cancel.error_code, publish_namespace_cancel_out.error_code);
    CHECK_EQ(publish_namespace_cancel.error_reason, publish_namespace_cancel_out.error_reason);
}

TEST_CASE("Subscribe Message encode/decode")
{
    auto params = Parameters{}
                    .Add(messages::ParameterType::kSubscriberPriority, 1)
                    .Add(messages::ParameterType::kGroupOrder, GroupOrder::kAscending)
                    .Add(messages::ParameterType::kSubscriptionFilter, FilterType::kLargestObject);

    Bytes buffer;
    auto subscribe = quicr::messages::Subscribe{ 0x1, kTrackNamespaceConf, kTrackNameAliceVideo, params };

    buffer << subscribe;

    Subscribe subscribe_out{};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.request_id, subscribe_out.request_id);
    CHECK_EQ(1, subscribe_out.parameters.Get<std::uint8_t>(messages::ParameterType::kSubscriberPriority));
    CHECK_EQ(GroupOrder::kAscending, subscribe_out.parameters.Get<GroupOrder>(messages::ParameterType::kGroupOrder));
    CHECK_EQ(FilterType::kLargestObject,
             subscribe_out.parameters.Get<FilterType>(messages::ParameterType::kSubscriptionFilter));
}

TEST_CASE("SubscribeOk Message encode/decode")
{
    auto params = Parameters{}
                    .Add(messages::ParameterType::kExpires, 1234)
                    .Add(messages::ParameterType::kLargestObject, Location{ .group = 10, .object = 5 });

    auto extensions = TrackExtensions{}
                        .Add(ExtensionType::kDeliveryTimeout, 0)
                        .Add(ExtensionType::kMaxCacheDuration, 0)
                        .AddImmutable(ExtensionType::kDefaultPublisherGroupOrder, GroupOrder::kAscending)
                        .Add(ExtensionType::kDefaultPublisherPriority, 1)
                        .AddImmutable(ExtensionType::kDynamicGroups, true);

    Bytes buffer;
    const auto track_alias = uint64_t(kTrackAliasAliceVideo);
    auto subscribe_ok = SubscribeOk(0x1, track_alias, params, extensions);

    buffer << subscribe_ok;

    auto subscribe_ok_out = SubscribeOk{};

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeOk), subscribe_ok_out));
    CHECK_EQ(subscribe_ok.request_id, subscribe_ok_out.request_id);
    CHECK_EQ(subscribe_ok.track_alias, subscribe_ok_out.track_alias);

    CHECK_EQ(1234, subscribe_ok_out.parameters.Get<std::uint64_t>(messages::ParameterType::kExpires));
    CHECK_EQ(10, subscribe_ok_out.parameters.Get<Location>(messages::ParameterType::kLargestObject).group);
    CHECK_EQ(5, subscribe_ok_out.parameters.Get<Location>(messages::ParameterType::kLargestObject).object);

    CHECK_EQ(0, subscribe_ok_out.track_extensions.Get<std::uint64_t>(ExtensionType::kDeliveryTimeout));
    CHECK_EQ(0, subscribe_ok_out.track_extensions.Get<std::uint64_t>(ExtensionType::kMaxCacheDuration));
    CHECK_EQ(1, subscribe_ok_out.track_extensions.Get<std::uint64_t>(ExtensionType::kDefaultPublisherPriority));
    CHECK_EQ(GroupOrder::kAscending,
             subscribe_ok_out.track_extensions.GetImmutable<GroupOrder>(ExtensionType::kDefaultPublisherGroupOrder));
    CHECK_EQ(true, subscribe_ok_out.track_extensions.GetImmutable<bool>(ExtensionType::kDynamicGroups));
}

TEST_CASE("Unsubscribe  Message encode/decode")
{
    Bytes buffer;

    auto unsubscribe = Unsubscribe{};
    unsubscribe.request_id = 0x1;
    buffer << unsubscribe;

    Unsubscribe unsubscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kUnsubscribe), unsubscribe_out));
    CHECK_EQ(unsubscribe.request_id, unsubscribe_out.request_id);
}

TEST_CASE("PublishDone  Message encode/decode")
{
    Bytes buffer;

    auto publish_done = PublishDone{};
    publish_done.request_id = 0x1;
    publish_done.status_code = quicr::messages::PublishDoneStatusCode::kExpired;
    publish_done.stream_count = 0x0;
    publish_done.error_reason = Bytes{ 0x0 };

    buffer << publish_done;

    PublishDone publish_done_out;

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kPublishDone), publish_done_out));
    CHECK_EQ(publish_done.request_id, publish_done_out.request_id);
    CHECK_EQ(publish_done.status_code, publish_done_out.status_code);
    CHECK_EQ(publish_done.stream_count, publish_done_out.stream_count);
    CHECK_EQ(publish_done.error_reason, publish_done_out.error_reason);
}

TEST_CASE("ClientSetup  Message encode/decode")
{
    Bytes buffer;

    const std::string endpoint_id = "client test";

    auto params = SetupParameters{}.Add(SetupParameterType::kEndpointId, endpoint_id);

    auto client_setup = ClientSetup{ params };
    buffer << client_setup;

    ClientSetup client_setup_out = {};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kClientSetup), client_setup_out));
    CHECK_EQ(endpoint_id, client_setup_out.setup_parameters.Get<std::string>(SetupParameterType::kEndpointId));
}

TEST_CASE("ServerSetup  Message encode/decode")
{
    const std::string endpoint_id = "server_test";
    auto params = SetupParameters{}.Add(SetupParameterType::kEndpointId, endpoint_id);
    auto server_setup = ServerSetup{ params };

    Bytes buffer;
    buffer << server_setup;

    ServerSetup server_setup_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kServerSetup), server_setup_out));
    CHECK_EQ(endpoint_id, server_setup_out.setup_parameters.Get<std::string>(SetupParameterType::kEndpointId));
}

TEST_CASE("Goaway Message encode/decode")
{
    Bytes buffer;

    auto goaway = Goaway{};
    goaway.new_session_uri = FromASCII("go.away.now.no.return");
    buffer << goaway;

    Goaway goaway_out{};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kGoaway), goaway_out));
    CHECK_EQ(FromASCII("go.away.now.no.return"), goaway_out.new_session_uri);
}

TEST_CASE("Fetch Message encode/decode")
{
    Bytes buffer;

    auto group_0 = std::make_optional<Fetch::Group_0>();
    if (group_0.has_value()) {
        group_0->standalone.track_namespace = kTrackNamespaceConf;
        group_0->standalone.track_name = kTrackNameAliceVideo;
        group_0->standalone.start.group = 0x1000;
        group_0->standalone.start.object = 0x0;
        group_0->standalone.end.group = 0x2000;
        group_0->standalone.end.object = 0x100;
    }

    auto params =
      Parameters{}.Add(ParameterType::kSubscriberPriority, 2).Add(ParameterType::kGroupOrder, GroupOrder::kAscending);

    auto fetch = Fetch(0x10, FetchType::kStandalone, group_0, std::nullopt, params);

    buffer << fetch;
    {
        auto fetch_out = Fetch(
          [](Fetch& self) {
              if (self.fetch_type == FetchType::kStandalone) {
                  self.group_0 = std::make_optional<Fetch::Group_0>();
              }
          },
          [](Fetch& self) {
              if (self.fetch_type == FetchType::kRelativeJoiningFetch) {
                  self.group_1 = std::make_optional<Fetch::Group_1>();
              }
          });

        CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetch), fetch_out));
        CHECK_EQ(fetch.request_id, fetch_out.request_id);
        CHECK_EQ(2, fetch_out.parameters.Get<std::uint8_t>(ParameterType::kSubscriberPriority));
        CHECK_EQ(GroupOrder::kAscending, fetch_out.parameters.Get<GroupOrder>(ParameterType::kGroupOrder));
        CHECK_EQ(fetch.fetch_type, fetch_out.fetch_type);

        CHECK_EQ(fetch.group_0->standalone.track_namespace, fetch_out.group_0->standalone.track_namespace);
        CHECK_EQ(fetch.group_0->standalone.track_name, fetch_out.group_0->standalone.track_name);
        CHECK_EQ(fetch.group_0->standalone.start, fetch_out.group_0->standalone.start);
        CHECK_EQ(fetch.group_0->standalone.end, fetch_out.group_0->standalone.end);
    }

    buffer.clear();

    auto group_1 = std::make_optional<Fetch::Group_1>();
    if (group_1.has_value()) {
        group_1->joining.request_id = 0x0;
        group_1->joining.joining_start = 0x0;
    }

    fetch = Fetch(0x10, FetchType::kRelativeJoiningFetch, std::nullopt, group_1, params);

    buffer << fetch;
    {
        auto fetch_out = Fetch(
          [](Fetch& self) {
              if (self.fetch_type == FetchType::kStandalone) {
                  self.group_0 = std::make_optional<Fetch::Group_0>();
              }
          },
          [](Fetch& self) {
              if (self.fetch_type == FetchType::kRelativeJoiningFetch) {
                  self.group_1 = std::make_optional<Fetch::Group_1>();
              }
          });

        CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetch), fetch_out));
        CHECK_EQ(fetch.group_1->joining.request_id, fetch_out.group_1->joining.request_id);
        CHECK_EQ(fetch.group_1->joining.joining_start, fetch_out.group_1->joining.joining_start);
    }
}

TEST_CASE("FetchOk/Error/Cancel Message encode/decode")
{
    auto fetch_ok = FetchOk{};
    fetch_ok.request_id = 0x1234;
    fetch_ok.end_location = { 0x9999, 0x9991 };
    fetch_ok.track_extensions = TrackExtensions{}
                                  .Add(ExtensionType::kDeliveryTimeout, 0)
                                  .Add(ExtensionType::kMaxCacheDuration, 0)
                                  .Add(ExtensionType::kDefaultPublisherGroupOrder, GroupOrder::kAscending)
                                  .Add(ExtensionType::kDefaultPublisherPriority, 1)
                                  .Add(ExtensionType::kDynamicGroups, true);

    Bytes buffer;
    buffer << fetch_ok;

    FetchOk fetch_ok_out{};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetchOk), fetch_ok_out));
    CHECK_EQ(fetch_ok.request_id, fetch_ok_out.request_id);
    CHECK_EQ(fetch_ok.end_location.group, fetch_ok_out.end_location.group);
    CHECK_EQ(fetch_ok.end_location.object, fetch_ok_out.end_location.object);

    CHECK_EQ(0, fetch_ok.track_extensions.Get<std::uint64_t>(ExtensionType::kDeliveryTimeout));
    CHECK_EQ(0, fetch_ok.track_extensions.Get<std::uint64_t>(ExtensionType::kMaxCacheDuration));
    CHECK_EQ(1, fetch_ok.track_extensions.Get<std::uint64_t>(ExtensionType::kDefaultPublisherPriority));
    CHECK_EQ(GroupOrder::kAscending,
             fetch_ok.track_extensions.Get<GroupOrder>(ExtensionType::kDefaultPublisherGroupOrder));
    CHECK_EQ(true, fetch_ok.track_extensions.Get<bool>(ExtensionType::kDynamicGroups));

    buffer.clear();
    auto fetch_cancel = FetchCancel{};
    fetch_cancel.request_id = 0x1111;

    buffer << fetch_cancel;

    FetchCancel fetch_cancel_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetchCancel), fetch_cancel_out));
    CHECK_EQ(fetch_cancel.request_id, fetch_cancel_out.request_id);
}

TEST_CASE("SubscribesBlocked Message encode/decode")
{
    Bytes buffer;

    auto sub_blocked = RequestsBlocked{};
    sub_blocked.maximum_request_id = std::numeric_limits<uint64_t>::max() >> 2;
    buffer << sub_blocked;

    RequestsBlocked sub_blocked_out{};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kRequestsBlocked), sub_blocked_out));
    CHECK_EQ(sub_blocked.maximum_request_id, sub_blocked_out.maximum_request_id);
}

TEST_CASE("Subscribe Namespaces encode/decode")
{
    Bytes buffer;

    auto msg = SubscribeNamespace{};
    msg.track_namespace_prefix = TrackNamespace{ "cisco"s, "meetings"s, "video"s, "1080p"s };
    buffer << msg;

    SubscribeNamespace msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeNamespace), msg_out));
    CHECK_EQ(msg.track_namespace_prefix, msg_out.track_namespace_prefix);
}
TEST_CASE("Namespace Done encode/decode")
{
    Bytes buffer;

    auto msg = NamespaceDone{ TrackNamespace{ "cisco"s, "meetings"s, "video"s, "1080p"s } };
    buffer << msg;

    NamespaceDone msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kNamespaceDone), msg_out));
    CHECK_EQ(msg.track_namespace_suffix, msg_out.track_namespace_suffix);
}

TEST_CASE("Publish Message encode/decode")
{
    Bytes buffer;

    std::optional<Location> largest_location = std::nullopt;
    auto params = Parameters{}
                    .Add(ParameterType::kForward, false)
                    .Add(ParameterType::kExpires, 1000)
                    .AddOptional(ParameterType::kLargestObject, largest_location);

    auto extensions = TrackExtensions{}
                        .Add(ExtensionType::kDeliveryTimeout, 0)
                        .Add(ExtensionType::kMaxCacheDuration, 0)
                        .Add(ExtensionType::kDefaultPublisherGroupOrder, GroupOrder::kAscending)
                        .Add(ExtensionType::kDefaultPublisherPriority, 1)
                        .Add(ExtensionType::kDynamicGroups, true);

    auto publish =
      Publish(0x1234, kTrackNamespaceConf, kTrackNameAliceVideo, kTrackAliasAliceVideo.Get(), params, extensions);

    buffer << publish;

    auto publish_out = Publish{};

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kPublish), publish_out));
    CHECK_EQ(publish.request_id, publish_out.request_id);
    CHECK_EQ(publish.track_namespace, publish_out.track_namespace);
    CHECK_EQ(publish.track_name, publish_out.track_name);
    CHECK_EQ(publish.track_alias, publish_out.track_alias);

    CHECK_EQ(false, publish_out.parameters.Get<bool>(ParameterType::kForward));
    CHECK_EQ(1000, publish_out.parameters.Get<std::uint64_t>(ParameterType::kExpires));
    CHECK_FALSE(publish_out.parameters.Contains(ParameterType::kLargestObject));

    CHECK_EQ(0, publish_out.track_extensions.Get<std::uint64_t>(ExtensionType::kDeliveryTimeout));
    CHECK_EQ(0, publish_out.track_extensions.Get<std::uint64_t>(ExtensionType::kMaxCacheDuration));
    CHECK_EQ(1, publish_out.track_extensions.Get<std::uint64_t>(ExtensionType::kDefaultPublisherPriority));
    CHECK_EQ(GroupOrder::kAscending,
             publish_out.track_extensions.Get<GroupOrder>(ExtensionType::kDefaultPublisherGroupOrder));
    CHECK_EQ(true, publish_out.track_extensions.Get<bool>(ExtensionType::kDynamicGroups));
}

TEST_CASE("PublishOk Message encode/decode")
{
    auto params = Parameters{}
                    .Add(ParameterType::kSubscriberPriority, 2)
                    .Add(ParameterType::kGroupOrder, GroupOrder::kAscending)
                    .Add(ParameterType::kSubscriptionFilter, FilterType::kLargestObject)
                    .Add(ParameterType::kForward, false);

    Bytes buffer;
    auto publish_ok = PublishOk(0x1234, params);
    buffer << publish_ok;

    auto publish_ok_out = PublishOk{};

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kPublishOk), publish_ok_out));
    CHECK_EQ(publish_ok.request_id, publish_ok_out.request_id);
    CHECK_EQ(2, publish_ok_out.parameters.Get<std::uint8_t>(ParameterType::kSubscriberPriority));
    CHECK_EQ(GroupOrder::kAscending, publish_ok_out.parameters.Get<GroupOrder>(ParameterType::kGroupOrder));
    CHECK_EQ(FilterType::kLargestObject, publish_ok_out.parameters.Get<FilterType>(ParameterType::kSubscriptionFilter));
    CHECK_EQ(false, publish_ok_out.parameters.Get<bool>(ParameterType::kForward));
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

TEST_CASE("ControlMessage encode/decode")
{
    ControlMessage msg;
    msg.type = 1234;
    msg.payload = Bytes{ 1, 2, 3, 4 };
    Bytes buffer;
    buffer << msg;
    ControlMessage out;
    buffer >> out;
    CHECK_EQ(out.type, msg.type);
    CHECK_EQ(out.payload, msg.payload);
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
        0,      1,
        63, // Max 1-byte varint
        64, // Min 2-byte varint
        127,    128, 255,
        16383, // Max 2-byte varint
        16384, // Min 4-byte varint
        100000,
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
        0,      1,
        63, // Max 1-byte varint
        64, // Min 2-byte varint
        127,    128, 255,
        16383, // Max 2-byte varint
        16384, // Min 4-byte varint
        100000,
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
