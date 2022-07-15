#include <vector>
#include <string>
#include <memory>

#include <doctest/doctest.h>
#include <quicr/quicr_client.h>
#include "../src/quicr_quic_transport.h"

using namespace quicr;

struct TestDelegate: public QuicRClient::Delegate
{
  void on_data_arrived(const std::string& name,
                       bytes&& data,
                       uint64_t group_id,
                       uint64_t object_id) override
  {}

  void on_connection_close(const std::string& name)
  {}

  void on_object_published(const std::string& name, uint64_t group_id, uint64_t object_id)
  {}
};

TEST_CASE("Object Lifetime")
{
  TestDelegate delegate;
  CHECK_NOTHROW(std::make_unique<QuicRClient>(delegate, "127.0.0.1", 1245));
}

TEST_CASE("Register/Unregister Publish Names")
{
  TestDelegate delegate;
  {
    auto qr_client = std::make_unique<QuicRClient>(delegate, "127.0.0.1", 1245);
    qr_client->register_names(std::vector<std::string> {"1", "2"}, true);
    CHECK_NOTHROW(qr_client->close());
  }

  {
    auto qr_client = std::make_unique<QuicRClient>(delegate, "127.0.0.1", 1245);
    qr_client->register_names(std::vector<std::string> {"1", "2"}, true);
    CHECK_NOTHROW(qr_client->unregister_names(std::vector<std::string> {"1", "2"}));
  }

  // unregister non existing names
  {
    auto qr_client = std::make_unique<QuicRClient>(delegate, "127.0.0.1", 1245);
    CHECK_NOTHROW(qr_client->unregister_names(std::vector<std::string> {"1", "2"}));
  }
}

TEST_CASE("Subscribe/Unsubscribe Names")
{
  TestDelegate delegate;
  {
    auto qr_client = std::make_unique<QuicRClient>(delegate, "127.0.0.1", 1245);
    qr_client->subscribe(std::vector<std::string>{"1", "2"}, false, false);
    CHECK_NOTHROW(qr_client->close());
  }


}

