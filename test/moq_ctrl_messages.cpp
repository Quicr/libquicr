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

const Parameters kExampleParameters = {
    { ParameterType::kAuthorizationToken, kExampleBytes }, { static_cast<ParameterType>(2), kUint1ByteValue },
    { static_cast<ParameterType>(4), kUint2ByteValue },    { static_cast<ParameterType>(6), kUint4ByteValue },
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

TEST_CASE("AnnounceOk Message encode/decode")
{
    Bytes buffer;

    auto announce_ok = AnnounceOk();
    announce_ok.request_id = 0x1234;
    buffer << announce_ok;

    AnnounceOk announce_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kAnnounceOk), announce_ok_out));
    CHECK_EQ(0x1234, announce_ok_out.request_id);
}

TEST_CASE("Announce Message encode/decode")
{
    Bytes buffer;

    auto announce = Announce{};
    announce.track_namespace = kTrackNamespaceConf;
    announce.parameters = kExampleParameters;
    buffer << announce;

    Announce announce_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kAnnounce), announce_out));
    CHECK_EQ(kTrackNamespaceConf, announce_out.track_namespace);
    CHECK_EQ(announce.parameters, announce_out.parameters);
}

TEST_CASE("Unannounce Message encode/decode")
{
    Bytes buffer;

    auto unannounce = Unannounce{};
    unannounce.track_namespace = kTrackNamespaceConf;
    buffer << unannounce;

    Unannounce unannounce_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kUnannounce), unannounce_out));
    CHECK_EQ(kTrackNamespaceConf, unannounce_out.track_namespace);
}

TEST_CASE("AnnounceError Message encode/decode")
{
    Bytes buffer;

    auto announce_err = AnnounceError{};
    announce_err.request_id = 0x1234;
    announce_err.error_code = quicr::messages::AnnounceErrorCode::kNotSupported;
    announce_err.error_reason = Bytes{ 0x1, 0x2, 0x3 };
    buffer << announce_err;

    AnnounceError announce_err_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kAnnounceError), announce_err_out));
    CHECK_EQ(0x1234, announce_err_out.request_id);
    CHECK_EQ(announce_err.error_code, announce_err_out.error_code);
    CHECK_EQ(announce_err.error_reason, announce_err_out.error_reason);
}

TEST_CASE("AnnounceCancel Message encode/decode")
{
    Bytes buffer;

    auto announce_cancel = AnnounceCancel{};
    announce_cancel.track_namespace = kTrackNamespaceConf;
    buffer << announce_cancel;

    AnnounceCancel announce_cancel_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kAnnounceCancel), announce_cancel_out));
    CHECK_EQ(announce_cancel.track_namespace, announce_cancel_out.track_namespace);
    CHECK_EQ(announce_cancel.error_code, announce_cancel_out.error_code);
    CHECK_EQ(announce_cancel.error_reason, announce_cancel_out.error_reason);
}

TEST_CASE("Subscribe (kLatestObject) Message encode/decode")
{
    Bytes buffer;
    auto subscribe = quicr::messages::Subscribe(0x1,
                                                kTrackNamespaceConf,
                                                kTrackNameAliceVideo,
                                                0x10,
                                                GroupOrder::kAscending,
                                                1,
                                                FilterType::kLatestObject,
                                                nullptr,
                                                std::nullopt,
                                                nullptr,
                                                std::nullopt,
                                                {});

    buffer << subscribe;

    Subscribe subscribe_out(
      [](Subscribe& msg) {
          if (msg.filter_type == FilterType::kLatestObject) {
              // do nothing...
          }
      },
      [](Subscribe& msg) {
          if (msg.filter_type == FilterType::kLatestGroup) {
              // again
          }
      });
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.request_id, subscribe_out.request_id);
    CHECK_EQ(subscribe.subscriber_priority, subscribe_out.subscriber_priority);
    CHECK_EQ(subscribe.group_order, subscribe_out.group_order);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
}

TEST_CASE("Subscribe (kLatestGroup) Message encode/decode")
{
    Bytes buffer;
    auto subscribe = quicr::messages::Subscribe(0x1,
                                                kTrackNamespaceConf,
                                                kTrackNameAliceVideo,
                                                0x10,
                                                GroupOrder::kAscending,
                                                1,
                                                FilterType::kLatestObject,
                                                nullptr,
                                                std::nullopt,
                                                nullptr,
                                                std::nullopt,
                                                {});

    buffer << subscribe;

    auto subscribe_out = Subscribe(
      [](Subscribe& msg) {
          if (msg.filter_type == FilterType::kLatestObject) {
              // do nothing...
          }
      },
      [](Subscribe& msg) {
          if (msg.filter_type == FilterType::kLatestGroup) {
              // again
          }
      });

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.request_id, subscribe_out.request_id);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
}

TEST_CASE("Subscribe (kAbsoluteStart) Message encode/decode")
{
    Bytes buffer;

    auto group_0 = std::make_optional<Subscribe::Group_0>();
    group_0->start_location = { 0x1000, 0xFF };

    auto subscribe = quicr::messages::Subscribe(0x1,
                                                kTrackNamespaceConf,
                                                kTrackNameAliceVideo,
                                                0x10,
                                                GroupOrder::kAscending,
                                                1,
                                                FilterType::kAbsoluteStart,
                                                nullptr,
                                                group_0,
                                                nullptr,
                                                std::nullopt,
                                                {});

    buffer << subscribe;

    auto subscribe_out = Subscribe(
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteStart ||
              subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_0 = std::make_optional<Subscribe::Group_0>();
          }
      },
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_1 = std::make_optional<Subscribe::Group_1>();
          }
      });

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.request_id, subscribe_out.request_id);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribe.group_0->start_location.group, subscribe_out.group_0->start_location.group);
    CHECK_EQ(subscribe.group_0->start_location.object, subscribe_out.group_0->start_location.object);
}

TEST_CASE("Subscribe (kAbsoluteRange) Message encode/decode")
{
    Bytes buffer;

    auto group_0 = std::make_optional<Subscribe::Group_0>();
    if (group_0.has_value()) {
        group_0->start_location = { 0x1000, 0x1 };
    }
    auto group_1 = std::make_optional<Subscribe::Group_1>();
    if (group_1.has_value()) {
        group_1->end_group = 0xFFF;
    }

    auto subscribe = quicr::messages::Subscribe(0x1,
                                                kTrackNamespaceConf,
                                                kTrackNameAliceVideo,
                                                0x10,
                                                GroupOrder::kAscending,
                                                1,
                                                FilterType::kAbsoluteRange,
                                                nullptr,
                                                group_0,
                                                nullptr,
                                                group_1,
                                                {});

    buffer << subscribe;

    auto subscribe_out = Subscribe(
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteStart ||
              subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_0 = std::make_optional<Subscribe::Group_0>();
          }
      },
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_1 = std::make_optional<Subscribe::Group_1>();
          }
      });

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.request_id, subscribe_out.request_id);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribe.group_0->start_location.group, subscribe_out.group_0->start_location.group);
    CHECK_EQ(subscribe.group_0->start_location.object, subscribe_out.group_0->start_location.object);
    CHECK_EQ(subscribe.group_1->end_group, subscribe_out.group_1->end_group);
}

TEST_CASE("Subscribe (Params) Message encode/decode")
{
    Bytes buffer;
    Parameter param;
    param.type = ParameterType::kDeliveryTimeout;
    param.value = { 0x1, 0x2 };
    SubscribeParameters params = { param };

    auto subscribe = quicr::messages::Subscribe(0x1,
                                                kTrackNamespaceConf,
                                                kTrackNameAliceVideo,
                                                0x10,
                                                GroupOrder::kAscending,
                                                1,
                                                FilterType::kLatestObject,
                                                nullptr,
                                                std::nullopt,
                                                nullptr,
                                                std::nullopt,
                                                params);

    buffer << subscribe;

    auto subscribe_out = Subscribe(
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteStart ||
              subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_0 = std::make_optional<Subscribe::Group_0>();
          }
      },
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_1 = std::make_optional<Subscribe::Group_1>();
          }
      }

    );

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.request_id, subscribe_out.request_id);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    REQUIRE_EQ(subscribe.subscribe_parameters.size(), 1);
    REQUIRE_EQ(subscribe.subscribe_parameters.size(), subscribe_out.subscribe_parameters.size());
    CHECK_EQ(subscribe.subscribe_parameters[0], subscribe_out.subscribe_parameters[0]);
}

TEST_CASE("Subscribe (Params - 2) Message encode/decode")
{
    Bytes buffer;
    Parameter param1;
    param1.type = ParameterType::kAuthorizationToken;
    param1.value = { 0x1, 0x2 };

    Parameter param2;
    param2.type = ParameterType::kAuthorizationToken;
    param2.value = { 0x1, 0x2, 0x3 };

    SubscribeParameters params = { param1, param2 };

    auto subscribe = quicr::messages::Subscribe(0x1,
                                                kTrackNamespaceConf,
                                                kTrackNameAliceVideo,
                                                0x10,
                                                GroupOrder::kAscending,
                                                1,
                                                FilterType::kLatestObject,
                                                nullptr,
                                                std::nullopt,
                                                nullptr,
                                                std::nullopt,
                                                params);

    buffer << subscribe;

    auto subscribe_out = Subscribe(
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteStart ||
              subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_0 = std::make_optional<Subscribe::Group_0>();
          }
      },
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_1 = std::make_optional<Subscribe::Group_1>();
          }
      }

    );

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.request_id, subscribe_out.request_id);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    REQUIRE_EQ(subscribe.subscribe_parameters.size(), 2);
    REQUIRE_EQ(subscribe.subscribe_parameters.size(), subscribe_out.subscribe_parameters.size());
    CHECK_EQ(subscribe.subscribe_parameters[0], subscribe_out.subscribe_parameters[0]);
    CHECK_EQ(subscribe.subscribe_parameters[1], subscribe_out.subscribe_parameters[1]);
}

Subscribe
GenerateSubscribe(FilterType filter, size_t num_params = 0, uint64_t sg = 0, uint64_t so = 0, uint64_t eg = 0)
{
    auto out = Subscribe(
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteStart ||
              subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_0 = std::make_optional<Subscribe::Group_0>();
          }
      },
      [](Subscribe& subscribe) {
          if (subscribe.filter_type == FilterType::kAbsoluteRange) {
              subscribe.group_1 = std::make_optional<Subscribe::Group_1>();
          }
      });
    out.request_id = 0xABCD;
    out.track_namespace = kTrackNamespaceConf;
    out.track_name = kTrackNameAliceVideo;
    out.filter_type = filter;
    switch (filter) {
        case FilterType::kLatestObject:
        case FilterType::kLatestGroup:
            break;
        case FilterType::kAbsoluteStart:
            out.group_0 = std::make_optional<Subscribe::Group_0>();
            out.group_0->start_location = { sg, so };
            break;
        case FilterType::kAbsoluteRange:
            out.group_0 = std::make_optional<Subscribe::Group_0>();
            out.group_0->start_location = { sg, so };
            out.group_1 = std::make_optional<Subscribe::Group_1>();
            out.group_1->end_group = eg;
            break;
        default:
            break;
    }

    while (num_params > 0) {
        Parameter param1;
        param1.type = ParameterType::kDeliveryTimeout;
        param1.value = { 0x1, 0x2 };
        out.subscribe_parameters.push_back(param1);
        num_params--;
    }
    return out;
}

TEST_CASE("Subscribe (Combo) Message encode/decode")
{
    auto subscribes = std::vector<Subscribe>{
        GenerateSubscribe(FilterType::kLatestObject),
        GenerateSubscribe(FilterType::kLatestGroup),
        GenerateSubscribe(FilterType::kLatestObject, 1),
        GenerateSubscribe(FilterType::kLatestGroup, 2),
        GenerateSubscribe(FilterType::kAbsoluteStart, 0, 0x100, 0x2),
        GenerateSubscribe(FilterType::kAbsoluteStart, 2, 0x100, 0x2),
        GenerateSubscribe(FilterType::kAbsoluteRange, 0, 0x100, 0x2, 0x500),
        GenerateSubscribe(FilterType::kAbsoluteRange, 2, 0x100, 0x2, 0x500),
    };

    for (size_t i = 0; i < subscribes.size(); i++) {
        Bytes buffer;
        buffer << subscribes[i];
        auto subscribe_out = Subscribe(
          [](Subscribe& subscribe) {
              if (subscribe.filter_type == FilterType::kAbsoluteStart ||
                  subscribe.filter_type == FilterType::kAbsoluteRange) {
                  subscribe.group_0 = std::make_optional<Subscribe::Group_0>();
              }
          },
          [](Subscribe& subscribe) {
              if (subscribe.filter_type == FilterType::kAbsoluteRange) {
                  subscribe.group_1 = std::make_optional<Subscribe::Group_1>();
              }
          });

        CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
        CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
        CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
        CHECK_EQ(subscribes[i].request_id, subscribe_out.request_id);
        CHECK_EQ(subscribes[i].filter_type, subscribe_out.filter_type);
        CHECK_EQ(subscribes[i].subscribe_parameters.size(), subscribe_out.subscribe_parameters.size());
        for (size_t j = 0; j < subscribes[i].subscribe_parameters.size(); j++) {
            CHECK_EQ(subscribes[i].subscribe_parameters[j], subscribe_out.subscribe_parameters[j]);
        }
    }
}

TEST_CASE("SubscribeUpdate Message encode/decode")
{
    Bytes buffer;

    auto subscribe_update = SubscribeUpdate{};
    subscribe_update.request_id = 0x1;
    subscribe_update.start_location = { uint64_t(0x1000), uint64_t(0x100) };
    subscribe_update.end_group = uint64_t(0x2000);
    subscribe_update.subscriber_priority = static_cast<SubscriberPriority>(0x10);

    buffer << subscribe_update;

    SubscribeUpdate subscribe_update_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeUpdate), subscribe_update_out));
    CHECK_EQ(0x1000, subscribe_update_out.start_location.group);
    CHECK_EQ(0x100, subscribe_update_out.start_location.object);
    CHECK_EQ(subscribe_update.request_id, subscribe_update_out.request_id);
    CHECK_EQ(0x2000, subscribe_update_out.end_group);
    CHECK_EQ(subscribe_update.subscriber_priority, subscribe_update_out.subscriber_priority);
}

TEST_CASE("SubscribeOk Message encode/decode")
{
    Bytes buffer;
    const auto track_alias = uint64_t(kTrackAliasAliceVideo);
    auto subscribe_ok = SubscribeOk(0x1, track_alias, 0, GroupOrder::kAscending, false, nullptr, std::nullopt, {});

    buffer << subscribe_ok;

    auto subscribe_ok_out = SubscribeOk([](SubscribeOk& msg) {
        if (msg.content_exists == 1) {
            msg.group_0 = std::make_optional<SubscribeOk::Group_0>();
        }
    });

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeOk), subscribe_ok_out));
    CHECK_EQ(subscribe_ok.request_id, subscribe_ok_out.request_id);
    CHECK_EQ(subscribe_ok.track_alias, subscribe_ok_out.track_alias);
    CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
    CHECK_EQ(subscribe_ok.group_order, subscribe_ok_out.group_order);
    CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
}

TEST_CASE("SubscribeOk (content-exists) Message encode/decode")
{
    Bytes buffer;

    auto group_0 = std::make_optional<SubscribeOk::Group_0>();
    group_0->largest_location = { 100, 200 };

    auto subscribe_ok = SubscribeOk(0x01, 0x1000, 0, GroupOrder::kAscending, 1, nullptr, group_0, {});

    buffer << subscribe_ok;

    auto subscribe_ok_out = SubscribeOk([](SubscribeOk& msg) {
        if (msg.content_exists == 1) {
            msg.group_0 = std::make_optional<SubscribeOk::Group_0>();
        }
    });

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeOk), subscribe_ok_out));
    CHECK_EQ(subscribe_ok.request_id, subscribe_ok_out.request_id);
    CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
    CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
    CHECK_EQ(subscribe_ok.group_0.has_value(), subscribe_ok_out.group_0.has_value());
    CHECK_EQ(subscribe_ok.group_0->largest_location.group, subscribe_ok_out.group_0->largest_location.group);
    CHECK_EQ(subscribe_ok.group_0->largest_location.object, subscribe_ok_out.group_0->largest_location.object);
}

TEST_CASE("Error  Message encode/decode")
{
    Bytes buffer;

    auto subscribe_err = SubscribeError{};
    subscribe_err.request_id = 0x1;
    subscribe_err.error_code = quicr::messages::SubscribeErrorCode::kTrackDoesNotExist;
    subscribe_err.error_reason = Bytes{ 0x0, 0x1 };
    buffer << subscribe_err;

    SubscribeError subscribe_err_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeError), subscribe_err_out));
    CHECK_EQ(subscribe_err.request_id, subscribe_err_out.request_id);
    CHECK_EQ(subscribe_err.error_code, subscribe_err_out.error_code);
    CHECK_EQ(subscribe_err.error_reason, subscribe_err_out.error_reason);
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

TEST_CASE("SubscribeDone  Message encode/decode")
{
    Bytes buffer;

    auto subscribe_done = SubscribeDone{};
    subscribe_done.request_id = 0x1;
    subscribe_done.status_code = quicr::messages::SubscribeDoneStatusCode::kExpired;
    subscribe_done.stream_count = 0x0;
    subscribe_done.error_reason = Bytes{ 0x0 };

    buffer << subscribe_done;

    SubscribeDone subscribe_done_out;

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeDone), subscribe_done_out));
    CHECK_EQ(subscribe_done.request_id, subscribe_done_out.request_id);
    CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
    CHECK_EQ(subscribe_done.stream_count, subscribe_done_out.stream_count);
    CHECK_EQ(subscribe_done.error_reason, subscribe_done_out.error_reason);
}

TEST_CASE("SubscribeDone (content-exists)  Message encode/decode")
{
    Bytes buffer;

    auto subscribe_done = SubscribeDone{};
    subscribe_done.request_id = 0x1;
    subscribe_done.status_code = quicr::messages::SubscribeDoneStatusCode::kGoingAway;
    subscribe_done.stream_count = 0x0;
    subscribe_done.error_reason = Bytes{ 0x0 };

    buffer << subscribe_done;

    SubscribeDone subscribe_done_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeDone), subscribe_done_out));
    CHECK_EQ(subscribe_done.request_id, subscribe_done_out.request_id);
    CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
    CHECK_EQ(subscribe_done.stream_count, subscribe_done_out.stream_count);
    CHECK_EQ(subscribe_done.error_reason, subscribe_done_out.error_reason);
}

TEST_CASE("ClientSetup  Message encode/decode")
{
    Bytes buffer;

    const std::string endpoint_id = "client test";

    auto client_setup = ClientSetup(
      { 0x1000, 0x2000 }, { { SetupParameterType::kEndpointId, Bytes(endpoint_id.begin(), endpoint_id.end()) } });
    buffer << client_setup;

    ClientSetup client_setup_out = {};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kClientSetup), client_setup_out));
    CHECK_EQ(client_setup.supported_versions, client_setup_out.supported_versions);
    CHECK_EQ(client_setup.setup_parameters[0], client_setup_out.setup_parameters[0]);
}

TEST_CASE("ServerSetup  Message encode/decode")
{
    const std::string endpoint_id = "server_test";
    auto server_setup =
      ServerSetup(0x1000, { { SetupParameterType::kEndpointId, Bytes(endpoint_id.begin(), endpoint_id.end()) } });

    Bytes buffer;
    buffer << server_setup;

    ServerSetup server_setup_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kServerSetup), server_setup_out));
    CHECK_EQ(server_setup.selected_version, server_setup_out.selected_version);
    CHECK_EQ(server_setup.setup_parameters[0], server_setup.setup_parameters[0]);
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
        group_0->track_namespace = kTrackNamespaceConf;
        group_0->track_name = kTrackNameAliceVideo;
        group_0->start_location.group = 0x1000;
        group_0->start_location.object = 0x0;
        group_0->end_location.group = 0x2000;
        group_0->end_location.object = 0x100;
    }
    auto fetch =
      Fetch(0x10, 1, GroupOrder::kAscending, FetchType::kStandalone, nullptr, group_0, nullptr, std::nullopt, {});

    buffer << fetch;
    {
        auto fetch_out = Fetch(
          [](Fetch& self) {
              if (self.fetch_type == FetchType::kStandalone) {
                  self.group_0 = std::make_optional<Fetch::Group_0>();
              } else {
                  self.group_1 = std::make_optional<Fetch::Group_1>();
              }
          },
          nullptr);

        CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetch), fetch_out));
        CHECK_EQ(fetch.request_id, fetch_out.request_id);
        CHECK_EQ(fetch.subscriber_priority, fetch_out.subscriber_priority);
        CHECK_EQ(fetch.group_order, fetch_out.group_order);
        CHECK_EQ(fetch.fetch_type, fetch_out.fetch_type);

        CHECK_EQ(fetch.group_0->track_namespace, fetch_out.group_0->track_namespace);
        CHECK_EQ(fetch.group_0->track_name, fetch_out.group_0->track_name);
        CHECK_EQ(fetch.group_0->start_location, fetch_out.group_0->start_location);
        CHECK_EQ(fetch.group_0->end_location, fetch_out.group_0->end_location);
    }

    buffer.clear();

    auto group_1 = std::make_optional<Fetch::Group_1>();
    if (group_1.has_value()) {
        group_1->joining_request_id = 0x0;
        group_1->joining_start = 0x0;
    }

    fetch =
      Fetch(0x10, 1, GroupOrder::kAscending, FetchType::kJoiningFetch, nullptr, std::nullopt, nullptr, group_1, {});

    buffer << fetch;
    {
        auto fetch_out = Fetch(
          [](Fetch& self) {
              if (self.fetch_type == FetchType::kStandalone) {
                  self.group_0 = std::make_optional<Fetch::Group_0>();
              } else {
                  self.group_1 = std::make_optional<Fetch::Group_1>();
              }
          },
          nullptr);

        CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetch), fetch_out));
        CHECK_EQ(fetch.group_1->joining_request_id, fetch_out.group_1->joining_request_id);
        CHECK_EQ(fetch.group_1->joining_start, fetch_out.group_1->joining_start);
    }
}

TEST_CASE("FetchOk/Error/Cancel Message encode/decode")
{
    Bytes buffer;

    auto fetch_ok = FetchOk{};
    fetch_ok.request_id = 0x1234;
    fetch_ok.group_order = GroupOrder::kDescending;
    fetch_ok.end_location = { 0x9999, 0x9991 };

    buffer << fetch_ok;

    FetchOk fetch_ok_out{};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetchOk), fetch_ok_out));
    CHECK_EQ(fetch_ok.request_id, fetch_ok_out.request_id);
    CHECK_EQ(fetch_ok.group_order, fetch_ok_out.group_order);
    CHECK_EQ(fetch_ok.end_location.group, fetch_ok_out.end_location.group);
    CHECK_EQ(fetch_ok.end_location.object, fetch_ok_out.end_location.object);

    buffer.clear();
    auto fetch_cancel = FetchCancel{};
    fetch_cancel.request_id = 0x1111;

    buffer << fetch_cancel;

    FetchCancel fetch_cancel_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetchCancel), fetch_cancel_out));
    CHECK_EQ(fetch_cancel.request_id, fetch_cancel_out.request_id);

    buffer.clear();
    auto fetch_error = FetchError{};
    fetch_error.request_id = 0x1111;
    fetch_error.error_code = quicr::messages::FetchErrorCode::kInternalError;

    buffer << fetch_error;

    FetchError fetch_error_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetchError), fetch_error_out));
    CHECK_EQ(fetch_error.request_id, fetch_error_out.request_id);
    CHECK_EQ(fetch_error.error_code, fetch_error_out.error_code);
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

TEST_CASE("Subscribe Announces encode/decode")
{
    Bytes buffer;

    auto msg = SubscribeAnnounces{};
    msg.track_namespace_prefix = TrackNamespace{ "cisco"s, "meetings"s, "video"s, "1080p"s };
    buffer << msg;

    SubscribeAnnounces msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeAnnounces), msg_out));
    CHECK_EQ(msg.track_namespace_prefix, msg_out.track_namespace_prefix);
}

TEST_CASE("Subscribe Announces Ok encode/decode")
{
    Bytes buffer;

    auto msg = SubscribeAnnouncesOk{};
    msg.request_id = 0x1234;
    buffer << msg;

    SubscribeAnnouncesOk msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeAnnouncesOk), msg_out));
    CHECK_EQ(msg.request_id, msg_out.request_id);
}

TEST_CASE("Unsubscribe Announces encode/decode")
{
    Bytes buffer;

    auto msg = UnsubscribeAnnounces{};
    msg.track_namespace_prefix = TrackNamespace{ "cisco"s, "meetings"s, "video"s, "1080p"s };
    buffer << msg;

    UnsubscribeAnnounces msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kUnsubscribeAnnounces), msg_out));
    CHECK_EQ(msg.track_namespace_prefix, msg_out.track_namespace_prefix);
}

TEST_CASE("Subscribe Announces Error encode/decode")
{
    Bytes buffer;

    auto msg = SubscribeAnnouncesError{};
    msg.request_id = 0x1234;
    msg.error_code = quicr::messages::SubscribeAnnouncesErrorCode::kNamespacePrefixUnknown;
    msg.error_reason = Bytes{ 0x1, 0x2, 0x3 };
    buffer << msg;

    SubscribeAnnouncesError msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<std::uint64_t>(ControlMessageType::kSubscribeAnnouncesError), msg_out));
    CHECK_EQ(msg.request_id, msg_out.request_id);
    CHECK_EQ(msg.error_code, msg_out.error_code);
    CHECK_EQ(msg.error_reason, msg_out.error_reason);
}

TEST_CASE("Publish Message encode/decode")
{
    Bytes buffer;

    auto publish = Publish(0x1234,
                           kTrackNamespaceConf,
                           kTrackNameAliceVideo,
                           kTrackAliasAliceVideo.Get(),
                           GroupOrder::kAscending,
                           1,
                           nullptr,
                           std::nullopt,
                           true,
                           {});

    buffer << publish;

    auto publish_out =
      Publish(0, TrackNamespace{}, Bytes{}, 0, GroupOrder::kAscending, 0, nullptr, std::nullopt, false, {});

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kPublish), publish_out));
    CHECK_EQ(publish.request_id, publish_out.request_id);
    CHECK_EQ(publish.track_namespace, publish_out.track_namespace);
    CHECK_EQ(publish.track_name, publish_out.track_name);
    CHECK_EQ(publish.track_alias, publish_out.track_alias);
    CHECK_EQ(publish.group_order, publish_out.group_order);
    CHECK_EQ(publish.contentexists, publish_out.contentexists);
    CHECK_EQ(publish.forward, publish_out.forward);
    CHECK_EQ(publish.parameters, publish_out.parameters);
}

TEST_CASE("PublishOk Message encode/decode")
{
    Bytes buffer;

    auto publish_ok = PublishOk(0x1234,
                                true,
                                0x10,
                                GroupOrder::kAscending,
                                FilterType::kLatestObject,
                                nullptr,
                                std::nullopt,
                                nullptr,
                                std::nullopt,
                                {});

    buffer << publish_ok;

    auto publish_ok_out = PublishOk(
      [](PublishOk& msg) {
          if (msg.filter_type == FilterType::kAbsoluteStart || msg.filter_type == FilterType::kAbsoluteRange) {
              msg.group_0 = std::make_optional<PublishOk::Group_0>();
          }
      },
      [](PublishOk& msg) {
          if (msg.filter_type == FilterType::kAbsoluteRange) {
              msg.group_1 = std::make_optional<PublishOk::Group_1>();
          }
      });

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kPublishOk), publish_ok_out));
    CHECK_EQ(publish_ok.request_id, publish_ok_out.request_id);
    CHECK_EQ(publish_ok.forward, publish_ok_out.forward);
    CHECK_EQ(publish_ok.subscriber_priority, publish_ok_out.subscriber_priority);
    CHECK_EQ(publish_ok.group_order, publish_ok_out.group_order);
    CHECK_EQ(publish_ok.filter_type, publish_ok_out.filter_type);
    CHECK_EQ(publish_ok.parameters, publish_ok_out.parameters);
}

TEST_CASE("PublishOk (with optional fields) Message encode/decode")
{
    Bytes buffer;

    auto group_0 = std::make_optional<PublishOk::Group_0>();
    group_0->start = { 100, 200 };

    auto group_1 = std::make_optional<PublishOk::Group_1>();
    group_1->endgroup = 300;

    auto publish_ok = PublishOk(
      0x1234, true, 0x10, GroupOrder::kAscending, FilterType::kAbsoluteRange, nullptr, group_0, nullptr, group_1, {});

    buffer << publish_ok;

    auto publish_ok_out = PublishOk(
      [](PublishOk& msg) {
          if (msg.filter_type == FilterType::kAbsoluteStart || msg.filter_type == FilterType::kAbsoluteRange) {
              msg.group_0 = std::make_optional<PublishOk::Group_0>();
          }
      },
      [](PublishOk& msg) {
          if (msg.filter_type == FilterType::kAbsoluteRange) {
              msg.group_1 = std::make_optional<PublishOk::Group_1>();
          }
      });

    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kPublishOk), publish_ok_out));
    CHECK_EQ(publish_ok.request_id, publish_ok_out.request_id);
    CHECK_EQ(publish_ok.forward, publish_ok_out.forward);
    CHECK_EQ(publish_ok.subscriber_priority, publish_ok_out.subscriber_priority);
    CHECK_EQ(publish_ok.group_order, publish_ok_out.group_order);
    CHECK_EQ(publish_ok.filter_type, publish_ok_out.filter_type);
    CHECK_EQ(publish_ok.group_0.has_value(), publish_ok_out.group_0.has_value());
    CHECK_EQ(publish_ok.group_0->start.group, publish_ok_out.group_0->start.group);
    CHECK_EQ(publish_ok.group_0->start.object, publish_ok_out.group_0->start.object);
    CHECK_EQ(publish_ok.group_1.has_value(), publish_ok_out.group_1.has_value());
    CHECK_EQ(publish_ok.group_1->endgroup, publish_ok_out.group_1->endgroup);
    CHECK_EQ(publish_ok.parameters, publish_ok_out.parameters);
}

TEST_CASE("PublishError Message encode/decode")
{
    Bytes buffer;

    auto publish_error = PublishError(0x1234, 0x01, Bytes{ 0x1, 0x2, 0x3 });
    buffer << publish_error;

    PublishError publish_error_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kPublishError), publish_error_out));
    CHECK_EQ(publish_error.request_id, publish_error_out.request_id);
    CHECK_EQ(publish_error.error_code, publish_error_out.error_code);
    CHECK_EQ(publish_error.error_reason, publish_error_out.error_reason);
}

using TestKVP64 = KeyValuePair<std::uint64_t>;
enum class ExampleEnum : std::uint64_t
{
    kOdd = 1,
    kEven = 2,
};
using TestKVPEnum = KeyValuePair<ExampleEnum>;
Bytes
KVP64(const std::uint64_t type, const Bytes& value)
{
    TestKVP64 test;
    test.type = type;
    test.value = value;
    Bytes buffer;
    buffer << test;
    return buffer;
}
Bytes
KVPEnum(const ExampleEnum type, const Bytes& value)
{
    TestKVPEnum test;
    test.type = type;
    test.value = value;
    Bytes buffer;
    buffer << test;
    return buffer;
}

TEST_CASE("Key Value Pair encode/decode")
{
    auto value = Bytes(sizeof(std::uint64_t));
    constexpr std::uint64_t one = 1;
    std::memcpy(value.data(), &one, value.size());
    {
        CAPTURE("UINT64_T");
        {
            CAPTURE("EVEN");
            std::size_t type = 2;
            Bytes serialized = KVP64(type, value);
            CHECK_EQ(serialized.size(), 2); // Minimal size, 1 byte for type and 1 byte for value.
            TestKVP64 out;
            serialized >> out;
            CHECK_EQ(out.type, type);
            std::uint64_t reconstructed_value = 0;
            std::memcpy(&reconstructed_value, out.value.data(), out.value.size());
            CHECK_EQ(reconstructed_value, one);
        }
        {
            CAPTURE("ODD");
            std::size_t type = 1;
            Bytes serialized = KVP64(type, value);
            CHECK_EQ(serialized.size(),
                     value.size() + 1 + 1); // 1 byte for type, 1 byte for length, and the value bytes.
            TestKVP64 out;
            serialized >> out;
            CHECK_EQ(out.type, type);
            CHECK_EQ(out.value, value);
        }
    }
    {
        CAPTURE("ENUM");
        {
            CAPTURE("EVEN");
            auto type = ExampleEnum::kEven;
            Bytes serialized = KVPEnum(type, value);
            CHECK_EQ(serialized.size(), 2); // Minimal size, 1 byte for type and 1 byte for value.
            TestKVPEnum out;
            serialized >> out;
            CHECK_EQ(out.type, type);
            std::uint64_t reconstructed_value = 0;
            std::memcpy(&reconstructed_value, out.value.data(), out.value.size());
            CHECK_EQ(reconstructed_value, one);
        }
        {
            CAPTURE("ODD");
            auto type = ExampleEnum::kOdd;
            Bytes serialized = KVPEnum(type, value);
            CHECK_EQ(serialized.size(),
                     value.size() + 1 + 1); // 1 byte for type, 1 byte for length, and the value bytes.
            TestKVPEnum out;
            serialized >> out;
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
        buffer << kvp;
        KeyValuePair<std::uint64_t> out;
        buffer >> out;
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
