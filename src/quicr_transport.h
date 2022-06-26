#pragma once

#include <cassert>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <netinet/in.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <vector>

#include <map>

// picoquic/quicr dependencies
#include "picoquic.h"
#include "picoquic_config.h"
#include "picoquic_utils.h"
#include "quicrq_reassembly.h"
#include "quicrq_relay.h"

#include <quicr/quicr_client.h>

namespace quicr::internal {

class QuicRTransport;

// Context shared with th the underlying quicr stack
struct TransportContext
{
  quicrq_ctx_t* qr_ctx;
  quicrq_cnx_ctx_t* cn_ctx;
  QuicRTransport* transport;
};

struct QuicRClientContext
{
  std::string server_name;
  uint16_t port;
  struct sockaddr_storage server_address;
  socklen_t server_address_len;
  quicrq_ctx_t* qr_ctx;
};

struct PublisherContext
{
  std::string quicr_name;
  quicrq_media_object_source_ctx_t* object_source_ctx; // used with/object api
  QuicRTransport* transport;
};

struct ConsumerContext
{
  std::string quicr_name;
  quicrq_reassembly_context_t reassembly_ctx;
  quicrq_object_stream_consumer_ctx* object_consumer_ctx;
  quicrq_cnx_ctx_t* cnx_ctx;
  QuicRTransport* transport;
};

///
/// QuicrTransport - Manages QuicR Protocol
///
class QuicRTransport
{
public:
  // Payload and metadata
  struct Data
  {
    std::string quicr_name;
    bytes app_data;
  };

  QuicRTransport(QuicRClient::Delegate& delegate_in,
                 const std::string& sfuName_in,
                 const uint16_t sfuPort_in);

  ~QuicRTransport();

  void register_publish_sources(const std::vector<std::string>& publishers);
  void unregister_publish_sources(const std::vector<std::string>& publishers);
  void publish_named_data(const std::string& url, Data&& data);
  void subscribe(const std::vector<std::string>& names);
  void unsubscribe(const std::vector<std::string>& names);
  void start();
  bool ready();
  void shutdown();

  // callback registered with the underlying quicr stack
  static int quicr_callback(picoquic_cnx_t* cnx,
                            uint64_t stream_id,
                            uint8_t* bytes,
                            size_t length,
                            picoquic_call_back_event_t fin_or_event,
                            void* callback_ctx,
                            void* v_stream_ctx);

  // Reports if the underlying quic stack is ready
  // for application messages
  std::mutex quicConnectionReadyMutex;
  bool quicConnectionReady;

  // Thread and its function managing quic context.
  std::thread quicTransportThread;
  int runQuicProcess();
  static int quicTransportThreadFunc(QuicRTransport* netTransportQuic)
  {
    if (!netTransportQuic) {
      throw std::runtime_error("Transpor not initialized");
    }
    return netTransportQuic->runQuicProcess();
  }

  const PublisherContext& get_publisher_context(const std::string& name) const
  {
    return publishers.at(name);
  }

  void wake_up_all_sources();

  // APis interacting with unerlying quic stack for sending and receiving the
  // data
  bool hasDataToSendToNet();
  bool getDataToSendToNet(Data& data_out);
  void recvDataFromNet(Data& data_in);

private:
  // Queue of data to be sent
  std::queue<Data> sendQ;
  std::mutex sendQMutex;

  TransportContext transport_context;
  QuicRClientContext quicr_client_ctx;

  // todo : implement RAII mechanism
  picoquic_quic_config_t config;
  quicrq_ctx_t* quicr_ctx;
  quicrq_cnx_ctx_t* cnx_ctx = nullptr;
  picoquic_quic_t* quic = nullptr;

  // source_id -> pub_ctx
  std::map<std::string, PublisherContext> publishers = {};
  // source_id -> consumer_ctx
  std::map<std::string, ConsumerContext> consumers = {};

  // Handler of transport events from the application
  QuicRClient::Delegate& application_delegate;

  LogHandler& logger;
};

} // namespace quicr