// Stub code until we have a real transport to use

#pragma once

#include "transport/transport.h"

struct FakeTransport : public qtransport::ITransport
{

  qtransport::TransportStatus status()
  {
    return qtransport::TransportStatus::Ready;
  }

  qtransport::TransportContextId start() { return 0x1000; }

  qtransport::TransportStatus status() const
  {
    return qtransport::TransportStatus::Ready;
  }

  qtransport::MediaStreamId createMediaStream(
    const qtransport::TransportContextId& tcid,
    bool use_reliable_transport)
  {
    return 0x2000;
  }

  void close(const qtransport::TransportContextId& context_id){};

  void closeMediaStream(const qtransport::TransportContextId& context_id,
                        qtransport::MediaStreamId mStreamId){};
  void close() {}

  qtransport::TransportError enqueue(const qtransport::TransportContextId& tcid,
                                     const qtransport::MediaStreamId& msid,
                                     std::vector<uint8_t>&& bytes)
  {
    stored_data = std::move(bytes);
    return qtransport::TransportError::None;
  }

  std::optional<std::vector<uint8_t>> dequeue(
    const qtransport::TransportContextId& tcid,
    const qtransport::MediaStreamId& msid)
  {
    return std::nullopt;
  }

  std::vector<uint8_t> stored_data;
};
