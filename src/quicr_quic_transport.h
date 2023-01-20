#pragma once

#include <atomic>
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

class QuicRQTransport;

// Context shared with th the underlying quicr stack
struct TransportContext
{
  quicrq_ctx_t* qr_ctx;
  quicrq_cnx_ctx_t* cn_ctx;
  QuicRQTransport* transport;
};

struct PublisherContext
{
  std::string quicr_name;
  quicrq_media_object_source_ctx_t* object_source_ctx; // used with/object api
  QuicRQTransport* transport;
};

struct WildCardSubscribeContext
{
  QuicrName name;
  SubscribeIntent intent;
  QuicRQTransport* transport;
  std::vector<std::string> mapped_names = {};
  quicrq_cnx_ctx_t* cnx_ctx;
  quicrq_stream_ctx_t* stream_ctx;
};

struct ConsumerContext
{
  std::string quicr_name;
  quicrq_object_stream_consumer_ctx* object_consumer_ctx;
  QuicRQTransport* transport;
};

///
/// QuicrTransport - Manages QuicR Protocol over QUIC
///
class QuicRQTransport
{
public:
  // Payload and metadata
  struct Data
  {
    std::string quicr_name;
    uint64_t group_id;
    uint64_t object_id;
    uint8_t priority;
    bytes app_data;
  };

  QuicRQTransport(QuicRClient::Delegate& delegate_in,
                  const std::string& sfuName_in,
                  const uint16_t sfuPort_in);

  ~QuicRQTransport();

  void register_publish_sources(
    const std::vector<quicr::QuicrName>& publishers);
  void unregister_publish_sources(
    const std::vector<quicr::QuicrName>& publishers);
  void publish_named_data(const std::string& url, Data&& data);
  void subscribe(const std::vector<quicr::QuicrName>& names,
                 SubscribeIntent& intent);
  void unsubscribe(const std::vector<quicr::QuicrName>& names);
  void start();
  bool ready();
  void close();
  void set_congestion_control_status(bool status);

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
  static int quicTransportThreadFunc(QuicRQTransport* netTransportQuic)
  {
    if (!netTransportQuic) {
      throw std::runtime_error("Transpor not initialized");
    }
    return netTransportQuic->runQuicProcess();
  }

  // delegate helpers
  void on_object_published(const std::string& name,
                           uint64_t group_id,
                           uint64_t object_id);

  // APIs interacting with underlying quic stack for
  // sending and receiving the data
  bool hasDataToSendToNet();
  bool getDataToSendToNet(Data& data_out);
  void recvDataFromNet(Data& data_in);
  void on_media_close(ConsumerContext* cons_ctx);
  const PublisherContext& get_publisher_context(const std::string& name) const
  {
    return publishers.at(name);
  }

  void on_pattern_match(WildCardSubscribeContext* ctx, std::string&& name);

  std::atomic<bool> shutting_down = false;
  std::atomic<bool> closed = false;

  // used by quic stack callbacks
  template<typename... Ts>
  void log(const Ts&... vals) const;

private:
  void subscribe(const std::string& name, SubscribeIntent& intent);
  void unsubscribe(const std::string& name);
  void unsubscribe(const std::vector<std::string>& name);

  // Queue of data to be published
  std::queue<Data> sendQ;
  std::mutex sendQMutex;

  // todo : implement RAII
  picoquic_quic_config_t config;
  quicrq_ctx_t* quicr_ctx = nullptr;
  quicrq_cnx_ctx_t* cnx_ctx = nullptr;
  picoquic_quic_t* quic = nullptr;

  // Underlying context for the transport
  TransportContext transport_context;
  // source -> pub_ctx
  std::map<std::string, PublisherContext> publishers = {};
  // source_ -> consumer_ctx
  std::map<std::string, ConsumerContext> consumers = {};
  // subscriber patterns
  std::vector<WildCardSubscribeContext*> wildcard_patterns = {};
  // Handler of transport events from the application
  QuicRClient::Delegate& application_delegate;
  LogHandler& logger;
};

} // namespace quicr