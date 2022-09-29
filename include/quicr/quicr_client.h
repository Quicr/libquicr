#pragma once
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace quicr {

// handy name aliasing
using bytes = std::vector<uint8_t>;

// Log Helpers
enum struct LogLevel : uint8_t
{
  fatal = 1,
  error = 2,
  info = 3,
  warn = 4,
  debug = 5,
};

struct LogHandler
{
  // log() provides logs to the application.  The default implementation is a
  // noop; the inputs are ignored.
  virtual void log(LogLevel /*level*/, const std::string& /*message*/) {}
};

// Names to be used in QuicR protocol.
// Mask is no-op for publish and is
// applicable for subscribes.
struct QuicrName
{
  std::string name;
  size_t mask = 0; // zero implies full name
};

/*
 * typedef enum {
    quicrq_subscribe_intent_current_group = 0,
    quicrq_subscribe_intent_next_group = 1,
    quicrq_subscribe_intent_start_point = 2
} quicrq_subscribe_intent_enum;

typedef struct st_quicrq_subscribe_intent_t {
    quicrq_subscribe_intent_enum intent_mode;
    uint64_t start_group_id;
    uint64_t start_object_id;
} quicrq_subscribe_intent_t;

 */
// Subscribe Intent
struct SubscribeIntent {
    enum class Mode {
        immediate = 0, // Start from the most latest object
        wait_up = 1,   // Start from the following group
        sync_up = 2,   // Start from the request position
    };

    Mode mode;
    uint64_t group_id;  // Applicable when mode == sync_up
    uint64_t object_id; // ^^^^^ Ditto ^^^^
};


struct QuicRClient
{
  // Application delegate to report callbacks from the transport
  struct Delegate : LogHandler
  {
    virtual ~Delegate() = default;

    // Callback from the transport on data reception
    // This callback is invoked on the transport thread
    // Delegate implementations must ensure to not carry out
    // time-consuming computations in this function to not
    // slow down the transport thread.
    virtual void on_data_arrived(const std::string& name,
                                 bytes&& data,
                                 uint64_t group_id,
                                 uint64_t object_id) = 0;

    virtual void on_object_published(const std::string& name,
                                     uint64_t group_id,
                                     uint64_t object_id) = 0;

    virtual void on_connection_close(const std::string& name) = 0;
  };

  QuicRClient(Delegate& delegate,
              const std::string& server,
              const uint16_t port);

  ~QuicRClient();

  // Transport APIs
  bool is_transport_ready();

  // Pub/Sub APIs
  void register_names(const std::vector<QuicrName>& names,
                      bool use_reliable_transport);

  void unregister_names(const std::vector<QuicrName>& names);

  void publish_named_data(const std::string& name,
                          bytes&& data,
                          uint64_t group_id,
                          uint64_t object_id,
                          uint8_t priority,
                          uint64_t best_before);

  void subscribe(const std::vector<QuicrName>& names,
                 SubscribeIntent& intent,
                 bool use_reliable_transport,
                 bool in_order_delivery);

  void unsubscribe(const std::vector<QuicrName>& names);

  void close();

private:
  struct Transport;
  std::unique_ptr<Transport> transport_handle;
};

}
