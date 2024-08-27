/*
*  Copyright (C) 2024
*  Cisco Systems, Inc.
*  All Rights Reserved
*/

#include "moq/detail/transport.h"
#include <sstream>

#define LOGGER_TRACE(logger, ...) if (logger) SPDLOG_LOGGER_TRACE(logger, __VA_ARGS__)
#define LOGGER_DEBUG(logger, ...) if (logger) SPDLOG_LOGGER_DEBUG(logger, __VA_ARGS__)
#define LOGGER_INFO(logger, ...) if (logger) SPDLOG_LOGGER_INFO(logger, __VA_ARGS__)
#define LOGGER_WARN(logger, ...) if (logger) SPDLOG_LOGGER_WARN(logger, __VA_ARGS__)
#define LOGGER_ERROR(logger, ...) if (logger) SPDLOG_LOGGER_ERROR(logger, __VA_ARGS__)
#define LOGGER_CRITICAL(logger, ...) if (logger) SPDLOG_LOGGER_CRITICAL(logger, __VA_ARGS__)

namespace moq {

   using namespace moq::messages;

   Transport::Transport(const ClientConfig& cfg)
     : client_mode_(true)
     , server_config_({})
     , client_config_(cfg)
     , quic_transport_({})
   {
       LOGGER_INFO(logger_, "Created Moq instance in client mode listening on {0}", cfg.connect_uri);
       Init();
   }

   Transport::Transport(const ServerConfig& cfg)
     : client_mode_(false)
     , server_config_(cfg)
     , client_config_({})
     , quic_transport_({})
   {
       LOGGER_INFO(
         logger_, "Created Moq instance in server mode listening on {0}:{1}", cfg.server_bind_ip, cfg.server_port);
       Init();
   }

   void Transport::Init()
   {
       LOGGER_INFO(logger_, "Starting metrics exporter");
   }

   Transport::Status Transport::Start()
   {
       if (client_mode_) {
           TransportRemote relay;
           relay.host_or_ip = client_config_.connect_uri;
        //    relay.port = client_config_.server_port; // TODO: Break out port form connect uri
           relay.proto = TransportProtocol::kQuic;

           quic_transport_ = ITransport::MakeClientTransport(relay, client_config_.transport_config, *this, logger_);

           status_ = Status::kClientConnecting;

           auto conn_id = quic_transport_->Start(nullptr, nullptr);

           LOGGER_INFO(logger_, "Connecting session conn_id: {0}...", conn_id);
           auto [conn_ctx, _] = connections_.try_emplace(conn_id, ConnectionContext{});
           conn_ctx->second.connection_handle = conn_id;

           return status_;
       } else {
           TransportRemote server;
           server.host_or_ip = server_config_.server_bind_ip;
           server.port = server_config_.server_port;
           server.proto = TransportProtocol::kQuic;

           quic_transport_ = ITransport::MakeServerTransport(server, server_config_.transport_config, *this, logger_);

           switch (quic_transport_->Status()) {
               case TransportStatus::kReady:
                   return Status::kReady;
               default:
                   return Status::kNotReady;
           }
       }
   }

   void Transport::SendCtrlMsg(const ConnectionContext& conn_ctx, std::vector<uint8_t>&& data)
   {
       if (not conn_ctx.ctrl_data_ctx_id) {
           CloseConnection(conn_ctx.connection_handle,
                            MoqTerminationReason::PROTOCOL_VIOLATION,
                            "Control bidir stream not created");
           return;
       }

       quic_transport_->Enqueue(conn_ctx.connection_handle,
                           *conn_ctx.ctrl_data_ctx_id,
                           std::move(data),
                           { MethodTraceItem{} },
                           0,
                           2000,
                           0,
                           { true, false, false, false });

   }

   void Transport::SendClientSetup()
   {
       StreamBuffer<uint8_t> buffer;
       auto client_setup = MoqClientSetup{};

       client_setup.num_versions = 1;      // NOTE: Not used for encode, verison vector size is used
       client_setup.supported_versions = { MOQ_VERSION };
       client_setup.role_parameter.type = static_cast<uint64_t>(ParameterType::Role);
       client_setup.role_parameter.length = 0x1; // NOTE: not used for encode, size of value is used
       client_setup.role_parameter.value = { 0x03 };
       client_setup.endpoint_id_parameter.value.assign(client_config_.endpoint_id.begin(),
                                                       client_config_.endpoint_id.end());

       buffer << client_setup;

       auto &conn_ctx = connections_.begin()->second;

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }

   void Transport::SendServerSetup(ConnectionContext& conn_ctx)
   {
       StreamBuffer<uint8_t> buffer;
       auto server_setup = MoqServerSetup{};

       server_setup.selection_version = { conn_ctx.client_version };
       server_setup.role_parameter.type = static_cast<uint64_t>(ParameterType::Role);
       server_setup.role_parameter.length = 0x1; // NOTE: not used for encode, size of value is used
       server_setup.role_parameter.value = { 0x03 };
       server_setup.endpoint_id_parameter.value.assign(server_config_.endpoint_id.begin(),
                                                       server_config_.endpoint_id.end());


       buffer << server_setup;

       LOGGER_DEBUG(logger_, "Sending SERVER_SETUP to conn_id: {0}", conn_ctx.connection_handle);

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }

   void Transport::SendAnnounce(ConnectionContext& conn_ctx, Span<uint8_t const> track_namespace)
   {
       StreamBuffer<uint8_t> buffer;
       auto announce = MoqAnnounce{};

       announce.track_namespace.assign(track_namespace.begin(), track_namespace.end());
       announce.params = {};
       buffer <<  announce;

       LOGGER_DEBUG(logger_, "Sending ANNOUNCE to conn_id: {0}", conn_ctx.connection_handle);

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }

   void Transport::SendAnnounceOk(ConnectionContext& conn_ctx, Span<uint8_t const> track_namespace)
   {
       StreamBuffer<uint8_t> buffer;
       auto announce_ok = MoqAnnounceOk{};

       announce_ok.track_namespace.assign(track_namespace.begin(), track_namespace.end());
       buffer <<  announce_ok;

       LOGGER_DEBUG(logger_, "Sending ANNOUNCE OK to conn_id: {0}", conn_ctx.connection_handle);

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }

   void Transport::SendUnannounce(ConnectionContext& conn_ctx, Span<uint8_t const> track_namespace)
   {
       StreamBuffer<uint8_t> buffer;
       auto unannounce = MoqUnannounce{};

       unannounce.track_namespace.assign(track_namespace.begin(), track_namespace.end());
       buffer <<  unannounce;

       LOGGER_DEBUG(logger_, "Sending UNANNOUNCE to conn_id: {0}", conn_ctx.connection_handle);

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }

   void Transport::SendSubscribe(ConnectionContext& conn_ctx,
                                    uint64_t subscribe_id,
                                    FullTrackName& tfn,
                                    TrackHash th)
   {
       StreamBuffer<uint8_t> buffer;

       auto subscribe  = MoqSubscribe {};
       subscribe.subscribe_id = subscribe_id;
       subscribe.track_alias = th.track_fullname_hash;
       subscribe.track_namespace.assign(tfn.name_space.begin(), tfn.name_space.end());
       subscribe.track_name.assign(tfn.name.begin(), tfn.name.end());
       subscribe.filter_type = FilterType::LatestGroup;
       subscribe.num_params = 0;

       buffer << subscribe;

       LOGGER_DEBUG(logger_,
                    "Sending SUBSCRIBE to conn_id: {0} subscribe_id: {1} track namespace hash: {2} name hash: {3}",
                    conn_ctx.connection_handle,
                    subscribe_id,
                    th.track_namespace_hash,
                    th.track_name_hash);

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }

   void Transport::SendSubscribeOk(ConnectionContext& conn_ctx,
                                       uint64_t subscribe_id,
                                       uint64_t expires,
                                       bool content_exists)
   {
       StreamBuffer<uint8_t> buffer;

       auto subscribe_ok  = MoqSubscribeOk {};
       subscribe_ok.subscribe_id = subscribe_id;
       subscribe_ok.expires = expires;
       subscribe_ok.content_exists = content_exists;
       buffer << subscribe_ok;

       LOGGER_DEBUG(logger_, "Sending SUBSCRIBE OK to conn_id: {0} subscribe_id: {1}",conn_ctx.connection_handle, subscribe_id);

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }

   void Transport::SendSubscribeDone(ConnectionContext& conn_ctx,
                                         uint64_t subscribe_id,
                                         const std::string& reason)
   {
       StreamBuffer<uint8_t> buffer;

       auto subscribe_done  = MoqSubscribeDone {};
       subscribe_done.subscribe_id = subscribe_id;
       subscribe_done.reason_phrase.assign(reason.begin(), reason.end());
       subscribe_done.content_exists = false;
       buffer << subscribe_done;

       LOGGER_DEBUG(logger_, "Sending SUBSCRIBE DONE to conn_id: {0} subscribe_id: {1}", conn_ctx.connection_handle, subscribe_id);

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }


   void Transport::SendUnsubscribe(ConnectionContext& conn_ctx,
                                      uint64_t subscribe_id)
   {
       StreamBuffer<uint8_t> buffer;

       auto unsubscribe  = MoqUnsubscribe {};
       unsubscribe.subscribe_id = subscribe_id;
       buffer << unsubscribe;

       LOGGER_DEBUG(logger_, "Sending UNSUBSCRIBE to conn_id: {0} subscribe_id: {1}", conn_ctx.connection_handle, subscribe_id);

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }


   void Transport::SendSubscribeError(ConnectionContext& conn_ctx,
                                          [[maybe_unused]] uint64_t subscribe_id,
                                          uint64_t track_alias,
                                          MoqSubscribeError error,
                                          const std::string& reason)
   {
       qtransport::StreamBuffer<uint8_t> buffer;

       auto subscribe_err  = MoqSubscribeError {};
       subscribe_err.subscribe_id = 0x1;
       subscribe_err.err_code = static_cast<uint64_t>(error.err_code);
       subscribe_err.track_alias = track_alias;
       subscribe_err.reason_phrase.assign(reason.begin(), reason.end());

       buffer << subscribe_err;

       LOGGER_DEBUG(logger_,
                    "Sending SUBSCRIBE ERROR to conn_id: {0} subscribe_id: {1} error code: {2} reason: {3}",
                    conn_ctx.connection_handle,
                    subscribe_id,
                    static_cast<int>(error),
                    reason);

       SendCtrlMsg(conn_ctx, buffer.Front(buffer.Size()));
   }

   Transport::Status Transport::Status()
   {
       return _status;
   }

   bool Transport::ProcessRecvCtrlMessage(ConnectionContext& conn_ctx,
                                               std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer)
   {
       if (stream_buffer->Size() == 0) { // should never happen
           CloseConnection(conn_ctx.connection_handle,
                            MoqTerminationReason::INTERNAL_ERROR,
                            "Stream buffer cannot be zero when parsing message type");
       }

       if (not conn_ctx.ctrl_msg_type_received) { // should never happen
           CloseConnection(conn_ctx.connection_handle,
                            MoqTerminationReason::INTERNAL_ERROR,
                            "Process recv message connection context is missing message type");
       }

       switch (*conn_ctx.ctrl_msg_type_received) {
           case MoqMessageType::SUBSCRIBE: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received subscribe, init stream buffer");
                   stream_buffer->InitAny<MoqSubscribe>();
               }

               auto& msg = stream_buffer->GetAny<MoqSubscribe>();
               if (*stream_buffer >> msg) {
                   auto tfn = FullTrackName{ msg.track_namespace, msg.track_name, std::nullopt };
                   auto th = TrackHash(tfn);

                   if (msg.subscribe_id > conn_ctx.current_subscribe_id) {
                       conn_ctx.current_subscribe_id = msg.subscribe_id + 1;
                   }

                   // For client/publisher, notify track that there is a subscriber
                   if (client_mode_) {
                       auto ptd = GetPubTrackDelegate(conn_ctx, th);
                       if (not ptd.has_value()) {
                           LOGGER_WARN(logger_,
                                       "Received subscribe unknown publish track conn_id: {0} namespace hash: {1} "
                                       "name hash: {2}",
                                       conn_ctx.connection_handle,
                                       th.track_namespace_hash,
                                       th.track_name_hash);

                           SendSubscribeError(conn_ctx,
                                                msg.subscribe_id,
                                                msg.track_alias,
                                                MoqSubscribeError::TRACK_NOT_EXIST,
                                                "Published track not found");
                           return true;
                       }

                       SendSubscribeOk(conn_ctx, msg.subscribe_id, MOQ_SUBSCRIBE_EXPIRES, false);

                       LOGGER_DEBUG(logger_, "Received subscribe to announced track alias: {0} recv subscribe_id: {1}, setting send state to ready", msg.track_alias, msg.subscribe_id);

                       // Indicate send is ready upon subscribe
                       // TODO(tievens): Maybe needs a delay as subscriber may have not received ok before data is sent
                       auto ptd_l = ptd->lock();
                       ptd_l->setSubscribeId(msg.subscribe_id);
                       ptd_l->SetSendStatus(PublishTrackHandler::PublishObjectStatus::kOk);
                       ptd_l->cb_sendReady();

                       conn_ctx.recv_sub_id[msg.subscribe_id] = { th.track_namespace_hash, th.track_name_hash };

                   } else { // Server mode
                       // TODO(tievens): add filter type when caching supports it
                       if (_delegate->cb_subscribe(conn_ctx.connection_handle, msg.subscribe_id, tfn.name_space, tfn.name)) {
                           SendSubscribeOk(conn_ctx, msg.subscribe_id, MOQ_SUBSCRIBE_EXPIRES, false);
                       }
                   }

                   stream_buffer->ResetAny();
                   return true;
               }
               break;
           }
           case MoqMessageType::SUBSCRIBE_OK: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received subscribe ok, init stream buffer");
                   stream_buffer->InitAny<MoqSubscribeOk>();
               }

               auto& msg = stream_buffer->GetAny<MoqSubscribeOk>();
               if (*stream_buffer >> msg) {
                   auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                   if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                       LOGGER_WARN(logger_, "Received subscribe ok to unknown subscribe track conn_id: {0} subscribe_id: {1}, ignored", conn_ctx.connection_handle, msg.subscribe_id);

                       // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race condition
                       stream_buffer->ResetAny();
                       return true;
                   }

                   sub_it->second.get()->cb_readReady();

                   stream_buffer->ResetAny();
                   return true;
               }
               break;
           }
           case MoqMessageType::SUBSCRIBE_ERROR: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received subscribe error, init stream buffer");
                   stream_buffer->InitAny<MoqSubscribeError>();
               }

               auto& msg = stream_buffer->GetAny<MoqSubscribeError>();
               if (*stream_buffer >> msg) {
                   auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);

                   if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                       LOGGER_WARN(logger_, "Received subscribe error to unknown subscribe_id conn_id: {0} subscribe_id: {1}, ignored", conn_ctx.connection_handle, msg.subscribe_id);

                       // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race condition
                       stream_buffer->ResetAny();
                       return true;
                   }

                   sub_it->second.get()->cb_readNotReady(MoqTrackDelegate::TrackReadStatus::SUBSCRIBE_ERROR);
                   RemoveSubscribeTrack(conn_ctx, *sub_it->second);

                   stream_buffer->ResetAny();
                   return true;
               }
               break;
           }
           case MoqMessageType::ANNOUNCE: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received announce, init stream buffer");
                   stream_buffer->InitAny<MoqAnnounce>();
               }

               auto& msg = stream_buffer->GetAny<MoqAnnounce>();
               if (*stream_buffer >> msg) {
                   auto tfn = FullTrackName{ msg.track_namespace, {} };
                   auto th = TrackHash(tfn);

                   if (_delegate->cb_announce(conn_ctx.connection_handle, th.track_namespace_hash)) {
                       SendAnnounceOk(conn_ctx, msg.track_namespace);
                       _delegate->cb_announce_post(conn_ctx.connection_handle, th.track_namespace_hash);
                   }

                   stream_buffer->ResetAny();
                   return true;
               }
               break;
           }
           case MoqMessageType::ANNOUNCE_OK: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received announce ok, init stream buffer");
                   stream_buffer->InitAny<MoqAnnounceOk>();
               }

               auto& msg = stream_buffer->GetAny<MoqAnnounceOk>();
               if (*stream_buffer >> msg) {
                   auto tfn = FullTrackName{ msg.track_namespace, {} };
                   auto th = TrackHash(tfn);
                   LOGGER_DEBUG(logger_, "Received announce ok, conn_id: {0} namespace_hash: {1}", conn_ctx.connection_handle, th.track_namespace_hash);

                   // Update each track to indicate status is okay to publish
                   auto pub_it = conn_ctx.pub_tracks_by_name.find(th.track_namespace_hash);
                   for (const auto& td : pub_it->second) {
                       if (td.second.get()->GetSendStatus() != MoqTrackDelegate::TrackSendStatus::OK)
                           td.second.get()->SetSendStatus(MoqTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                   }

                   stream_buffer->ResetAny();
                   return true;
               }
               break;
           }
           case MoqMessageType::ANNOUNCE_ERROR: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received announce error, init stream buffer");
                   stream_buffer->InitAny<MoqAnnounceError>();
               }

               auto& msg = stream_buffer->GetAny<MoqAnnounceError>();
               if (*stream_buffer >> msg) {
                   if (msg.track_namespace) {
                       std::string reason = "unknown";
                       auto tfn = FullTrackName{ *msg.track_namespace, {} };
                       auto th = TrackHash(tfn);

                       if (msg.reason_phrase) {
                           reason.assign(msg.reason_phrase->begin(), msg.reason_phrase->end());
                       }

                       LOGGER_INFO(logger_,
                                   "Received announce error for namespace_hash: {0} error code: {1} reason: {2}",
                                   th.track_namespace_hash,
                                   (msg.err_code.has_value() ? *msg.err_code : 0),
                                   reason);

                       stream_buffer->ResetAny();
                       return true;
                   }
               }

               break;
           }
           case MoqMessageType::UNANNOUNCE: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received unannounce, init stream buffer");
                   stream_buffer->InitAny<MoqUnannounce>();
               }

               auto& msg = stream_buffer->GetAny<MoqUnannounce>();
               if (*stream_buffer >> msg) {
                   auto tfn = FullTrackName{ msg.track_namespace, {} };
                   auto th = TrackHash(tfn);

                   LOGGER_INFO(logger_, "Received unannounce for namespace_hash: {0}", th.track_namespace_hash);

                   _delegate->cb_unannounce(conn_ctx.connection_handle, th.track_namespace_hash, std::nullopt);

                   stream_buffer->ResetAny();
                   return true;
               }

               break;
           }
           case MoqMessageType::UNSUBSCRIBE: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received unsubscribe, init stream buffer");
                   stream_buffer->InitAny<MoqUnsubscribe>();
               }

               auto& msg = stream_buffer->GetAny<MoqUnsubscribe>();
               if (*stream_buffer >> msg) {
                   if (!client_mode_) {
                       auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                       if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                           LOGGER_WARN(logger_, "Received unsubscribe to unknown subscribe_id conn_id: {0} subscribe_id: {1}, ignored", conn_ctx.connection_handle, msg.subscribe_id);

                           // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race condition
                           stream_buffer->ResetAny();
                           return true;
                       }

                       LOGGER_DEBUG(logger_, "Received unsubscribe conn_id: {0} subscribe_id: {1}" << conn_ctx.connection_handle, msg.subscribe_id);
                       sub_it->second.get()->cb_readNotReady(MoqTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);

                       _delegate->cb_unsubscribe(conn_ctx.connection_handle, msg.subscribe_id);

                       RemoveSubscribeTrack(conn_ctx, *sub_it->second);
                   } else {
                       const auto& [name_space, name] = conn_ctx.recv_sub_id[msg.subscribe_id];
                       TrackHash th(name_space, name);
                       if (auto pdt = GetPubTrackDelegate(conn_ctx, th)) {
                           pdt->lock()->SetSendStatus(MoqTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                           pdt->lock()->cb_sendNotReady(MoqTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                       }

                       conn_ctx.recv_sub_id.erase(msg.subscribe_id);
                   }

                   stream_buffer->ResetAny();
                   return true;
               }

               break;
           }
           case MoqMessageType::SUBSCRIBE_DONE: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received subscribe done, init stream buffer");
                   stream_buffer->InitAny<MoqSubscribeDone>();
               }

               auto& msg = stream_buffer->GetAny<MoqSubscribeDone>();
               if (*stream_buffer >> msg) {
                   auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                   if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                       LOGGER_WARN(logger_, "Received subscribe done to unknown subscribe_id conn_id: {0} subscribe_id: {1}", conn_ctx.connection_handle, msg.subscribe_id);

                       // TODO(tievens): Draft doesn't indicate what to do in this case, which can happen due to race condition
                       stream_buffer->ResetAny();
                       return true;
                   }
                   auto tfn = sub_it->second->GetFullTrackName();
                   auto th = TrackHash(tfn);

                   LOGGER_DEBUG(logger_,
                                "Received subscribe done conn_id: {0} subscribe_id: {1} track namespace hash: {2} "
                                "name hash: {3} track alias: {4}",
                                conn_ctx.connection_handle,
                                msg.subscribe_id,
                                th.track_namespace_hash,
                                th.track_name_hash,
                                th.track_fullname_hash);

                   sub_it->second.get()->cb_readNotReady(MoqTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);
                   _delegate->cb_unannounce(conn_ctx.connection_handle, th.track_namespace_hash, th.track_name_hash);

                   stream_buffer->ResetAny();
                   return true;
               }
               break;
           }
           case MoqMessageType::ANNOUNCE_CANCEL: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received announce cancel, init stream buffer");
                   stream_buffer->InitAny<MoqAnnounceCancel>();
               }

               auto& msg = stream_buffer->GetAny<MoqAnnounceCancel>();
               if (*stream_buffer >> msg) {
                   auto tfn = FullTrackName{ msg.track_namespace, {} };
                   auto th = TrackHash(tfn);

                   LOGGER_INFO(logger_, "Received announce cancel for namespace_hash: {0}", th.track_namespace_hash);

                   stream_buffer->ResetAny();
                   return true;
               }

               break;
           }
           case MoqMessageType::TRACK_STATUS_REQUEST: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received track status request, init stream buffer");
                   stream_buffer->InitAny<MoqTrackStatusRequest>();
               }

               auto& msg = stream_buffer->GetAny<MoqTrackStatusRequest>();
               if (*stream_buffer >> msg) {
                   auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                   auto th = TrackHash(tfn);

                   LOGGER_INFO(logger_, "Received track status request for namespace_hash: {0} name_hash: {1}", th.track_namespace_hash, th.track_name_hash);

                   stream_buffer->ResetAny();
                   return true;
               }

               break;
           }
           case MoqMessageType::TRACK_STATUS: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received track status, init stream buffer");
                   stream_buffer->InitAny<MoqTrackStatus>();
               }

               auto& msg = stream_buffer->GetAny<MoqTrackStatus>();
               if (*stream_buffer >> msg) {
                   auto tfn = FullTrackName{ msg.track_namespace, msg.track_name };
                   auto th = TrackHash(tfn);

                   LOGGER_INFO(logger_, "Received track status for namespace_hash: {0} name_hash: {1}", th.track_namespace_hash, th.track_name_hash);

                   stream_buffer->ResetAny();
                   return true;
               }

               break;
           }
           case MoqMessageType::GOAWAY: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received goaway, init stream buffer");
                   stream_buffer->InitAny<MoqGoaway>();
               }

               auto& msg = stream_buffer->GetAny<MoqGoaway>();
               if (*stream_buffer >> msg) {
                   std::string new_sess_uri(msg.new_session_uri.begin(), msg.new_session_uri.end());
                   LOGGER_INFO(logger_, "Received goaway new session uri: {0}", new_sess_uri);

                   stream_buffer->ResetAny();
                   return true;
               }

               break;
           }
           case MoqMessageType::CLIENT_SETUP: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received client setup, init stream buffer");
                   stream_buffer->InitAny<MoqClientSetup>();
               }

               auto& msg = stream_buffer->GetAny<MoqClientSetup>();
               if (*stream_buffer >> msg) {
                   if (!msg.supported_versions.size()) { // should never happen
                       CloseConnection(conn_ctx.connection_handle,
                                        MoqTerminationReason::PROTOCOL_VIOLATION,
                                        "Client setup contained zero versions");

                   }

                   std::string client_endpoint_id(msg.endpoint_id_parameter.value.begin(),
                                                  msg.endpoint_id_parameter.value.end());

                   _delegate->cb_connectionStatus(conn_ctx.connection_handle,
                                                  msg.endpoint_id_parameter.value,
                                                  TransportStatus::Ready);

                   LOGGER_INFO(
                     logger_,
                     "Client setup received conn_id: {0} from: {1} num_versions: {2} role: {3} version: {4}",
                     conn_ctx.connection_handle,
                     client_endpoint_id,
                     msg.num_versions,
                     static_cast<int>(msg.role_parameter.value.front()),
                     msg.supported_versions.front());

                   conn_ctx.client_version = msg.supported_versions.front();
                   stream_buffer->ResetAny();

                   SendServerSetup(conn_ctx);
                   conn_ctx.setup_complete = true;
                   return true;
               }
               break;
           }
           case MoqMessageType::SERVER_SETUP: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received server setup, init stream buffer");
                   stream_buffer->InitAny<MoqServerSetup>();
               }

               auto& msg = stream_buffer->GetAny<MoqServerSetup>();
               if (*stream_buffer >> msg) {
                   std::string server_endpoint_id(msg.endpoint_id_parameter.value.begin(),
                                                  msg.endpoint_id_parameter.value.end());

                   _delegate->cb_connectionStatus(conn_ctx.connection_handle,
                                                  msg.endpoint_id_parameter.value,
                                                  TransportStatus::Ready);

                   LOGGER_INFO(logger_,
                               "Server setup received conn_id: {0} from: {1} role: {2} selected_version: {3}",
                               conn_ctx.connection_handle,
                               server_endpoint_id,
                               static_cast<int>(msg.role_parameter.value.front()),
                               msg.selection_version);

                   stream_buffer->ResetAny();
                   conn_ctx.setup_complete = true;
                   return true;
               }
               break;
           }

           default:
               LOGGER_ERROR(logger_, "Unsupported MOQT message type: {0}", static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received));
               CloseConnection(conn_ctx.connection_handle,
                                MoqTerminationReason::PROTOCOL_VIOLATION,
                                "Unsupported MOQT message type");
               return true;

       } // End of switch(msg type)

       LOGGER_DEBUG(logger_, "type: {0} sbuf_size: {1}", static_cast<int>(*conn_ctx.ctrl_msg_type_received), stream_buffer->Size());
       return false;
   }

   bool Transport::ProcessRecvStreamDataMessage(ConnectionContext& conn_ctx,
                                                      std::shared_ptr<StreamBuffer<uint8_t>>& stream_buffer)
   {
       if (stream_buffer->Size() == 0) { // should never happen
           CloseConnection(conn_ctx.connection_handle,
                            MoqTerminationReason::INTERNAL_ERROR,
                            "Stream buffer cannot be zero when parsing message type");
       }

       // Header not set, get the header for this stream or datagram
       MoqMessageType data_type;
       if (!stream_buffer->AnyHasValue()) {
           auto val = stream_buffer->DecodeUintV();
           if (val) {
               data_type = static_cast<MoqMessageType>(*val);
           } else {
               return false;
           }
       } else {
           auto dt = stream_buffer->GetAnyType();
           if (dt.has_value()) {
               data_type = static_cast<MoqMessageType>(*dt);
           }
           else {
               LOGGER_WARN(logger_, "Unknown data type for data stream");
               return true;
           }
       }

       switch (data_type) {
           case MoqMessageType::OBJECT_STREAM: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received stream header object, init stream buffer");
                   stream_buffer->InitAny<MoqObjectStream>(
                     static_cast<uint64_t>(MoqMessageType::OBJECT_STREAM));
               }

               auto& msg = stream_buffer->GetAny<MoqObjectStream>();
               if (*stream_buffer >> msg) {
                   auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                   if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                       LOGGER_WARN(logger_, "Received stream_object to unknown subscribe track subscribe_id: {0}, ignored", msg.subscribe_id);

                       // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                       return true;
                   }

                   LOGGER_DEBUG(logger_,
                                "Received stream_object subscribe_id: {0} priority: {1} track_alias: {2} group_id: "
                                "{3} object_id: {4} data size: {5}",
                                msg.subscribe_id,
                                msg.priority,
                                msg.track_alias,
                                msg.group_id,
                                msg.object_id,
                                msg.payload.size());
                   sub_it->second->cb_objectReceived(msg.group_id, msg.object_id, msg.priority,
                                                     std::move(msg.payload),
                                                     TrackMode::kStreamPerObject);
                   stream_buffer->ResetAny();
               }
               break;
           }

           case MoqMessageType::STREAM_HEADER_TRACK: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received stream header track, init stream buffer");
                   stream_buffer->InitAny<MoqStreamHeaderTrack>(
                     static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_TRACK));
               }

               auto& msg = stream_buffer->GetAny<MoqStreamHeaderTrack>();
               if (!stream_buffer->AnyHasValueB() && *stream_buffer >> msg) {
                   auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                   if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                       LOGGER_WARN(logger_, "Received stream_header_track to unknown subscribe track subscribe_id: {0}, ignored", msg.subscribe_id);

                       // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                       return true;
                   }

                   // Init second working buffer to read data object
                   stream_buffer->InitAnyB<MoqStreamTrackObject>();

                   LOGGER_DEBUG(logger_,
                                "Received stream_header_track subscribe_id: {0} priority: {1} track_alias: {2}",
                                msg.subscribe_id,
                                msg.priority,
                                msg.track_alias);
               }

               if (stream_buffer->AnyHasValueB()) {
                   MoqStreamTrackObject obj;
                   if (*stream_buffer >> obj) {
                       auto sub_it


                         = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                       if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                           LOGGER_WARN(logger_, "Received stream_header_group to unknown subscribe track subscribe_id: {0}, ignored", msg.subscribe_id);

                           // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                           return true;
                       }

                       LOGGER_DEBUG(logger_,
                                    "Received stream_track_object subscribe_id: {0} priority: {1} track_alias: {2} "
                                    "group_id: {3} object_id: {4} data size: {5}",
                                    msg.subscribe_id,
                                    msg.priority,
                                    msg.track_alias,
                                    obj.group_id,
                                    obj.object_id,
                                    obj.payload.size());
                       stream_buffer->ResetAnyB();

                       sub_it->second->cb_objectReceived(obj.group_id, obj.object_id, msg.priority,
                                                         std::move(obj.payload),
                                                         TrackMode::kStreamPerTrack);
                   }
               }
               break;
           }
           case MoqMessageType::STREAM_HEADER_GROUP: {
               if (not stream_buffer->AnyHasValue()) {
                   LOGGER_DEBUG(logger_, "Received stream header group, init stream buffer");
                   stream_buffer->InitAny<MoqStreamHeaderGroup>(static_cast<uint64_t>(MoqMessageType::STREAM_HEADER_GROUP));
               }

               auto& msg = stream_buffer->GetAny<MoqStreamHeaderGroup>();
               if (!stream_buffer->AnyHasValueB() && *stream_buffer >> msg) {
                   auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                   if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                       LOGGER_WARN(logger_, "Received stream_header_group to unknown subscribe track subscribe_id: {0}, ignored",  msg.subscribe_id);

                       // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                       return true;
                   }

                   // Init second working buffer to read data object
                   stream_buffer->InitAnyB<MoqStreamGroupObject>();

                   LOGGER_DEBUG(
                     logger_,
                     "Received stream_header_group subscribe_id: {0} priority: {1} track_alias: {2} group_id: {3}",
                     msg.subscribe_id,
                     msg.priority,
                     msg.track_alias,
                     msg.group_id);
               }

               if (stream_buffer->AnyHasValueB()) {
                   MoqStreamGroupObject obj;
                   if (*stream_buffer >> obj) {
                       auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                       if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                           LOGGER_WARN(logger_, "Received stream_header_group to unknown subscribe track subscribe_id: {0}, ignored", msg.subscribe_id);

                           // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                           return true;
                       }

                       LOGGER_DEBUG(logger_,
                                    "Received stream_group_object subscribe_id: {0} priority: {1} track_alias: {2} "
                                    "group_id: {3} object_id: {4} data size: {5}",
                                    msg.subscribe_id,
                                    msg.priority,
                                    msg.track_alias,
                                    msg.group_id,
                                    obj.object_id,
                                    obj.payload.size());
                       stream_buffer->ResetAnyB();

                       sub_it->second->cb_objectReceived(msg.group_id, obj.object_id, msg.priority,
                                                         std::move(obj.payload),
                                                         TrackMode::kStreamPerGroup);
                   }
               }

               break;
           }

           default:
               // Process the stream object type
               /*
               logger_->error << "Unsupported MOQT data message "
                              << "type: " << static_cast<uint64_t>(*conn_ctx.ctrl_msg_type_received)
                              << std::flush;
               CloseConnection(conn_ctx.connection_handle,
                                MoqTerminationReason::PROTOCOL_VIOLATION,
                                "Unsupported MOQT data message type");
                */
               return true;

       }

       return false;
   }

   std::optional<uint64_t> Transport::SubscribeTrack(TransportConnId conn_id,
                                                       std::shared_ptr<MoqTrackDelegate> track_delegate)
   {
       // Generate track alias
       auto tfn = FullTrackName{ track_delegate->GetTrackNamespace(), track_delegate->getTrackName() };

       // Track hash is the track alias for now.
       // TODO(tievens): Evaluate; change hash to be more than 62 bits to avoid collisions
       auto th = TrackHash(tfn);

       track_delegate->setTrackAlias(th.track_fullname_hash);

       LOGGER_INFO(logger_, "Subscribe track conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

       std::lock_guard<std::mutex> _(state_mutex_);
       auto conn_it = connections_.find(conn_id);
       if (conn_it == connections_.end()) {
           LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_id);
           return std::nullopt;
       }

       auto sid = conn_it->second.current_subscribe_id++;

       LOGGER_DEBUG(logger_, "subscribe id to add to memory: {0}", sid);

       // Set the track delegate for pub/sub using _sub_pub_id, which is the subscribe Id in MOQT
       conn_it->second.tracks_by_sub_id[sid] = track_delegate;

       track_delegate->setSubscribeId(sid);

       SendSubscribe(conn_it->second, sid, tfn, th);

       return th.track_fullname_hash;
   }

   std::optional<uint64_t> Transport::BindSubscribeTrack(TransportConnId conn_id,
                                                           uint64_t subscribe_id,
                                                           std::shared_ptr<MoqTrackDelegate> track_delegate) {


       // Generate track alias
       auto tfn = FullTrackName{ track_delegate->GetTrackNamespace(), track_delegate->getTrackName() };

       // Track hash is the track alias for now.
       auto th = TrackHash(tfn);

       track_delegate->setTrackAlias(th.track_fullname_hash);

       LOGGER_INFO(logger_, "Bind subscribe track delegate conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

       std::lock_guard<std::mutex> _(state_mutex_);
       auto conn_it = connections_.find(conn_id);
       if (conn_it == connections_.end()) {
           LOGGER_ERROR(logger_, "Subscribe track conn_id: {0} does not exist.", conn_id);
           return std::nullopt;
       }

       // Set the track delegate for pub/sub using _sub_pub_id, which is the subscribe Id in MOQT
       conn_it->second.tracks_by_sub_id[subscribe_id] = track_delegate;

       track_delegate->setSubscribeId(subscribe_id);

       track_delegate->_mi_conn_id = conn_id;

       track_delegate->_mi_send_data_ctx_id = quic_transport_->CreateDataContext(
         conn_id,
         track_delegate->_mi_track_mode == TrackMode::kDatagram ? false : true,
         track_delegate->_def_priority,
         false);

       // Setup the function for the track delegate to use to send objects with thread safety
       track_delegate->_mi_sendObjFunc =
         [&,
          track_delegate = track_delegate,
          subscribe_id = track_delegate->GetSubscribeId()](uint8_t priority,
                                                           uint32_t ttl,
                                                           bool stream_header_needed,
                                                           uint64_t group_id,
                                                           uint64_t object_id,
                                                           Span<uint8_t const> data) -> MoqTrackDelegate::SendError {
           return send_object(track_delegate,
                              priority,
                              ttl,
                              stream_header_needed,
                              group_id,
                              object_id,
                              data);
       };


       return th.track_fullname_hash;
   }

   void Transport::UnsubscribeTrack(qtransport::TransportConnId conn_id, std::shared_ptr<MoqTrackDelegate> track_delegate)
   {
       auto& conn_ctx = connections_[conn_id];
       if (track_delegate->GetSubscribeId().has_value()) {
           SendUnsubscribe(conn_ctx, *track_delegate->GetSubscribeId());
       }
       RemoveSubscribeTrack(conn_ctx,*track_delegate);
   }

   void Transport::RemoveSubscribeTrack(ConnectionContext& conn_ctx,
                                           MoqTrackDelegate& delegate, bool remove_delegate)
   {
       delegate.setReadStatus(MoqTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);
       delegate.setSubscribeId(std::nullopt);

       auto subscribe_id = delegate.GetSubscribeId();
       if (subscribe_id.has_value()) {

           SendUnsubscribe(conn_ctx, *subscribe_id);

           LOGGER_DEBUG(logger_, "remove subscribe id: {0}", *subscribe_id);

           quic_transport_->DeleteDataContext(conn_ctx.connection_handle, delegate._mi_send_data_ctx_id);

           delegate._mi_send_data_ctx_id = 0;


           if (remove_delegate) {
               std::lock_guard<std::mutex> _(state_mutex_);
               conn_ctx.tracks_by_sub_id.erase(*subscribe_id);
           }
       }
   }

   void Transport::UnpublishTrack(TransportConnId conn_id,
                                    std::shared_ptr<MoqTrackDelegate> track_delegate) {

       // Generate track alias
       auto tfn = FullTrackName{ track_delegate->GetTrackNamespace(), track_delegate->getTrackName() };
       auto th = TrackHash(tfn);

       LOGGER_INFO(logger_, "Unpublish track conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

       std::lock_guard<std::mutex> _(state_mutex_);

       auto conn_it = connections_.find(conn_id);
       if (conn_it == connections_.end()) {
           LOGGER_ERROR(logger_, "Unpublish track conn_id: {0} does not exist.", conn_id);
           return;
       }

       // Check if this published track is a new namespace or existing.
       auto pub_ns_it = conn_it->second.pub_tracks_by_name.find(th.track_namespace_hash);
       if (pub_ns_it != conn_it->second.pub_tracks_by_name.end()) {
           auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
           if (pub_n_it != pub_ns_it->second.end()) {

               // Send subscribe done if track has subscriber and is sending
               if (pub_n_it->second->GetSendStatus() == MoqTrackDelegate::TrackSendStatus::OK &&
                   pub_n_it->second->GetSubscribeId().has_value()) {
                   LOGGER_INFO(logger_,
                               "Unpublish track namespace hash: {0} track_name_hash: {1} track_alias: {2}, sending "
                               "subscribe_done",
                               th.track_namespace_hash,
                               th.track_name_hash,
                               th.track_fullname_hash);
                   SendSubscribeDone(conn_it->second, *pub_n_it->second->GetSubscribeId(), "Unpublish track");
               } else {
                   LOGGER_INFO(logger_,
                               "Unpublish track namespace hash: {0} track_name_hash: {1} track_alias: {2}",
                               th.track_namespace_hash,
                               th.track_name_hash,
                               th.track_fullname_hash);
               }

               pub_n_it->second->_mi_send_data_ctx_id = 0;

               pub_n_it->second->SetSendStatus(MoqTrackDelegate::TrackSendStatus::NOT_ANNOUNCED);
               pub_ns_it->second.erase(pub_n_it);
           }

           if (!pub_ns_it->second.size()) {
               LOGGER_INFO(logger_, "Unpublish namespace hash: {0}, has no tracks, sending unannounce", th.track_namespace_hash);
               SendUnannounce(conn_it->second, track_delegate->GetTrackNamespace());
               conn_it->second.pub_tracks_by_name.erase(pub_ns_it);
           }
       }
   }


   std::optional<uint64_t> Transport::PublishTrack(TransportConnId conn_id,
                                                     std::shared_ptr<MoqTrackDelegate> track_delegate) {

       // Generate track alias
       auto tfn = FullTrackName{ track_delegate->GetTrackNamespace(), track_delegate->getTrackName() };

       // Track hash is the track alias for now.
       // TODO(tievens): Evaluate; change hash to be more than 62 bits to avoid collisions
       auto th = TrackHash(tfn);

       track_delegate->setTrackAlias(th.track_fullname_hash);

       LOGGER_INFO(logger_, "Publish track conn_id: {0} hash: {1}", conn_id, th.track_fullname_hash);

       std::lock_guard<std::mutex> _(state_mutex_);

       auto conn_it = connections_.find(conn_id);
       if (conn_it == connections_.end()) {
           LOGGER_ERROR(logger_, "Publish track conn_id: {0} does not exist.", conn_id);
           return std::nullopt;
       }

       // Check if this published track is a new namespace or existing.
       auto pub_ns_it = conn_it->second.pub_tracks_by_name.find(th.track_namespace_hash);
       if (pub_ns_it == conn_it->second.pub_tracks_by_name.end()) {
           LOGGER_INFO(logger_, "Publish track has new namespace hash: {0} sending ANNOUNCE message", th.track_namespace_hash);

           track_delegate->SetSendStatus(MoqTrackDelegate::TrackSendStatus::PENDING_ANNOUNCE_RESPONSE);
           SendAnnounce(conn_it->second, track_delegate->GetTrackNamespace());

       } else {
           auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
           if (pub_n_it == pub_ns_it->second.end()) {
               LOGGER_INFO(logger_,
                           "Publish track has new track namespace hash: {0} name hash: {1}",
                           th.track_namespace_hash,
                           th.track_name_hash);
           }
       }

       // Set the track delegate for pub/sub
       conn_it->second.pub_tracks_by_name[th.track_namespace_hash][th.track_name_hash] = track_delegate;

       track_delegate->_mi_conn_id = conn_id;
       track_delegate->_mi_send_data_ctx_id = quic_transport_->CreateDataContext(
         conn_id,
         track_delegate->_mi_track_mode == TrackMode::kDatagram ? false : true,
         track_delegate->_def_priority,
         false);

       // Setup the function for the track delegate to use to send objects with thread safety
       track_delegate->_mi_sendObjFunc =
         [&,
          track_delegate = track_delegate,
          subscribe_id = track_delegate->GetSubscribeId()](uint8_t priority,
                                                           uint32_t ttl,
                                                           bool stream_header_needed,
                                                           uint64_t group_id,
                                                           uint64_t object_id,
                                                           Span<const uint8_t> data) -> MoqTrackDelegate::SendError {
           return send_object(track_delegate,
                              priority,
                              ttl,
                              stream_header_needed,
                              group_id,
                              object_id,
                              data);
       };

       return th.track_fullname_hash;
   }

   MoqTrackDelegate::SendError Transport::SendObject(std::weak_ptr<MoqTrackDelegate> track_delegate,
                                                        uint8_t priority,
                                                        uint32_t ttl,
                                                        bool stream_header_needed,
                                                        uint64_t group_id,
                                                        uint64_t object_id,
                                                        Span<const uint8_t> data)
   {

       auto td = track_delegate.lock();

       if (!td->getTrackAlias().has_value()) {
           return MoqTrackDelegate::SendError::NOT_ANNOUNCED;
       }

       if (!td->GetSubscribeId().has_value()) {
           return MoqTrackDelegate::SendError::NO_SUBSCRIBERS;
       }

       ITransport::EnqueueFlags eflags;

       StreamBuffer<uint8_t> buffer;

       switch(td->_mi_track_mode) {
           case TrackMode::kDatagram: {
               MoqObjectDatagram object;
               object.group_id = group_id;
               object.object_id = object_id;
               object.priority = priority;
               object.subscribe_id = *td->GetSubscribeId();
               object.track_alias = *td->getTrackAlias();
               object.payload.assign(data.begin(), data.end());
               buffer << object;
               break;
           }
           case TrackMode::kStreamPerObject: {
               eflags.use_reliable = true;
               eflags.new_stream = true;

               MoqObjectStream object;
               object.group_id = group_id;
               object.object_id = object_id;
               object.priority = priority;
               object.subscribe_id = *td->GetSubscribeId();
               object.track_alias = *td->getTrackAlias();
               object.payload.assign(data.begin(), data.end());
               buffer << object;

               break;
           }

           case TrackMode::kStreamPerGroup: {
               eflags.use_reliable = true;

               if (stream_header_needed) {
                   eflags.new_stream = true;
                   eflags.clear_tx_queue = true;
                   eflags.use_reset = true;

                   MoqStreamHeaderGroup group_hdr;
                   group_hdr.group_id = group_id;
                   group_hdr.priority = priority;
                   group_hdr.subscribe_id = *td->GetSubscribeId();
                   group_hdr.track_alias = *td->getTrackAlias();
                   buffer << group_hdr;
               }

               MoqStreamGroupObject object;
               object.object_id = object_id;
               object.payload.assign(data.begin(), data.end());
               buffer << object;

               break;
           }
           case TrackMode::kStreamPerTrack: {
               eflags.use_reliable = true;

               if (stream_header_needed) {
                   eflags.new_stream = true;

                   MoqStreamHeaderTrack track_hdr;
                   track_hdr.priority = priority;
                   track_hdr.subscribe_id = *td->GetSubscribeId();
                   track_hdr.track_alias = *td->getTrackAlias();
                   buffer << track_hdr;
               }

               MoqStreamTrackObject object;
               object.group_id = group_id;
               object.object_id = object_id;
               object.payload.assign(data.begin(), data.end());
               buffer << object;

               break;
           }
       }

       // TODO(tievens): Add M10x specific chunking... lacking in the draft
       std::vector<uint8_t> serialized_data = buffer.Front(buffer.Size());

       quic_transport_->Enqueue(td->_mi_conn_id, td->_mi_send_data_ctx_id, std::move(serialized_data),
                           { MethodTraceItem{} }, priority,
                           ttl, 0, eflags);

       return MoqTrackDelegate::SendError::OK;
   }


   std::optional<std::weak_ptr<MoqTrackDelegate>> Transport::GetPubTrackDelegate(ConnectionContext& conn_ctx,
                                                                                   TrackHash& th)
   {
       auto pub_ns_it = conn_ctx.pub_tracks_by_name.find(th.track_namespace_hash);
       if (pub_ns_it == conn_ctx.pub_tracks_by_name.end()) {
           return std::nullopt;
       } else {
           auto pub_n_it = pub_ns_it->second.find(th.track_name_hash);
           if (pub_n_it == pub_ns_it->second.end()) {
               return std::nullopt;
           }

           return pub_n_it->second;
       }
   }


   // ---------------------------------------------------------------------------------------
   // Transport delegate callbacks
   // ---------------------------------------------------------------------------------------

   void Transport::OnConnectionStatus(const TransportConnId& conn_id, const TransportStatus status)
   {
       LOGGER_DEBUG(logger_, "Connection status conn_id: {0} status: {1}", conn_id, static_cast<int>(status));

       switch (status) {
           case TransportStatus::Ready: {
               if (client_mode_) {
                   auto& conn_ctx = connections_[conn_id];
                   LOGGER_INFO(logger_, "Connection established, creating bi-dir stream and sending CLIENT_SETUP");

                   conn_ctx.ctrl_data_ctx_id = quic_transport_->CreateDataContext(conn_id, true, 0, true);

                   send_client_setup();
                   _status = Status::READY;
               }
               break;
           }

           case TransportStatus::Connecting:
               _status = Status::CLIENT_CONNECTING;
               break;
           case TransportStatus::RemoteRequestClose:
               [[fallthrough]];

           case TransportStatus::Disconnected: {

               // Clean up publish and subscribe tracks
               std::lock_guard<std::mutex> _(state_mutex_);
               auto conn_it = connections_.find(conn_id);

               if (conn_it == connections_.end()) {
                   return;
               }

               for (const auto& [sub_id, delegate] : conn_it->second.tracks_by_sub_id) {
                   delegate->cb_readNotReady(MoqTrackDelegate::TrackReadStatus::NOT_SUBSCRIBED);
                   _delegate->cb_unsubscribe(conn_id, sub_id);
                   RemoveSubscribeTrack(conn_it->second, *delegate);
               }

               for (const auto& [name_space, track] : conn_it->second.recv_sub_id) {
                   TrackHash th(track.first, track.second);
                   if (auto pdt = GetPubTrackDelegate(conn_it->second, th)) {
                       pdt->lock()->SetSendStatus(MoqTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                       pdt->lock()->cb_sendNotReady(MoqTrackDelegate::TrackSendStatus::NO_SUBSCRIBERS);
                   }
               }

               conn_it->second.recv_sub_id.clear();
               conn_it->second.tracks_by_sub_id.clear();

               // TODO(tievens): Clean up publish tracks

               connections_.erase(conn_it);

               break;
           }
           case TransportStatus::Shutdown:
               _status = Status::NOT_READY;
               break;
       }
   }

   void Transport::OnNewConnection(const TransportConnId& conn_id, const TransportRemote& remote)
   {
       auto [conn_ctx, is_new] = connections_.try_emplace(conn_id, ConnectionContext{});

       LOGGER_INFO(logger_, "New connection conn_id: {0} remote ip: {1} port: {2}", conn_id, remote.host_or_ip, remote.port);

       conn_ctx->second.conn_id = conn_id;
   }

   void Transport::OnRecvStream(const TransportConnId& conn_id,
                                    uint64_t stream_id,
                                    std::optional<DataContextId> data_ctx_id,
                                    const bool is_bidir)
   {
       auto stream_buf = quic_transport_->getStreamBuffer(conn_id, stream_id);

       //TODO(tievens): Considering moving lock to here... std::lock_guard<std::mutex> _(state_mutex_);

       auto& conn_ctx = connections_[conn_id];

       if (stream_buf == nullptr) {
           return;
       }

       if (is_bidir && not conn_ctx.ctrl_data_ctx_id) {
           if (not data_ctx_id) {
               CloseConnection(conn_id,
                                MoqTerminationReason::INTERNAL_ERROR,
                                "Received bidir is missing data context");
               return;
           }
           conn_ctx.ctrl_data_ctx_id = data_ctx_id;
       }

       for (int i=0; i < MOQ_READ_LOOP_MAX_PER_STREAM; i++) { // don't loop forever, especially on bad stream
           // bidir is Control stream, data streams are unidirectional
           if (is_bidir) {
               if (not conn_ctx.ctrl_msg_type_received) {
                   auto msg_type = stream_buf->decode_uintV();

                   if (msg_type) {
                       conn_ctx.ctrl_msg_type_received = static_cast<MoqMessageType>(*msg_type);
                   } else {
                       break;
                   }
               }

               if (conn_ctx.ctrl_msg_type_received) {
                   if (process_recv_ctrl_message(conn_ctx, stream_buf)) {
                       conn_ctx.ctrl_msg_type_received = std::nullopt;
                       break;
                   }
               }
           }

           // Data stream, unidirectional
           else {
               if (process_recv_stream_data_message(conn_ctx, stream_buf)) {
                   break;
               }
           }


           if (!stream_buf->size()) { // done
               break;
           }
       }
   }

   void Transport::OnRecvDgram(const TransportConnId& conn_id, std::optional<DataContextId> data_ctx_id)
   {
       MoqObjectStream object_datagram_out;
       for (int i=0; i < MOQ_READ_LOOP_MAX_PER_STREAM; i++) {
           auto data = quic_transport_->dequeue(conn_id, data_ctx_id);
           if (data && !data->empty()) {
               StreamBuffer<uint8_t> buffer;
               buffer.Push(*data);

               auto msg_type = buffer.DecodeUintV();
               if (!msg_type || static_cast<MoqMessageType>(*msg_type) != MoqMessageType::OBJECT_DATAGRAM) {
                   LOGGER_WARN(logger_, "Received datagram that is not message type OBJECT_DATAGRAM, dropping");
                   continue;
               }

               MoqObjectDatagram msg;
               if (buffer >> msg) {

                   ////TODO(tievens): Considering moving lock to here... std::lock_guard<std::mutex> _(state_mutex_);

                   auto& conn_ctx = connections_[conn_id];
                   auto sub_it = conn_ctx.tracks_by_sub_id.find(msg.subscribe_id);
                   if (sub_it == conn_ctx.tracks_by_sub_id.end()) {
                       LOGGER_WARN(logger_, "Received datagram to unknown subscribe track subscribe_id: {0}, ignored", msg.subscribe_id);

                       // TODO(tievens): Should close/reset stream in this case but draft leaves this case hanging

                       continue;
                   }

                   LOGGER_DEBUG(logger_,
                                "Received object datagram conn_id: {0} data_ctx_id: {1} subscriber_id: {2} "
                                "track_alias: {3} group_id: {4} object_id: {5} data size: {6}",
                                conn_id,
                                (data_ctx_id ? *data_ctx_id : 0),
                                msg.subscribe_id,
                                msg.track_alias,
                                msg.group_id,
                                msg.object_id,
                                msg.payload.size());

                   sub_it->second->cb_objectReceived(msg.group_id, msg.object_id, msg.priority,
                                                     std::move(msg.payload),
                                                     TrackMode::kDatagram);

               } else {
                   LOGGER_WARN(logger_, "Failed to decode datagram conn_id: {0} data_ctx_id: {1}", conn_id, (data_ctx_id ? *data_ctx_id : 0));
               }

           }
       }

   }

   void Transport::CloseConnection(TransportConnId conn_id, messages::MoqTerminationReason reason,
                                      const std::string& reason_str)
   {
       std::ostringstream log_msg;
       log_msg << "Closing conn_id: " << conn_id;
       switch (reason) {
           case MoqTerminationReason::NO_ERROR:
               log_msg << " no error";
               break;
           case MoqTerminationReason::INTERNAL_ERROR:
               log_msg << " internal error: " << reason_str;
               break;
           case MoqTerminationReason::UNAUTHORIZED:
               log_msg << " unauthorized: " << reason_str;
               break;
           case MoqTerminationReason::PROTOCOL_VIOLATION:
               log_msg << " protocol violation: " << reason_str;
               break;
           case MoqTerminationReason::DUP_TRACK_ALIAS:
               log_msg << " duplicate track alias: " << reason_str;
               break;
           case MoqTerminationReason::PARAM_LEN_MISMATCH:
               log_msg << " param length mismatch: " << reason_str;
               break;
           case MoqTerminationReason::GOAWAY_TIMEOUT:
               log_msg << " goaway timeout: " << reason_str;
               break;
       }

       LOGGER_INFO(logger_, log_msg.str());

       quic_transport_->close(conn_id, static_cast<uint64_t>(reason));

       if (client_mode_) {
           LOGGER_INFO(logger_, "Client connection closed, stopping client");
           _stop = true;
       }
   }


} // namespace moq