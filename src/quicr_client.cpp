#include <queue>

#include <quicr/quicr_client.h>
#include "quicr_transport.h"

namespace quicr {

///
/// Transport
///

struct QuicRClient::Transport
{
  explicit Transport(Delegate& delegate_in, const std::string& server,
                     const uint16_t port) :
                      delegate(delegate_in),
                      quicr_transport(std::make_unique<internal::QuicRTransport>(delegate_in, server, port))
  {}

  const Delegate& delegate;
  std::unique_ptr<internal::QuicRTransport> quicr_transport;
};


///
/// QuicrClient
///

QuicRClient::QuicRClient(QuicRClient::Delegate& delegate_in,
                         const std::string& server,
                         const uint16_t port)
  : delegate(delegate_in)
  , transport_handle(
      std::make_unique<Transport>(delegate_in, server, port))
{}

void
QuicRClient::register_names(const std::vector<std::string>& name, bool /*use_reliable_transport*/)
{
  transport_handle->quicr_transport->register_publish_sources(name);
}

void
QuicRClient::unregister_names(const std::vector<std::string>& /*names*/)
{}

void
QuicRClient::publish_named_data(const std::string& name,
                                bytes&& data_in,
                                uint8_t /*priority*/,
                                uint64_t /*best_before*/)
{
  auto data = internal::QuicRTransport::Data{name, std::move(data_in) };
  transport_handle->quicr_transport->publish_named_data(name, std::move(data));
}

void
QuicRClient::subscribe(const std::vector<std::string>& names,
                       bool /*use_reliable_transport*/,
                       bool /*in_order_delivery*/)
{
  transport_handle->quicr_transport->subscribe(names);
}

void
QuicRClient::unsubscribe(const std::vector<std::string>& names)
{
  transport_handle->quicr_transport->unsubscribe(names);
}

}