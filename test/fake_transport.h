#pragma once
#include <transport/transport.h>

using namespace qtransport;

struct FakeTransportDelegate : public ITransport::TransportDelegate
{
  ~FakeTransportDelegate() = default;

  virtual void on_connection_status(const TransportConnId& /* conn_id */,
                                    const TransportStatus /* status */) override
  {
  }

  virtual void on_new_connection(const TransportConnId& /* conn_id */,
                                 const TransportRemote& /* remote */) override
  {
  }

    void on_recv_notify(const TransportConnId& /* conn_id */,
                        const DataContextId& /* data_ctx_id */,
                        const bool /* is_bidir */) override
  {
  }

  void on_new_data_context(const qtransport::TransportConnId& /* conn_id */,
                           const qtransport::DataContextId& /* data_ctx_id */) override
  {
  }
};

struct FakeTransport : public ITransport
{

  TransportStatus status() const override { return TransportStatus::Ready; }

  TransportConnId start() override { return 0x1000; }

  DataContextId createDataContext([[maybe_unused]] const TransportConnId conn_id,
                                  [[maybe_unused]] bool use_reliable_transport,
                                  [[maybe_unused]] uint8_t priority = 1,
                                  [[maybe_unused]] bool bidir = false) override
  {
    return 0x2000;
  }

  void close(const TransportConnId& /* conn_id */) override {};

  void deleteDataContext([[maybe_unused]] const TransportConnId& conn_id,
                         [[maybe_unused]] DataContextId data_ctx_id) override
  {

  }

  void close() {}

  bool getPeerAddrInfo(const TransportConnId& /*conn_id*/,
                       sockaddr_storage* /*addr*/) override
  {
    return false;
  }

  TransportError enqueue(const TransportConnId& /* tcid */,
                         const DataContextId& /* sid */,
                         std::vector<uint8_t>&& bytes,
                         std::vector<MethodTraceItem>&&,
                         const uint8_t,
                         const uint32_t,
                         const EnqueueFlags) override
  {
    stored_data = std::move(bytes);
    return TransportError::None;
  }

  std::optional<std::vector<uint8_t>> dequeue(
    const TransportConnId& /* tcid */,
    const DataContextId& /* sid */) override
  {
    return std::nullopt;
  }

  std::vector<uint8_t> stored_data;
};