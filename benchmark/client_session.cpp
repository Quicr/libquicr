#include <benchmark/benchmark.h>

#include <quicr/quicr_client.h>

#include <condition_variable>

static auto logger = std::make_shared<cantina::Logger>("BENCH");

struct BenchmarkPublishDelegate : public quicr::PublisherDelegate
{
  std::function<void()> benchmark_callback;
  void onPublishIntentResponse(const quicr::Namespace&,
                               const quicr::PublishIntentResult&) override
  {
    benchmark_callback();
  }
};

static void
ClientSession_Publish(benchmark::State& state)
{
  quicr::RelayInfo info{
    .hostname = "relay.quicr.ctgpoc.com",
    .port = 33435,
    .proto = quicr::RelayInfo::Protocol::QUIC,
  };
  qtransport::TransportConfig config{
    .tls_cert_filename = nullptr,
    .tls_key_filename = nullptr,
    .use_reset_wait_strategy = false,
  };

  quicr::Client client(info, "benchmark@cisco.com", 0, config, logger);
  client.connect();

    std::this_thread::sleep_for(std::chrono::seconds(2));

  std::condition_variable cv;
  std::mutex mutex;
  auto delegate = std::make_shared<BenchmarkPublishDelegate>();
  delegate->benchmark_callback = [&] {
    const auto start_time =
      std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now());
    std::vector<qtransport::MethodTraceItem> trace;
    for (auto _ : state) {
      trace.push_back({ "qController:publishNamedObject", start_time });
      state.ResumeTiming();
      client.publishNamedObject(0x01020304050607080910111213141516_name,
                                1,
                                500,
                                {},
                                std::move(trace));
      state.PauseTiming();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    cv.notify_all();
  };

  std::unique_lock lock(mutex);
  client.publishIntent(
    delegate,
    std::string_view{ "0x01020304050607080910111213141516/80" },
    "",
    "",
    {},
    quicr::TransportMode::ReliablePerTrack);

  cv.wait(lock);
}

BENCHMARK(ClientSession_Publish);
