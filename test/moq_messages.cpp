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
const Extensions kExampleExtensions = { { 0x1, { 0x1, 0x2 } }, { 0x2, { 0, 0, 0, 0, 0, 0x3, 0x2, 0x1 } } };
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

BytesSpan
operator>>(BytesSpan buffer, uint64_t& value)
{
    UintVar value_uv(buffer);
    value = static_cast<uint64_t>(value_uv);
    return buffer.subspan(value_uv.Size());
}

template<typename T>
bool
VerifyCtrl(BytesSpan buffer, uint64_t message_type, T& message)
{
    uint64_t msg_type = 0;
    uint64_t length = 0;
    buffer = buffer >> msg_type >> length;

    CHECK_EQ(msg_type, message_type);
    CHECK_EQ(length, buffer.size());

    buffer = buffer >> message;

    return true;
}

TEST_CASE("AnnounceOk Message encode/decode")
{
    Bytes buffer;

    auto announce_ok = AnnounceOk{};
    announce_ok.track_namespace = kTrackNamespaceConf;
    buffer << announce_ok;

    AnnounceOk announce_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kAnnounceOk), announce_ok_out));
    CHECK_EQ(kTrackNamespaceConf, announce_ok_out.track_namespace);
}

TEST_CASE("Announce Message encode/decode")
{
    Bytes buffer;

    auto announce = Announce{};
    announce.track_namespace = kTrackNamespaceConf;
    announce.params = {};
    buffer << announce;

    Announce announce_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kAnnounce), announce_out));
    CHECK_EQ(kTrackNamespaceConf, announce_out.track_namespace);
    CHECK_EQ(0, announce_out.params.size());
}

TEST_CASE("Unannounce Message encode/decode")
{
    Bytes buffer;

    auto unannounce = Unannounce{};
    unannounce.track_namespace = kTrackNamespaceConf;
    buffer << unannounce;

    AnnounceOk announce_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kUnannounce), announce_ok_out));
    CHECK_EQ(kTrackNamespaceConf, announce_ok_out.track_namespace);
}

TEST_CASE("AnnounceError Message encode/decode")
{
    Bytes buffer;

    auto announce_err = AnnounceError{};
    announce_err.track_namespace = kTrackNamespaceConf;
    announce_err.err_code = 0x1234;
    announce_err.reason_phrase = Bytes{ 0x1, 0x2, 0x3 };
    buffer << announce_err;

    AnnounceError announce_err_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kAnnounceError), announce_err_out));
    CHECK_EQ(kTrackNamespaceConf, announce_err_out.track_namespace);
    CHECK_EQ(announce_err.err_code, announce_err_out.err_code);
    CHECK_EQ(announce_err.reason_phrase, announce_err_out.reason_phrase);
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
    CHECK_EQ(announce_cancel.reason_phrase, announce_cancel_out.reason_phrase);
}

TEST_CASE("Subscribe (kLatestObject) Message encode/decode")
{
    Bytes buffer;

    auto subscribe = Subscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.priority = 0x10;
    subscribe.group_order = GroupOrder::kDescending;
    subscribe.filter_type = FilterType::kLatestObject;

    buffer << subscribe;

    Subscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.priority, subscribe_out.priority);
    CHECK_EQ(subscribe.group_order, subscribe_out.group_order);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
}

TEST_CASE("Subscribe (kLatestGroup) Message encode/decode")
{
    Bytes buffer;

    auto subscribe = Subscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::kLatestGroup;

    buffer << subscribe;

    Subscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
}

TEST_CASE("Subscribe (kAbsoluteStart) Message encode/decode")
{
    Bytes buffer;

    auto subscribe = Subscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::kAbsoluteStart;
    subscribe.start_group = 0x1000;
    subscribe.start_object = 0xFF;

    buffer << subscribe;

    Subscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribe.start_group, subscribe_out.start_group);
    CHECK_EQ(subscribe.start_object, subscribe_out.start_object);
}

TEST_CASE("Subscribe (kAbsoluteRange) Message encode/decode")
{
    Bytes buffer;

    auto subscribe = Subscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::kAbsoluteRange;
    subscribe.start_group = 0x1000;
    subscribe.start_object = 0x1;
    subscribe.end_group = 0xFFF;

    buffer << subscribe;

    Subscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
    CHECK_EQ(kTrackNamespaceConf, subscribe_out.track_namespace);
    CHECK_EQ(kTrackNameAliceVideo, subscribe_out.track_name);
    CHECK_EQ(subscribe.subscribe_id, subscribe_out.subscribe_id);
    CHECK_EQ(subscribe.track_alias, subscribe_out.track_alias);
    CHECK_EQ(subscribe.filter_type, subscribe_out.filter_type);
    CHECK_EQ(subscribe.start_group, subscribe_out.start_group);
    CHECK_EQ(subscribe.start_object, subscribe_out.start_object);
    CHECK_EQ(subscribe.end_group, subscribe_out.end_group);
}

TEST_CASE("Subscribe (Params) Message encode/decode")
{
    Bytes buffer;
    Parameter param;
    param.type = static_cast<uint64_t>(ParameterType::kMaxSubscribeId), param.length = 0x2;
    param.value = { 0x1, 0x2 };

    auto subscribe = Subscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::kLatestObject;
    subscribe.track_params.push_back(param);
    buffer << subscribe;

    Subscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
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
    Parameter param1;
    param1.type = static_cast<uint64_t>(ParameterType::kMaxSubscribeId);
    param1.length = 0x2;
    param1.value = { 0x1, 0x2 };

    Parameter param2;
    param2.type = static_cast<uint64_t>(ParameterType::kMaxSubscribeId);
    param2.length = 0x3;
    param2.value = { 0x1, 0x2, 0x3 };

    auto subscribe = Subscribe{};
    subscribe.subscribe_id = 0x1;
    subscribe.track_alias = uint64_t(kTrackAliasAliceVideo);
    subscribe.track_namespace = kTrackNamespaceConf;
    subscribe.track_name = kTrackNameAliceVideo;
    subscribe.filter_type = FilterType::kLatestObject;
    subscribe.track_params.push_back(param1);
    subscribe.track_params.push_back(param2);
    buffer << subscribe;

    Subscribe subscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
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

Subscribe
GenerateSubscribe(FilterType filter, size_t num_params = 0, uint64_t sg = 0, uint64_t so = 0, uint64_t eg = 0)
{
    Subscribe out;
    out.subscribe_id = 0xABCD;
    out.track_alias = uint64_t(kTrackAliasAliceVideo);
    out.track_namespace = kTrackNamespaceConf;
    out.track_name = kTrackNameAliceVideo;
    out.filter_type = filter;
    switch (filter) {
        case FilterType::kLatestObject:
        case FilterType::kLatestGroup:
            break;
        case FilterType::kAbsoluteStart:
            out.start_group = sg;
            out.start_object = so;
            break;
        case FilterType::kAbsoluteRange:
            out.start_group = sg;
            out.start_object = so;
            out.end_group = eg;
            break;
        default:
            break;
    }

    while (num_params > 0) {
        Parameter param1;
        param1.type = static_cast<uint64_t>(ParameterType::kMaxSubscribeId);
        param1.length = 0x2;
        param1.value = { 0x1, 0x2 };
        out.track_params.push_back(param1);
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
        Subscribe subscribe_out;
        CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribe), subscribe_out));
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

TEST_CASE("SubscribeUpdate Message encode/decode")
{
    Bytes buffer;

    auto subscribe_update = SubscribeUpdate{};
    subscribe_update.subscribe_id = 0x1;
    subscribe_update.start_group = uint64_t(0x1000);
    subscribe_update.start_object = uint64_t(0x100);
    subscribe_update.end_group = uint64_t(0x2000);
    subscribe_update.priority = 0x10;

    buffer << subscribe_update;

    SubscribeUpdate subscribe_update_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeUpdate), subscribe_update_out));
    CHECK_EQ(0x1000, subscribe_update_out.start_group);
    CHECK_EQ(0x100, subscribe_update_out.start_object);
    CHECK_EQ(subscribe_update.subscribe_id, subscribe_update_out.subscribe_id);
    CHECK_EQ(0x2000, subscribe_update_out.end_group);
    CHECK_EQ(subscribe_update.priority, subscribe_update_out.priority);
}

TEST_CASE("SubscribeOk Message encode/decode")
{
    Bytes buffer;

    auto subscribe_ok = SubscribeOk{};
    subscribe_ok.subscribe_id = 0x1;
    subscribe_ok.expires = 0x100;
    subscribe_ok.group_order = 0x0;
    subscribe_ok.content_exists = false;
    buffer << subscribe_ok;

    SubscribeOk subscribe_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeOk), subscribe_ok_out));
    CHECK_EQ(subscribe_ok.subscribe_id, subscribe_ok_out.subscribe_id);
    CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
    CHECK_EQ(subscribe_ok.group_order, subscribe_ok_out.group_order);
    CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
}

TEST_CASE("SubscribeOk (content-exists) Message encode/decode")
{
    Bytes buffer;

    auto subscribe_ok = SubscribeOk{};
    subscribe_ok.subscribe_id = 0x1;
    subscribe_ok.expires = 0x100;
    subscribe_ok.content_exists = true;
    subscribe_ok.largest_group = 0x1000;
    subscribe_ok.largest_object = 0xff;
    buffer << subscribe_ok;

    SubscribeOk subscribe_ok_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeOk), subscribe_ok_out));
    CHECK_EQ(subscribe_ok.subscribe_id, subscribe_ok_out.subscribe_id);
    CHECK_EQ(subscribe_ok.expires, subscribe_ok_out.expires);
    CHECK_EQ(subscribe_ok.content_exists, subscribe_ok_out.content_exists);
    CHECK_EQ(subscribe_ok.largest_group, subscribe_ok_out.largest_group);
    CHECK_EQ(subscribe_ok.largest_object, subscribe_ok_out.largest_object);
}

TEST_CASE("Error  Message encode/decode")
{
    Bytes buffer;

    auto subscribe_err = SubscribeError{};
    subscribe_err.subscribe_id = 0x1;
    subscribe_err.err_code = 0;
    subscribe_err.reason_phrase = Bytes{ 0x0, 0x1 };
    subscribe_err.track_alias = uint64_t(kTrackAliasAliceVideo);
    buffer << subscribe_err;

    SubscribeError subscribe_err_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeError), subscribe_err_out));
    CHECK_EQ(subscribe_err.subscribe_id, subscribe_err_out.subscribe_id);
    CHECK_EQ(subscribe_err.err_code, subscribe_err_out.err_code);
    CHECK_EQ(subscribe_err.reason_phrase, subscribe_err_out.reason_phrase);
    CHECK_EQ(subscribe_err.track_alias, subscribe_err_out.track_alias);
}

TEST_CASE("Unsubscribe  Message encode/decode")
{
    Bytes buffer;

    auto unsubscribe = Unsubscribe{};
    unsubscribe.subscribe_id = 0x1;
    buffer << unsubscribe;

    Unsubscribe unsubscribe_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kUnsubscribe), unsubscribe_out));
    CHECK_EQ(unsubscribe.subscribe_id, unsubscribe_out.subscribe_id);
}

TEST_CASE("SubscribeDone  Message encode/decode")
{
    Bytes buffer;

    auto subscribe_done = SubscribeDone{};
    subscribe_done.subscribe_id = 0x1;
    subscribe_done.status_code = 0x0;
    subscribe_done.stream_count = 0x0;
    subscribe_done.reason_phrase = Bytes{ 0x0 };

    buffer << subscribe_done;

    SubscribeDone subscribe_done_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeDone), subscribe_done_out));
    CHECK_EQ(subscribe_done.subscribe_id, subscribe_done_out.subscribe_id);
    CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
    CHECK_EQ(subscribe_done.stream_count, subscribe_done_out.stream_count);
    CHECK_EQ(subscribe_done.reason_phrase, subscribe_done_out.reason_phrase);
}

TEST_CASE("SubscribeDone (content-exists)  Message encode/decode")
{
    Bytes buffer;

    auto subscribe_done = SubscribeDone{};
    subscribe_done.subscribe_id = 0x1;
    subscribe_done.status_code = 0x0;
    subscribe_done.stream_count = 0x0;
    subscribe_done.reason_phrase = Bytes{ 0x0 };

    buffer << subscribe_done;

    SubscribeDone subscribe_done_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeDone), subscribe_done_out));
    CHECK_EQ(subscribe_done.subscribe_id, subscribe_done_out.subscribe_id);
    CHECK_EQ(subscribe_done.status_code, subscribe_done_out.status_code);
    CHECK_EQ(subscribe_done.stream_count, subscribe_done_out.stream_count);
    CHECK_EQ(subscribe_done.reason_phrase, subscribe_done_out.reason_phrase);
}

TEST_CASE("ClientSetup  Message encode/decode")
{
    Bytes buffer;
    const std::string endpoint_id = "client test";
    auto client_setup = ClientSetup{};
    client_setup.num_versions = 2;
    client_setup.supported_versions = { 0x1000, 0x2000 };
    client_setup.endpoint_id_parameter.value.assign(endpoint_id.begin(), endpoint_id.end());

    buffer << client_setup;

    ClientSetup client_setup_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kClientSetup), client_setup_out));
    CHECK_EQ(client_setup.supported_versions, client_setup_out.supported_versions);
    CHECK_EQ(client_setup.endpoint_id_parameter.value, client_setup_out.endpoint_id_parameter.value);
}

TEST_CASE("ServerSetup  Message encode/decode")
{
    const std::string endpoint_id = "server_test";
    auto server_setup = ServerSetup{};
    server_setup.selection_version = { 0x1000 };
    server_setup.endpoint_id_parameter.value.assign(endpoint_id.begin(), endpoint_id.end());

    Bytes buffer;
    buffer << server_setup;

    ServerSetup server_setup_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kServerSetup), server_setup_out));
    CHECK_EQ(server_setup.selection_version, server_setup_out.selection_version);
    CHECK_EQ(server_setup.endpoint_id_parameter.value, server_setup_out.endpoint_id_parameter.value);
}

static void
ObjectDatagramEncodeDecode(bool extensions)
{
    Bytes buffer;
    auto object_datagram = ObjectDatagram{};
    object_datagram.track_alias = uint64_t(kTrackAliasAliceVideo);
    object_datagram.group_id = 0x1000;
    object_datagram.object_id = 0xFF;
    object_datagram.priority = 0xA;
    object_datagram.extensions = extensions ? kOptionalExtensions : std::nullopt;
    object_datagram.payload = { 0x1, 0x2, 0x3, 0x5, 0x6 };

    buffer << object_datagram;

    ObjectDatagram object_datagram_out;
    StreamBuffer<uint8_t> sbuf;
    sbuf.Push(buffer);

    auto msg_type = sbuf.DecodeUintV();

    CHECK_EQ(msg_type, static_cast<uint64_t>(DataMessageType::kObjectDatagram));

    sbuf >> object_datagram_out;

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
    ObjectDatagramEncodeDecode(false);
    ObjectDatagramEncodeDecode(true);
}

TEST_CASE("ObjectDatagramStatus  Message encode/decode")
{
    Bytes buffer;
    auto object_datagram_status = ObjectDatagramStatus{};
    object_datagram_status.track_alias = uint64_t(kTrackAliasAliceVideo);
    object_datagram_status.group_id = 0x1000;
    object_datagram_status.object_id = 0xFF;
    object_datagram_status.priority = 0xA;
    object_datagram_status.status = quicr::ObjectStatus::kAvailable;

    buffer << object_datagram_status;

    ObjectDatagramStatus object_datagram_status_out;
    CHECK(Verify(buffer, static_cast<uint64_t>(DataMessageType::kObjectDatagramStatus), object_datagram_status_out));
    CHECK_EQ(object_datagram_status.track_alias, object_datagram_status_out.track_alias);
    CHECK_EQ(object_datagram_status.group_id, object_datagram_status_out.group_id);
    CHECK_EQ(object_datagram_status.object_id, object_datagram_status_out.object_id);
    CHECK_EQ(object_datagram_status.priority, object_datagram_status_out.priority);
    CHECK_EQ(object_datagram_status.status, object_datagram_status_out.status);
}

static void
StreamPerSubGroupObjectEncodeDecode(bool extensions, bool empty_payload)
{
    Bytes buffer;
    auto hdr_grp = StreamHeaderSubGroup{};
    hdr_grp.track_alias = uint64_t(kTrackAliasAliceVideo);
    hdr_grp.group_id = 0x1000;
    hdr_grp.subgroup_id = 0x5000;
    hdr_grp.priority = 0xA;

    buffer << hdr_grp;

    StreamHeaderSubGroup hdr_group_out;
    CHECK(Verify(buffer, static_cast<uint64_t>(DataMessageType::kStreamHeaderSubgroup), hdr_group_out));
    CHECK_EQ(hdr_grp.track_alias, hdr_group_out.track_alias);
    CHECK_EQ(hdr_grp.group_id, hdr_group_out.group_id);
    CHECK_EQ(hdr_grp.subgroup_id, hdr_group_out.subgroup_id);

    // stream all the objects
    buffer.clear();
    auto objects = std::vector<StreamSubGroupObject>{};
    // send 10 objects
    for (size_t i = 0; i < 10; i++) {
        auto obj = StreamSubGroupObject{};
        obj.object_id = 0x1234;

        if (empty_payload) {
            obj.object_status = ObjectStatus::kDoesNotExist;
        } else {
            obj.payload = { 0x1, 0x2, 0x3, 0x4, 0x5 };
        }

        obj.extensions = extensions ? kOptionalExtensions : std::nullopt;
        objects.push_back(obj);
        buffer << obj;
    }

    auto obj_out = StreamSubGroupObject{};
    size_t object_count = 0;
    StreamBuffer<uint8_t> in_buffer;
    for (size_t i = 0; i < buffer.size(); i++) {
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
        CHECK_EQ(obj_out.extensions, objects[object_count].extensions);
        // got one object
        object_count++;
        obj_out = {};
        in_buffer.Pop(in_buffer.Size());
    }

    CHECK_EQ(object_count, objects.size());
}

TEST_CASE("StreamPerSubGroup Object  Message encode/decode")
{
    StreamPerSubGroupObjectEncodeDecode(false, true);
    StreamPerSubGroupObjectEncodeDecode(false, false);
    StreamPerSubGroupObjectEncodeDecode(true, true);
    StreamPerSubGroupObjectEncodeDecode(true, false);
}

TEST_CASE("GoAway Message encode/decode")
{
    Bytes buffer;

    auto goaway = GoAway{};
    goaway.new_session_uri = FromASCII("go.away.now.no.return");
    buffer << goaway;

    GoAway goaway_out{};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kGoAway), goaway_out));
    CHECK_EQ(FromASCII("go.away.now.no.return"), goaway_out.new_session_uri);
}

TEST_CASE("Fetch Message encode/decode")
{
    Bytes buffer;

    auto fetch = Fetch{};
    fetch.priority = 1;
    fetch.group_order = GroupOrder::kAscending;
    fetch.fetch_type = FetchType::kStandalone;
    fetch.track_namespace = kTrackNamespaceConf;
    fetch.track_name = kTrackNameAliceVideo;
    fetch.start_group = 0x1000;
    fetch.start_object = 0x0;
    fetch.end_group = 0x2000;
    fetch.end_object = 0x100;
    fetch.params = {};

    buffer << fetch;
    {
        Fetch fetch_out{};
        CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetch), fetch_out));
        CHECK_EQ(fetch.track_namespace, fetch_out.track_namespace);
        CHECK_EQ(fetch.track_name, fetch_out.track_name);
        CHECK_EQ(fetch.priority, fetch_out.priority);
        CHECK_EQ(fetch.group_order, fetch_out.group_order);
        CHECK_EQ(fetch.start_group, fetch_out.start_group);
        CHECK_EQ(fetch.start_object, fetch_out.start_object);
        CHECK_EQ(fetch.end_group, fetch_out.end_group);
        CHECK_EQ(fetch.end_object, fetch_out.end_object);
    }

    fetch.fetch_type = FetchType::kJoiningFetch;
    fetch.joining_subscribe_id = 0x0;
    fetch.preceding_group_offset = 0x0;

    buffer.clear();
    buffer << fetch;
    {
        Fetch fetch_out{};
        CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetch), fetch_out));
        CHECK_EQ(fetch.joining_subscribe_id, fetch_out.joining_subscribe_id);
        CHECK_EQ(fetch.preceding_group_offset, fetch_out.preceding_group_offset);
    }
}

TEST_CASE("FetchOk/Error/Cancel Message encode/decode")
{
    Bytes buffer;

    auto fetch_ok = FetchOk{};
    fetch_ok.subscribe_id = 0x1234;
    fetch_ok.group_order = GroupOrder::kDescending;
    fetch_ok.largest_group = 0x9999;
    fetch_ok.largest_object = 0x9999;

    buffer << fetch_ok;

    FetchOk fetch_ok_out{};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetchOk), fetch_ok_out));
    CHECK_EQ(fetch_ok.subscribe_id, fetch_ok_out.subscribe_id);
    CHECK_EQ(fetch_ok.group_order, fetch_ok_out.group_order);
    CHECK_EQ(fetch_ok.largest_group, fetch_ok_out.largest_group);
    CHECK_EQ(fetch_ok.largest_object, fetch_ok_out.largest_object);

    buffer.clear();
    auto fetch_cancel = FetchCancel{};
    fetch_cancel.subscribe_id = 0x1111;

    buffer << fetch_cancel;

    FetchCancel fetch_cancel_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetchCancel), fetch_cancel_out));
    CHECK_EQ(fetch_cancel.subscribe_id, fetch_cancel_out.subscribe_id);

    buffer.clear();
    auto fetch_error = FetchError{};
    fetch_error.subscribe_id = 0x1111;
    fetch_error.err_code = 0x0;

    buffer << fetch_error;

    FetchError fetch_error_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kFetchError), fetch_error_out));
    CHECK_EQ(fetch_error.subscribe_id, fetch_error_out.subscribe_id);
    CHECK_EQ(fetch_error.err_code, fetch_error_out.err_code);
}

TEST_CASE("SubscribesBlocked Message encode/decode")
{
    Bytes buffer;

    auto sub_blocked = SubscribesBlocked{};
    sub_blocked.max_subscribe_id = std::numeric_limits<uint64_t>::max() >> 2;
    buffer << sub_blocked;

    SubscribesBlocked sub_blocked_out{};
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribesBlocked), sub_blocked_out));
    CHECK_EQ(sub_blocked.max_subscribe_id, sub_blocked_out.max_subscribe_id);
}

TEST_CASE("Subscribe Announces encode/decode")
{
    Bytes buffer;

    auto msg = SubscribeAnnounces{};
    msg.prefix_namespace = TrackNamespace{ "cisco"s, "meetings"s, "video"s, "1080p"s };
    buffer << msg;

    SubscribeAnnounces msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeAnnounces), msg_out));
    CHECK_EQ(msg.prefix_namespace, msg_out.prefix_namespace);
}

TEST_CASE("Subscribe Announces Ok encode/decode")
{
    Bytes buffer;

    auto msg = SubscribeAnnouncesOk{};
    msg.prefix_namespace = TrackNamespace{ "cisco"s, "meetings"s, "video"s, "1080p"s };
    buffer << msg;

    SubscribeAnnouncesOk msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeAnnouncesOk), msg_out));
    CHECK_EQ(msg.prefix_namespace, msg_out.prefix_namespace);
}

TEST_CASE("Unsubscribe Announces encode/decode")
{
    Bytes buffer;

    auto msg = UnsubscribeAnnounces{};
    msg.prefix_namespace = TrackNamespace{ "cisco"s, "meetings"s, "video"s, "1080p"s };
    buffer << msg;

    UnsubscribeAnnounces msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kUnsubscribeAnnounces), msg_out));
    CHECK_EQ(msg.prefix_namespace, msg_out.prefix_namespace);
}

TEST_CASE("Subscribe Announces Error encode/decode")
{
    Bytes buffer;

    auto msg = SubscribeAnnouncesError{};
    msg.prefix_namespace = TrackNamespace{ "cisco"s, "meetings"s, "video"s, "1080p"s };
    msg.error_code = SubscribeAnnouncesErrorCode::kNamespacePrefixUnknown;
    msg.reason_phrase = Bytes{ 0x1, 0x2, 0x3 };
    buffer << msg;

    SubscribeAnnouncesError msg_out;
    CHECK(VerifyCtrl(buffer, static_cast<uint64_t>(ControlMessageType::kSubscribeAnnouncesError), msg_out));
    CHECK_EQ(msg.prefix_namespace, msg_out.prefix_namespace);
    CHECK_EQ(msg.error_code, msg_out.error_code);
    CHECK_EQ(msg.reason_phrase, msg_out.reason_phrase);
}

static void
FetchStreamEncodeDecode(bool extensions, bool empty_payload)
{
    Bytes buffer;
    auto fetch_header = FetchHeader{};
    fetch_header.subscribe_id = 0x1234;

    buffer << fetch_header;

    FetchHeader fetch_header_out;
    CHECK(Verify(buffer, static_cast<uint64_t>(DataMessageType::kFetchHeader), fetch_header_out));
    CHECK_EQ(fetch_header.subscribe_id, fetch_header_out.subscribe_id);

    // stream all the objects
    buffer.clear();
    auto objects = std::vector<FetchObject>{};
    // send 10 objects
    for (size_t i = 0; i < 10; i++) {
        auto obj = FetchObject{};
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

    auto obj_out = FetchObject{};
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
