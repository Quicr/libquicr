#pragma once

#include "../quicr_client_raw_session.h"

#include "../helpers.h"
#include "quicr/quicr_common.h"

#include <chrono>
#include <stdint.h>
#include <thread>

namespace quicr {

class ClientRawSession_Datagram : public QuicRClientRawSession
{
public:
  using QuicRClientRawSession::QuicRClientRawSession;

protected:
  virtual std::optional<std::pair<Namespace, PublishContext&>>
  findPublishStream(Name name)
  {
    if (auto found = publish_state.find(name); found != publish_state.end()) {
      return *found;
    }

    return std::nullopt;
  }

  virtual void createPublishStream(PublishContext& context, bool)
  {
    if (context.state == PublishContext::State::Ready)
      return;

    context.state = PublishContext::State::Ready;
  }

  virtual bool detectJump(Name a, Name b) const
  {
    const uint32_t group_id_jump =
      a.bits<uint32_t>(16, 32) - b.bits<uint32_t>(16, 32);
    const uint16_t object_id_jump =
      a.bits<uint16_t>(0, 16) - b.bits<uint16_t>(0, 16);
    return group_id_jump > 1u || (group_id_jump == 0 && object_id_jump > 1u);
  }

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

    // Fragment the payload if needed
    if (data.size() <= quicr::MAX_TRANSPORT_DATA_SIZE) {
      messages::MessageBuffer msg;

      datagram.media_data_length = static_cast<uintVar_t>(data.size());
      datagram.media_data = std::move(data);

      msg << datagram;

      // No fragmenting needed
      transport->enqueue(
        transport_context_id, context.stream_id, msg.take(), priority, expiry_age_ms);
      return;
    }

    // Fragments required. At this point this only counts whole blocks
    int frag_num = data.size() / quicr::MAX_TRANSPORT_DATA_SIZE;
    int frag_remaining_bytes = data.size() % quicr::MAX_TRANSPORT_DATA_SIZE;

    int offset = 0;

    while (frag_num-- > 0) {
      messages::MessageBuffer msg;

      if (frag_num == 0 && !frag_remaining_bytes) {
        datagram.header.offset_and_fin = (offset << 1) + 1;
      } else {
        datagram.header.offset_and_fin = offset << 1;
      }

      bytes frag_data(data.begin() + offset,
                      data.begin() + offset + quicr::MAX_TRANSPORT_DATA_SIZE);

      datagram.media_data_length = frag_data.size();
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      offset += quicr::MAX_TRANSPORT_DATA_SIZE;

      /*
       * For UDP based transports, some level of pacing is required to prevent
       * buffer overruns throughput the network path and with the remote end.
       *  TODO: Fix... This is set a bit high because the server code is
       * running too slow
       */
      if (need_pacing && (frag_num % 30) == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

      if (transport->enqueue(transport_context_id,
                             context.stream_id,
                             msg.take(),
                             priority,
                             expiry_age_ms) !=
          qtransport::TransportError::None) {
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        // No point in finishing fragment if one is dropped
        return;
      }
    }

    // Send last fragment, which will be less than MAX_TRANSPORT_DATA_SIZE
    if (frag_remaining_bytes) {
      messages::MessageBuffer msg;
      datagram.header.offset_and_fin = uintVar_t((offset << 1) + 1);

      bytes frag_data(data.begin() + offset, data.end());
      datagram.media_data_length = static_cast<uintVar_t>(frag_data.size());
      datagram.media_data = std::move(frag_data);

      msg << datagram;

      if (auto err = transport->enqueue(transport_context_id,
                                        context.stream_id,
                                        msg.take(),
                                        priority,
                                        expiry_age_ms);
          err != qtransport::TransportError::None) {
        LOG_WARNING(
          logger, "Published object delayed due to enqueue error " << int(err));
        std::this_thread::sleep_for(std::chrono::microseconds(100));
      }
    }
  }
};

}
