#pragma once
#include <transport/transport.h>

using namespace qtransport;

struct FakeTransportDelegate : public ITransport::TransportDelegate
{
  ~FakeTransportDelegate() = default;

  virtual void on_connection_status(const TransportContextId& /* context_id */,
                                    const TransportStatus /* status */) override
  {
  }

  virtual void on_new_connection(const TransportContextId& /* context_id */,
                                 const TransportRemote& /* remote */) override
  {
  }

  virtual void on_new_stream(const TransportContextId& /* context_id */,
                                   const StreamId& /* streamId */) override
  {
  }

  void on_recv_notify(const TransportContextId& /* context_id */,
                      const StreamId& /* streamId */) override
  {
  }
};

struct FakeTransport : public ITransport
{

  TransportStatus status() { return TransportStatus::Ready; }

  TransportContextId start() { return 0x1000; }

  TransportStatus status() const { return TransportStatus::Ready; }

  StreamId createStream(const TransportContextId& /* tcid */,
                        bool /* use_reliable_transport */)
  {
    return 0x2000;
  }

  void close(const TransportContextId& /* context_id */){};

  void closeStream(const TransportContextId& /* context_id */,
                        StreamId /* streamId */){};
  void close() {}

  TransportError enqueue(const TransportContextId& /* tcid */,
                         const StreamId& /* sid */,
                         std::vector<uint8_t>&& bytes,
                         const uint8_t,
                         const uint32_t)
  {
    stored_data = std::move(bytes);
    return TransportError::None;
  }

  std::optional<std::vector<uint8_t>> dequeue(const TransportContextId& /* tcid */,
                                              const StreamId& /* sid */)
  {
    return std::nullopt;
  }

  std::vector<uint8_t> stored_data;
};