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

static Bytes
FromASCII(const std::string& ascii)
{
    return std::vector<uint8_t>(ascii.begin(), ascii.end());
}

const Bytes kTrackNamespaceConf = FromASCII("moqt://conf.example.com/conf/1");
const Bytes kTrackNameAliceVideo = FromASCII("alice/video");
const UintVar kTrackAliasAliceVideo{ 0xA11CE };
const Extensions kExampleExtensions = { { 0x1, { 0x1, 0x2 } } };
const std::optional<Extensions> kOptionalExtensions = kExampleExtensions;

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
VerifyCtrl(std::vector<uint8_t>& buffer, uint64_t message_type, T& message, [[maybe_unused]] size_t slice_depth = 1)
{
    StreamBuffer<uint8_t> in_buffer;
    in_buffer.Push(buffer);

    auto length = in_buffer.DecodeUintV();
    CHECK(length.has_value());
    CHECK_EQ(*length, in_buffer.Size());

    auto msg_type = in_buffer.DecodeUintV();
    CHECK(msg_type.has_value());
    CHECK_EQ(*msg_type, message_type);

    return in_buffer >> message;
}

TEST_CASE("AnnounceOk Message encode/decode")
{
    Bytes buffer;

    auto announce_ok = MoqAnnounceOk{};
    announce_ok.track_namespace = kTrackNamespaceConf;
    buffer << announce_ok;

    MoqAnnounceOk announce_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::ANNOUNCE_OK), announce_ok_out));
    CHECK_EQ(kTrackNamespaceConf, announce_ok_out.track_namespace);
}

TEST_CASE("Announce Message encode/decode")
{
    Bytes buffer;

    auto announce = MoqAnnounce{};
    announce.track_namespace = kTrackNamespaceConf;
    announce.params = {};
    buffer << announce;

    MoqAnnounce announce_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::ANNOUNCE), announce_out));
    CHECK_EQ(kTrackNamespaceConf, announce_out.track_namespace);
    CHECK_EQ(0, announce_out.params.size());
}

TEST_CASE("Unannounce Message encode/decode")
{
    Bytes buffer;

    auto unannounce = MoqUnannounce{};
    unannounce.track_namespace = kTrackNamespaceConf;
    buffer << unannounce;

    MoqAnnounceOk announce_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::UNANNOUNCE), announce_ok_out));
    CHECK_EQ(kTrackNamespaceConf, announce_ok_out.track_namespace);
}

TEST_CASE("AnnounceError Message encode/decode")
{
    Bytes buffer;

    auto announce_err = MoqAnnounceError{};
    announce_err.track_namespace = kTrackNamespaceConf;
    announce_err.err_code = 0x1234;
    announce_err.reason_phrase = Bytes{ 0x1, 0x2, 0x3 };
    buffer << announce_err;

    MoqAnnounceError announce_err_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::ANNOUNCE_ERROR), announce_err_out));
    CHECK_EQ(kTrackNamespaceConf, announce_err_out.track_namespace);
    CHECK_EQ(announce_err.err_code, announce_err_out.err_code);
    CHECK_EQ(announce_err.reason_phrase, announce_err_out.reason_phrase);
}

TEST_CASE("AnnounceCancel Message encode/decode")
{
    Bytes buffer;

    auto announce_cancel = MoqAnnounceCancel{};
    announce_cancel.track_namespace = kTrackNamespaceConf;
    buffer << announce_cancel;

    MoqAnnounceCancel announce_cancel_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::ANNOUNCE_CANCEL), announce_cancel_out));
    CHECK_EQ(kTrackNamespaceConf, announce_cancel_out.track_namespace);
}

TEST_CASE("Subscribe (LatestObject) Message encode/decode")
{
    Bytes buffer;

    auto subscribe = MoqSubscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::LatestObject;

    buffer << subscribe;

    MoqSubscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
}

TEST_CASE("Subscribe (LatestGroup) Message encode/decode")
{
    Bytes buffer;

    auto subscribe = MoqSubscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::LatestGroup;

    buffer << subscribe;

    MoqSubscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
}

TEST_CASE("Subscribe (AbsoluteStart) Message encode/decode")
{
    Bytes buffer;

    auto subscribe = MoqSubscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::AbsoluteStart;
    subscribe.start_group = 0x1000;
    subscribe.start_object = 0xFF;

    buffer << subscribe;

    MoqSubscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribe.start_group, subscribe_out.start_group);
    CHECK_EQ(subscribe.start_object, subscribe_out.start_object);
}

TEST_CASE("Subscribe (AbsoluteRange) Message encode/decode")
{
    Bytes buffer;

    auto subscribe = MoqSubscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::AbsoluteRange;
    subscribe.start_group = 0x1000;
    subscribe.start_object = 0x1;
    subscribe.end_group = 0xFFF;
    subscribe.end_object = 0xFF;

    buffer << subscribe;

    MoqSubscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribe.start_group, subscribe_out.start_group);
    CHECK_EQ(subscribe.start_object, subscribe_out.start_object);
    CHECK_EQ(subscribe.end_group, subscribe_out.end_group);
    CHECK_EQ(subscribe.end_object, subscribe_out.end_object);
}

TEST_CASE("Subscribe (Params) Message encode/decode")
{
    Bytes buffer;
    MoqParameter param;
    param.type = static_cast<uint64_t>(ParameterType::AuthorizationInfo), param.length = 0x2;
    param.value = { 0x1, 0x2 };

    auto subscribe = MoqSubscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::LatestObject;
    subscribe.track_params.push_back(param);
    buffer << subscribe;

    MoqSubscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribe.track_params.size(), subscribe_out.track_params.size());
    CHECK_EQ(subscribe.track_params[0].type, subscribe_out.track_params[0].type);
    CHECK_EQ(subscribe.track_params[0].length, subscribe_out.track_params[0].length);
    CHECK_EQ(subscribe.track_params[0].value, subscribe_out.track_params[0].value);
}

TEST_CASE("Subscribe (Params - 2) Message encode/decode")
{
    Bytes buffer;
    MoqParameter param1;
    param1.type = static_cast<uint64_t>(ParameterType::AuthorizationInfo);
    param1.length = 0x2;
    param1.value = { 0x1, 0x2 };

    MoqParameter param2;
    param2.type = static_cast<uint64_t>(ParameterType::AuthorizationInfo);
    param2.length = 0x3;
    param2.value = { 0x1, 0x2, 0x3 };

    auto subscribe = MoqSubscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::LatestObject;
    subscribe.track_params.push_back(param1);
    subscribe.track_params.push_back(param2);
    buffer << subscribe;

    MoqSubscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribe.track_params.size(), subscribe_out.track_params.size());
    CHECK_EQ(subscribe.track_params[0].type, subscribe_out.track_params[0].type);
    CHECK_EQ(subscribe.track_params[0].length, subscribe_out.track_params[0].length);
    CHECK_EQ(subscribe.track_params[0].value, subscribe_out.track_params[0].value);

    CHECK_EQ(subscribe.track_params[1].type, subscribe_out.track_params[1].type);
    CHECK_EQ(subscribe.track_params[1].length, subscribe_out.track_params[1].length);
    CHECK_EQ(subscribe.track_params[1].value, subscribe_out.track_params[1].value);
}

MoqSubscribe
GenerateSubscribe(FilterType filter,
                  size_t num_params = 0,
                  uint64_t sg = 0,
                  uint64_t so = 0,
                  uint64_t eg = 0,
                  uint64_t eo = 0)
{
    MoqSubscribe out;
    out.subscribe_id = 0xABCD;
    out.track_alias = uint64_t(kTrackAliasAliceVideo);
    out.track_namespace = kTrackNamespaceConf;
    out.track_name = kTrackNameAliceVideo;
    out.filter_type = filter;
    switch (filter) {
        case FilterType::LatestObject:
        case FilterType::LatestGroup:
            break;
        case FilterType::AbsoluteStart:
            out.start_group = sg;
            out.start_object = so;
            break;
        case FilterType::AbsoluteRange:
            out.start_group = sg;
            out.start_object = so;
            out.end_group = eg;
            out.end_object = eo;
            break;
        default:
            break;
    }

    while (num_params > 0) {
        MoqParameter param1;
        param1.type = static_cast<uint64_t>(ParameterType::AuthorizationInfo);
        param1.length = 0x2;
        param1.value = { 0x1, 0x2 };
        out.track_params.push_back(param1);
        num_params--;
    }
    return out;
}

TEST_CASE("Subscribe (Combo) Message encode/decode")
{
    auto subscribes = std::vector<MoqSubscribe>{
        GenerateSubscribe(FilterType::LatestObject),
        GenerateSubscribe(FilterType::LatestGroup),
        GenerateSubscribe(FilterType::LatestObject, 1),
        GenerateSubscribe(FilterType::LatestGroup, 2),
        GenerateSubscribe(FilterType::AbsoluteStart, 0, 0x100, 0x2),
        GenerateSubscribe(FilterType::AbsoluteStart, 2, 0x100, 0x2),
        GenerateSubscribe(FilterType::AbsoluteRange, 0, 0x100, 0x2, 0x500, 0x2),
        GenerateSubscribe(FilterType::AbsoluteRange, 2, 0x100, 0x2, 0x500, 0x2),
    };

    for (size_t i = 0; i < subscribes.size(); i++) {
        Bytes buffer;
        buffer << subscribes[i];
        MoqSubscribe subscribe_out;
        CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE), subscribe_out));
        CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
        CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
        CHECK_EQ(subscribes[i].subscribe_id, subscribe_out.subscribe_id);
        CHECK_EQ(subscribes[i].track_alias, subscribe_out.track_alias);
        CHECK_EQ(subscribes[i].filter_type, subscribe_out.filter_type);
        CHECK_EQ(subscribes[i].track_params.size(), subscribe_out.track_params.size());
        for (size_t j = 0; j < subscribes[i].track_params.size(); j++) {
            CHECK_EQ(subscribes[i].track_params[j].type, subscribe_out.track_params[j].type);
            CHECK_EQ(subscribes[i].track_params[j].length, subscribe_out.track_params[j].length);
            CHECK_EQ(subscribes[i].track_params[j].value, subscribe_out.track_params[j].value);
        }
    }
}

TEST_CASE("SubscribeOk Message encode/decode")
{
    Bytes buffer;

    auto subscribe_ok = MoqSubscribeOk{};
    subscribe_ok.subscribe_id = 0x1;
    subscribe_ok.expires = 0x100;
    subscribe_ok.content_exists = false;
    buffer << subscribe_ok;

    MoqSubscribeOk subscribe_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_OK), subscribe_ok_out));
    CHECK_EQ(subscribe_ok.subscribe_id, subscribe_ok_out.subscribe_id);
    CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
    CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
}

TEST_CASE("SubscribeOk (content-exists) Message encode/decode")
{
    Bytes buffer;

    auto subscribe_ok = MoqSubscribeOk{};
    subscribe_ok.subscribe_id = 0x1;
    subscribe_ok.expires = 0x100;
    subscribe_ok.content_exists = true;
    subscribe_ok.largest_group = 0x1000;
    subscribe_ok.largest_object = 0xff;
    buffer << subscribe_ok;

    MoqSubscribeOk subscribe_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_OK), subscribe_ok_out));
    CHECK_EQ(subscribe_ok.subscribe_id, subscribe_ok_out.subscribe_id);
    CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
    CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
    CHECK_EQ(subscribe_ok.largest_group, subscribe_ok_out.largest_group);
    CHECK_EQ(subscribe_ok.largest_object, subscribe_ok_out.largest_object);
}

TEST_CASE("Error  Message encode/decode")
{
    Bytes buffer;

    auto subscribe_err = MoqSubscribeError{};
    subscribe_err.subscribe_id = 0x1;
    subscribe_err.err_code = 0;
    subscribe_err.reason_phrase = Bytes{ 0x0, 0x1 };
    subscribe_err.track_alias = uint64_t(kTrackAliasAliceVideo);
    buffer << subscribe_err;

    MoqSubscribeError subscribe_err_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_ERROR), subscribe_err_out));
    CHECK_EQ(subscribe_err.subscribe_id, subscribe_err_out.subscribe_id);
    CHECK_EQ(subscribe_err.err_code, subscribe_err_out.err_code);
    CHECK_EQ(subscribe_err.reason_phrase, subscribe_err_out.reason_phrase);
    CHECK_EQ(subscribe_err.track_alias, subscribe_err_out.track_alias);
}

TEST_CASE("Unsubscribe  Message encode/decode")
{
    Bytes buffer;

    auto unsubscribe = MoqUnsubscribe{};
    unsubscribe.subscribe_id = 0x1;
    buffer << unsubscribe;

    MoqUnsubscribe unsubscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::UNSUBSCRIBE), unsubscribe_out));
    CHECK_EQ(unsubscribe.subscribe_id, unsubscribe_out.subscribe_id);
}

TEST_CASE("SubscribeDone  Message encode/decode")
{
    Bytes buffer;

    auto subscribe_done = MoqSubscribeDone{};
    subscribe_done.subscribe_id = 0x1;
    subscribe_done.status_code = 0x0;
    subscribe_done.reason_phrase = Bytes{ 0x0 };
    subscribe_done.content_exists = false;

    buffer << subscribe_done;

    MoqSubscribeDone subscribe_done_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_DONE), subscribe_done_out));
    CHECK_EQ(subscribe_done.subscribe_id, subscribe_done_out.subscribe_id);
    CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
    CHECK_EQ(subscribe_done.reason_phrase, subscribe_done_out.reason_phrase);
    CHECK_EQ(subscribe_done.content_exists, subscribe_done_out.content_exists);
}

TEST_CASE("SubscribeDone (content-exists)  Message encode/decode")
{
    Bytes buffer;

    auto subscribe_done = MoqSubscribeDone{};
    subscribe_done.subscribe_id = 0x1;
    subscribe_done.status_code = 0x0;
    subscribe_done.reason_phrase = Bytes{ 0x0 };
    subscribe_done.content_exists = true;
    subscribe_done.final_group_id = 0x1111;
    subscribe_done.final_object_id = 0xff;

    buffer << subscribe_done;

    MoqSubscribeDone subscribe_done_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SUBSCRIBE_DONE), subscribe_done_out));
    CHECK_EQ(subscribe_done.subscribe_id, subscribe_done_out.subscribe_id);
    CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
    CHECK_EQ(subscribe_done.reason_phrase, subscribe_done_out.reason_phrase);
    CHECK_EQ(subscribe_done.content_exists, subscribe_done_out.content_exists);
    CHECK_EQ(subscribe_done.final_group_id, subscribe_done_out.final_group_id);
    CHECK_EQ(subscribe_done.final_object_id, subscribe_done_out.final_object_id);
}

TEST_CASE("ClientSetup  Message encode/decode")
{
    Bytes buffer;
    const std::string endpoint_id = "client test";
    auto client_setup = MoqClientSetup{};
    client_setup.num_versions = 2;
    client_setup.supported_versions = { 0x1000, 0x2000 };
    client_setup.role_parameter.type = static_cast<uint64_t>(ParameterType::Role);
    client_setup.role_parameter.length = 0x1;
    client_setup.role_parameter.value = { 0xFF };
    client_setup.endpoint_id_parameter.value.assign(endpoint_id.begin(), endpoint_id.end());

    buffer << client_setup;

    MoqClientSetup client_setup_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::CLIENT_SETUP), client_setup_out));
    CHECK_EQ(client_setup.supported_versions, client_setup_out.supported_versions);
    CHECK_EQ(client_setup.role_parameter.value, client_setup_out.role_parameter.value);
    CHECK_EQ(client_setup.endpoint_id_parameter.value, client_setup_out.endpoint_id_parameter.value);
}

TEST_CASE("ServerSetup  Message encode/decode")
{
    Bytes buffer;
    const std::string endpoint_id = "server_test";
    auto server_setup = MoqServerSetup{};
    server_setup.selection_version = { 0x1000 };
    server_setup.role_parameter.type = static_cast<uint64_t>(ParameterType::Role);
    server_setup.role_parameter.length = 0x1;
    server_setup.role_parameter.value = { 0xFF };
    server_setup.endpoint_id_parameter.value.assign(endpoint_id.begin(), endpoint_id.end());

    buffer << server_setup;

    MoqServerSetup server_setup_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::SERVER_SETUP), server_setup_out));
    CHECK_EQ(server_setup.selection_version, server_setup_out.selection_version);
    CHECK_EQ(server_setup.role_parameter.value, server_setup.role_parameter.value);
    CHECK_EQ(server_setup.endpoint_id_parameter.value, server_setup_out.endpoint_id_parameter.value);
}

static void
ObjectStreamEncodeDecode(bool extensions)
{
    Bytes buffer;
    auto object_stream = MoqObjectStream{};
    object_stream.subscribe_id = 0x100;
    object_stream.track_alias = uint64_t(kTrackAliasAliceVideo);
    object_stream.group_id = 0x1000;
    object_stream.object_id = 0xFF;
    object_stream.priority = 0xA;
    object_stream.extensions = extensions ? kOptionalExtensions : std::nullopt;
    object_stream.payload = { 0x1, 0x2, 0x3, 0x5, 0x6 };

    buffer << object_stream;

    MoqObjectStream object_stream_out;
    CHECK(Verify(buffer, static_cast<uint64_t>(MoqMessageType::OBJECT_STREAM), object_stream_out));
    CHECK_EQ(object_stream.subscribe_id, object_stream_out.subscribe_id);
    CHECK_EQ(object_stream.track_alias, object_stream_out.track_alias);
    CHECK_EQ(object_stream.group_id, object_stream_out.group_id);
    CHECK_EQ(object_stream.object_id, object_stream_out.object_id);
    CHECK_EQ(object_stream.priority, object_stream_out.priority);
    CHECK_EQ(object_stream.extensions, object_stream_out.extensions);
    CHECK_EQ(object_stream.payload, object_stream_out.payload);
}

TEST_CASE("ObjectStream  Message encode/decode")
{
    ObjectStreamEncodeDecode(true);
    ObjectStreamEncodeDecode(false);
}

static void
ObjectDatagramEncodeDecode(bool extensions)
{
    Bytes buffer;
    auto object_datagram = MoqObjectDatagram{};
    object_datagram.subscribe_id = 0x100;
    object_datagram.track_alias = uint64_t(kTrackAliasAliceVideo);
    object_datagram.group_id = 0x1000;
    object_datagram.object_id = 0xFF;
    object_datagram.priority = 0xA;
    object_datagram.extensions = extensions ? kOptionalExtensions : std::nullopt;
    object_datagram.payload = { 0x1, 0x2, 0x3, 0x5, 0x6 };

    buffer << object_datagram;

    MoqObjectStream object_datagram_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::OBJECT_DATAGRAM), object_datagram_out));
    CHECK_EQ(object_datagram.subscribe_id, object_datagram_out.subscribe_id);
    CHECK_EQ(object_datagram.track_alias, object_datagram_out.track_alias);
    CHECK_EQ(object_datagram.group_id, object_datagram_out.group_id);
    CHECK_EQ(object_datagram.object_id, object_datagram_out.object_id);
    CHECK_EQ(object_datagram.priority, object_datagram_out.priority);
    CHECK_EQ(object_datagram.extensions, object_datagram_out.extensions);
    CHECK_EQ(object_datagram.payload, object_datagram_out.payload);
}

TEST_CASE("ObjectDatagram  Message encode/decode")
{
    ObjectDatagramEncodeDecode(false);
    ObjectDatagramEncodeDecode(true);
}

static void
StreamPerGroupObjectEncodeDecode(bool extensions)
{
    Bytes buffer;
    auto hdr_grp = MoqStreamHeaderGroup{};
    hdr_grp.subscribe_id = 0x100;
    hdr_grp.track_alias = uint64_t(kTrackAliasAliceVideo);
    hdr_grp.group_id = 0x1000;
    hdr_grp.priority = 0xA;

    buffer << hdr_grp;

    MoqStreamHeaderGroup hdr_group_out;
    CHECK(Verify(buffer, static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_GROUP), hdr_group_out));
    CHECK_EQ(hdr_grp.subscribe_id, hdr_group_out.subscribe_id);
    CHECK_EQ(hdr_grp.track_alias, hdr_group_out.track_alias);
    CHECK_EQ(hdr_grp.group_id, hdr_group_out.group_id);

    // stream all the objects
    buffer.clear();
    auto objects = std::vector<MoqStreamGroupObject>{};
    // send 10 objects
    for (size_t i = 0; i < 1000; i++) {
        auto obj = MoqStreamGroupObject{};
        obj.object_id = i;
        obj.extensions = extensions ? kOptionalExtensions : std::nullopt;
        obj.payload = { 0x1, 0x2, 0x3, 0x4, 0x5 };
        objects.push_back(obj);
        buffer << obj;
    }

    auto obj_out = MoqStreamGroupObject{};
    size_t object_count = 0;
    StreamBuffer<uint8_t> in_buffer;
    for (size_t i = 0; i < buffer.size(); i++) {
        in_buffer.Push(buffer.at(i));
        bool done;
        done = in_buffer >> obj_out;
        if (done) {
            CHECK_EQ(obj_out.object_id, objects[object_count].object_id);
            CHECK_EQ(obj_out.extensions, objects[object_count].extensions);
            CHECK_EQ(obj_out.payload, objects[object_count].payload);
            // got one object
            object_count++;
            obj_out = MoqStreamGroupObject{};
            in_buffer.Pop(in_buffer.Size());
        }
    }

    CHECK_EQ(object_count, 1000);
}

TEST_CASE("StreamPerGroup Object  Message encode/decode")
{
    StreamPerGroupObjectEncodeDecode(false);
    StreamPerGroupObjectEncodeDecode(true);
}

static void
StreamPerTrackObjectEncodeDecode(bool extensions)
{
    Bytes buffer;
    auto hdr = MoqStreamHeaderTrack{};
    hdr.subscribe_id = 0x100;
    hdr.track_alias = uint64_t(kTrackAliasAliceVideo);
    hdr.priority = 0xA;

    buffer << hdr;

    MoqStreamHeaderTrack hdr_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_TRACK), hdr_out));
    CHECK_EQ(hdr_out.subscribe_id, hdr_out.subscribe_id);
    CHECK_EQ(hdr_out.track_alias, hdr_out.track_alias);
    CHECK_EQ(hdr_out.priority, hdr_out.priority);

    // stream all the objects
    buffer.clear();
    auto objects = std::vector<MoqStreamTrackObject>{};
    // send 10 objects
    for (size_t i = 0; i < 1000; i++) {
        auto obj = MoqStreamTrackObject{};
        if (i % 10 == 0) {
            obj.group_id = i;
            obj.object_id = 0;
        } else {
            obj.object_id = i;
        }

        obj.extensions = extensions ? kOptionalExtensions : std::nullopt;
        obj.payload = { 0x1, 0x2, 0x3, 0x4, 0x5 };
        objects.push_back(obj);
        buffer << obj;
    }

    auto obj_out = MoqStreamTrackObject{};
    size_t object_count = 0;
    StreamBuffer<uint8_t> in_buffer;
    for (size_t i = 0; i < buffer.size(); i++) {
        in_buffer.Push(buffer.at(i));
        bool done;
        done = in_buffer >> obj_out;
        if (done) {
            CHECK_EQ(obj_out.group_id, objects[object_count].group_id);
            CHECK_EQ(obj_out.object_id, objects[object_count].object_id);
            CHECK_EQ(obj_out.extensions, objects[object_count].extensions);
            CHECK_EQ(obj_out.payload, objects[object_count].payload);
            // got one object
            object_count++;
            obj_out = MoqStreamTrackObject{};
            in_buffer.Pop(in_buffer.Size());
        }
    }

    CHECK_EQ(object_count, 1000);
}

TEST_CASE("StreamPerTrack Object  Message encode/decode")
{
    StreamPerTrackObjectEncodeDecode(false);
    StreamPerTrackObjectEncodeDecode(true);
}

TEST_CASE("MoqGoaway Message encode/decode")
{
    Bytes buffer;

    auto goaway = MoqGoaway{};
    goaway.new_session_uri = FromASCII("go.away.now.no.return");
    buffer << goaway;

    MoqGoaway goaway_out{};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(MoqMessageType::GOAWAY), goaway_out));
    CHECK_EQ(FromASCII("go.away.now.no.return"), goaway_out.new_session_uri);
}
