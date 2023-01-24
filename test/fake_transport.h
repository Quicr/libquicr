#pragma
#include <transport/transport.h>

using namespace qtransport;
struct FakeTransport : public ITransport
{

  TransportStatus status() { return TransportStatus::Ready; }

  TransportContextId start() { return 0x1000; }

  TransportStatus status() const { return TransportStatus::Ready; }

  MediaStreamId createMediaStream(const TransportContextId& tcid,
                                  bool use_reliable_transport)
  {
    return 0x2000;
  }

  void close(const TransportContextId &context_id) {};

  void closeMediaStream(const TransportContextId &context_id,
                                MediaStreamId mStreamId) {};
  void close() {}

  TransportError enqueue(const TransportContextId& tcid,
                const MediaStreamId& msid,
                std::vector<uint8_t>&& bytes)
  {
    return TransportError::None;
  }

  std::optional<std::vector<uint8_t>> dequeue(const TransportContextId& tcid,
                                              const MediaStreamId& msid)
  {
    return std::nullopt;
  }
};