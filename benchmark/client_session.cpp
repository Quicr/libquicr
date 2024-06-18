#include <benchmark/benchmark.h>

#include <quicr/quicr_client.h>

#include <atomic>
#include <condition_variable>

static auto logger = std::make_shared<cantina::CustomLogger>([](auto, const std::string&, bool) {});

std::condition_variable cv;
std::mutex mutex;
std::atomic_bool can_publish = false;

struct BenchmarkPublishDelegate : public quicr::PublisherDelegate
{
  void onPublishIntentResponse(const quicr::Namespace&,
                               const quicr::PublishIntentResult&) override
  {
    can_publish = true;
    cv.notify_all();
  }
};

class ClientFixture : public benchmark::Fixture
{
public:
  const quicr::RelayInfo info{
    .hostname = "relay.quicr.ctgpoc.com",
    .port = 33435,
    .proto = quicr::RelayInfo::Protocol::QUIC,
    .relay_id = "",
  };
  const qtransport::TransportConfig config{
    .tls_cert_filename = nullptr,
    .tls_key_filename = nullptr,
    .use_reset_wait_strategy = false,
    .quic_qlog_path = "",
  };

  std::unique_ptr<quicr::Client> client;

  void SetUp(::benchmark::State&)
  {
    client = std::make_unique<quicr::Client>(
      info, "benchmark@cisco.com", 0, config, logger);
    client->connect();
    client->publishIntent(
      std::make_shared<BenchmarkPublishDelegate>(),
      std::string_view{ "0x01020304050607080910111213141516/80" },
      "",
      "",
      {},
      quicr::TransportMode::ReliablePerTrack);
  }

  void TearDown(::benchmark::State&)
  {
    client->disconnect();
    client.reset();
  }
};

BENCHMARK_DEFINE_F(ClientFixture, Publish)(benchmark::State& state)
{
  std::unique_lock lock(mutex);
  cv.wait(lock, [&] { return can_publish.load(); });

  const auto start_time =
    std::chrono::time_point_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now());

  constexpr quicr::Name name = 0x01020304050607080910111213141516_name;

  for (auto _ : state) {
    auto start = std::chrono::high_resolution_clock::now();

    client->publishNamedObject(name,
                               1,
                               500,
                               {},
                               { {
                                 "qController:publishNamedObject",
                                 start_time,
                               } });

    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_seconds =
      std::chrono::duration_cast<std::chrono::duration<double>>(end - start);

    state.SetIterationTime(elapsed_seconds.count());

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

BENCHMARK_REGISTER_F(ClientFixture, Publish)->UseManualTime();
