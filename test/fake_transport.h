#pragma once
#include <transport/transport.h>

using namespace qtransport;

struct FakeTransportDelegate : public ITransport::TransportDelegate
{
  ~FakeTransportDelegate() = default;

  void on_connection_status(const TransportStatus status) override {}

  virtual void on_new_connection(const TransportContextId& context_id) override
  {
  }

  virtual void on_recv_notify(TransportContextId& tcid) override{};
};

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
    stored_data = std::move(bytes);
    return TransportError::None;
  }

  std::optional<std::vector<uint8_t>> dequeue(const TransportContextId& tcid,
                                              const MediaStreamId& msid)
  {
    return std::nullopt;
  }

  std::vector<uint8_t> stored_data;
};