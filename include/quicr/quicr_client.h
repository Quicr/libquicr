#pragma once
#include <mutex>
#include <string>
#include <vector>
#include <memory>

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
  void register_names(const std::vector<std::string>& names,
                      bool use_reliable_transport);

  void unregister_names(const std::vector<std::string>& names);

  void publish_named_data(const std::string& name,
                          bytes&& data,
                          uint8_t priority,
                          uint64_t best_before);

  void subscribe(const std::vector<std::string>& names,
                 bool use_reliable_transport,
                 bool in_order_delivery);

  void unsubscribe(const std::vector<std::string>& names);

  void close();

private:
  struct Transport;
  std::unique_ptr<Transport> transport_handle;
};

}
