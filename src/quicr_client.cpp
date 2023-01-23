#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>

namespace quicr {

quicr::QuicRClient::QuicRClient(
  ITransport& transport_in,
  std::shared_ptr<SubscriberDelegate> subscriber_delegate,
  std::shared_ptr<PublisherDelegate> pub_delegate)
  : transport(transport_in)
  , sub_delegate(subscriber_delegate)
  , pub_delegate(pub_delegate)
{
}

QuicRClient::QuicRClient(
  ITransport& transport_in,
  std::shared_ptr<SubscriberDelegate> subscriber_delegate)
  : transport(transport_in)
  , sub_delegate(subscriber_delegate)
{
}

QuicRClient::QuicRClient(ITransport& transport_in,
                         std::shared_ptr<PublisherDelegate> pub_delegate)
  : transport(transport_in)
  , pub_delegate(pub_delegate)
{
}

bool
QuicRClient::publishIntent(const QUICRNamespace& quicr_namespace,
                           const std::string& origin_url,
                           const std::string& auth_token,
                           bytes&& payload)
{
  return false;
}

void
QuicRClient::publishIntentEnd(const QUICRNamespace& quicr_namespace,
                              const std::string& auth_token)
{
}

void
QuicRClient::subscribe(const QUICRNamespace& quicr_namespace,
                       const SubscribeIntent& intent,
                       const std::string& origin_url,
                       bool use_reliable_transport,
                       const std::string& auth_token,
                       bytes&& e2e_token)
{
  /*
   *
   */
}

void
QuicRClient::unsubscribe(const QUICRNamespace& quicr_namespace,
                         const std::string& origin_url,
                         const std::string& auth_token)
{
}

void
QuicRClient::publishNamedObject(const QUICRName& quicr_name,
                                uint8_t priority,
                                uint16_t expiry_age_ms,
                                bool use_reliable_transport,
                                bytes&& data)
{
}

void
QuicRClient::publishNamedObjectFragment(const QUICRName& quicr_name,
                                        uint8_t priority,
                                        uint16_t expiry_age_ms,
                                        bool use_reliable_transport,
                                        const uint64_t& offset,
                                        bool is_last_fragment,
                                        bytes&& data)
{
}

} // namespace quicr