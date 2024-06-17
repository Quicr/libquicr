#include <cxxopts.hpp>
#include <quicr/quicr_client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>

std::condition_variable cv;
std::mutex mutex;
std::atomic_bool can_publish = false;

struct PerfPublishDelegate : public quicr::PublisherDelegate
{
  void onPublishIntentResponse(const quicr::Namespace&,
                               const quicr::PublishIntentResult&) override
  {
    can_publish = true;
    cv.notify_all();
  }
};

template<typename D, typename I, typename F, typename... Args>
void
loop_for(const D& duration, const I& interval, const F& func, Args&&... args)
{
  I t = I::zero();
  while (t < duration) {
    func(std::forward<Args>(args)...);
    std::this_thread::sleep_for(interval);
    t += interval;
  }
}

int
main(int argc, char** argv)
{
  // clang-format off
  cxxopts::Options options("FlowCode");
  options.add_options()
    ("streams", "Number of streams per client", cxxopts::value<size_t>()->default_value("1"))
    ("chunk_size", "Chunk size", cxxopts::value<size_t>()->default_value("3000"))
    ("n,namespace", "Namespace to publish on", cxxopts::value<std::string>())
    ("relay_url", "Relay port to connect on", cxxopts::value<std::string>()->default_value("relay.quicr.ctgpoc.com"))
    ("relay_port", "Relay port to connect on", cxxopts::value<std::uint16_t>()->default_value("33435"))
    ("s,msg_size", "Byte size of message", cxxopts::value<std::uint16_t>()->default_value("1024"))
    ("h,help", "Print usage");
  // clang-format on

  cxxopts::ParseResult result;

  try {
    result = options.parse(argc, argv);
  } catch (const cxxopts::exceptions::exception& e) {
    std::cerr << "Caught exception while parsing arguments: " << e.what()
              << std::endl;
    return EXIT_FAILURE;
  }

  if (result.count("help")) {
    std::cerr << options.help() << std::endl;
    return EXIT_SUCCESS;
  }

  if (!result.count("namespace")) {
    std::cerr << "'namespace' argument is required" << std::endl;
    return EXIT_FAILURE;
  }

  quicr::RelayInfo info{
    .hostname = result["relay_url"].as<std::string>(),
    .port = result["relay_port"].as<std::uint16_t>(),
    .proto = quicr::RelayInfo::Protocol::QUIC,
  };
  qtransport::TransportConfig config{
    .tls_cert_filename = nullptr,
    .tls_key_filename = nullptr,
    .use_reset_wait_strategy = false,
  };

  auto logger = std::make_shared<cantina::Logger>("perf", "[PERF]");
  quicr::Client client(info, "perf@cisco.com", result["chunk_size"].as<size_t>(), config, logger);

  client.connect();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::unique_lock lock(mutex);

  quicr::Namespace ns = { result["namespace"].as<std::string>() };
  client.publishIntent(std::make_shared<PerfPublishDelegate>(),
                       ns,
                       "",
                       "",
                       {},
                       quicr::TransportMode::ReliablePerTrack);
  cv.wait(lock, [&] { return can_publish.load(); });

  LOGGER_INFO(logger, "Running test for the next 2 minutes...");

  constexpr auto now = [] {
    return std::chrono::time_point_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now());
  };
  const std::uint16_t msg_size = result["msg_size"].as<std::uint16_t>();

  quicr::Name name = ns;
  ::loop_for(std::chrono::minutes(2), std::chrono::milliseconds(1), [&] {
    std::vector<uint8_t> buffer(msg_size);
    std::generate(buffer.begin(), buffer.end(), std::rand);

    const auto start_time = now();
    client.publishNamedObject(
      name, 1, 500, std::move(buffer), { { "perf:publish", start_time } });
    name = ns.name() | (~(~0x0_name << (128 - ns.length())) & (name + 1));
  });

  LOGGER_INFO(logger, "Test complete, exiting...");

  return EXIT_SUCCESS;
}
