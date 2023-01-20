#include "quicr_quic_transport.h"
#include <queue>
#include <quicr/quicr_client_old.h>

namespace quicr {

///
/// Transport
///

struct QuicRClient::Transport
{
  // TODO: Support multiple transports
  explicit Transport(Delegate& delegate_in,
                     const std::string& server,
                     const uint16_t port)
    : delegate(delegate_in)
    , quicr_transport(
        std::make_unique<internal::QuicRQTransport>(delegate_in, server, port))
  {
    // kick off the transport loop
    quicr_transport->start();
  }

  ~Transport() = default;

  const Delegate& delegate;
  std::unique_ptr<internal::QuicRQTransport> quicr_transport;
};

///
/// QuicrClient
///

QuicRClient::QuicRClient(QuicRClient::Delegate& delegate_in,
                         const std::string& server,
                         const uint16_t port)
  : transport_handle(std::make_unique<Transport>(delegate_in, server, port))
{
}

QuicRClient::~QuicRClient() = default;

void
QuicRClient::register_names(const std::vector<QuicrName>& names,
                            bool /*use_reliable_transport*/)
{
  transport_handle->quicr_transport->register_publish_sources(names);
}

void
QuicRClient::unregister_names(const std::vector<QuicrName>& names)
{
  transport_handle->quicr_transport->unregister_publish_sources(names);
}

void
QuicRClient::publish_named_data(const std::string& name,
                                bytes&& data_in,
                                uint64_t group_id,
                                uint64_t object_id,
                                uint8_t priority,
                                uint64_t /*best_before*/)
{
  auto data = internal::QuicRQTransport::Data{
    name, group_id, object_id, priority, std::move(data_in)
  };
  transport_handle->quicr_transport->publish_named_data(name, std::move(data));
}

void
QuicRClient::subscribe(const std::vector<QuicrName>& names,
                       SubscribeIntent& intent,
                       bool /*use_reliable_transport*/,
                       bool /*in_order_delivery*/)
{
  transport_handle->quicr_transport->subscribe(names, intent);
}

void
QuicRClient::unsubscribe(const std::vector<QuicrName>& names)
{
  transport_handle->quicr_transport->unsubscribe(names);
}

bool
QuicRClient::is_transport_ready()
{
  return transport_handle->quicr_transport->ready();
}

void
QuicRClient::close()
{
  return transport_handle->quicr_transport->close();
}

void
QuicRClient::set_congestion_control_status(bool status)
{
  transport_handle->quicr_transport->set_congestion_control_status(status);
}

}