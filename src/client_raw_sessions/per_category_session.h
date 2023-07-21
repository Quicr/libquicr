#pragma once

#include "../helpers.h"
#include "../quicr_client_raw_session.h"

namespace quicr {

class ClientRawSession_PerCategory : public QuicRClientRawSession
{
public:
  using QuicRClientRawSession::QuicRClientRawSession;

protected:
  virtual void sendPublishData(const quicr::Name& name,
                               const PublishContext& context,
                               uint8_t priority,
                               uint16_t expiry_age_ms,
                               bytes&& data)
  {
    messages::PublishDatagram datagram;
    datagram.header = {
      .media_id = context.stream_id,
      .name = name,
      .group_id = context.name.bits<uint32_t>(16, 32),
      .object_id = context.name.bits<uint16_t>(0, 16),
      .offset_and_fin = 1,
      .flags = 0x0,
    };

    datagram.media_type = messages::MediaType::RealtimeMedia;
    datagram.media_data_length = static_cast<uintVar_t>(data.size());
    datagram.media_data = std::move(data);

    messages::MessageBuffer msg(sizeof(datagram) + data.size());
    msg << datagram;

    transport->enqueue(
      transport_context_id, context.stream_id, msg.take(), priority, expiry_age_ms);
  }
};

}
