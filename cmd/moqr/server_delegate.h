#pragma once

#include "async_queue.h"

#include <cantina/logger.h>
#include <quicr/quicr_server_delegate.h>
#include "quicr/encode.h"
#include "quicr/message_buffer.h"
#include "quicr/moq_message_types.h"
#include "qsession.h"
#include "quicr/quicr_common.h"
#include <future>


class ServerDelegate : public quicr::ServerDelegate
{
public:
  ServerDelegate(cantina::LoggerPointer logger_in,
                 std::shared_ptr<AsyncQueue<QuicrObject>> queue_in,
                 std::promise<bool> on_response_in);

  ~ServerDelegate() = default;

  void onPublishIntent(const quicr::Namespace& quicr_name,
                               const std::string& origin_url,
                               bool use_reliable_transport,
                               const std::string& auth_token,
                               quicr::bytes&& e2e_token) override;


  void onPublishIntentEnd(const quicr::Namespace& quicr_namespace,
                                  const std::string& auth_token,
                                  quicr::bytes&& e2e_token) override;

  void onPublisherObject(
    const qtransport::TransportContextId& context_id,
    const qtransport::StreamId& stream_id,
    bool use_reliable_transport,
    quicr::messages::PublishDatagram&& datagram) override;

  void onPublishedObject(
    const qtransport::TransportContextId& context_id,
    const qtransport::StreamId& stream_id,
    bool use_reliable_transport,
    quicr::messages::MoqObject&& datagram) override;

  void onSubscribe(const quicr::Namespace& quicr_namespace,
                           const uint64_t& subscriber_id,
                           const qtransport::TransportContextId& context_id,
                           const qtransport::StreamId& stream_id,
                           const quicr::SubscribeIntent subscribe_intent,
                           const std::string& origin_url,
                           bool use_reliable_transport,
                           const std::string& auth_token,
                           quicr::bytes&& data) override;

  void onUnsubscribe(const quicr::Namespace& quicr_namespace,
                             const uint64_t& subscriber_id,
                             const std::string& auth_token) override;

private:
  cantina::LoggerPointer logger;
  std::shared_ptr<AsyncQueue<QuicrObject>> queue;
  std::optional<std::promise<bool>> on_response;
};