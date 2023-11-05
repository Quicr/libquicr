#include "server_delegate.h"
#include <sstream>

void ServerDelegate::onPublishIntent(const quicr::Namespace& quicr_name,
                const std::string& origin_url,
                bool use_reliable_transport,
                const std::string& auth_token,
                quicr::bytes&& e2e_token) {}


void ServerDelegate::onPublishIntentEnd(const quicr::Namespace& quicr_namespace,
                   const std::string& auth_token,
                   quicr::bytes&& e2e_token) {}

void ServerDelegate::onPublisherObject(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& stream_id,
  bool use_reliable_transport,
  quicr::messages::PublishDatagram&& datagram)
{

}

void ServerDelegate::onPublishedObject(
  const qtransport::TransportContextId& context_id,
  const qtransport::StreamId& stream_id,
  bool use_reliable_transport,
  quicr::messages::MoqObject&& datagram) {}

void ServerDelegate::onSubscribe(const quicr::Namespace& quicr_namespace,
            const uint64_t& subscriber_id,
            const qtransport::TransportContextId& context_id,
            const qtransport::StreamId& stream_id,
            const quicr::SubscribeIntent subscribe_intent,
            const std::string& origin_url,
            bool use_reliable_transport,
            const std::string& auth_token,
            quicr::bytes&& data)
{
}


void ServerDelegate::onUnsubscribe(const quicr::Namespace& quicr_namespace,
              const uint64_t& subscriber_id,
              const std::string& auth_token)
{

}