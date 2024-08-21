#include <cxxopts.hpp>
#include <quicr/quicr_client.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <thread>
#include <unordered_map>

namespace {
std::condition_variable cv;
std::mutex mutex;
std::atomic_bool terminate = false;
std::atomic_size_t sub_responses_received = 0;

struct PerfSubscriberDelegate : public quicr::SubscriberDelegate
{
  std::shared_ptr<spdlog::logger> logger;
  PerfSubscriberDelegate(std::shared_ptr<spdlog::logger> l) : logger{std::move(l)} {}

  void onSubscribeResponse(const quicr::Namespace&,
                           const quicr::SubscribeResult&)
  {
    ++sub_responses_received;
    cv.notify_one();
  }

  void onSubscriptionEnded(const quicr::Namespace&,
                           const quicr::SubscribeResult::SubscribeStatus&)
  {
    SPDLOG_LOGGER_INFO(logger, "Subscription ended");
  }

  void onSubscribedObject(const quicr::Name&, uint8_t, quicr::bytes&& data)
  {
    ++subscribed_objects_received;
    total_bytes_received += data.size();
  }

  void onSubscribedObjectFragment(const quicr::Name&,
                                  uint8_t,
                                  const uint64_t&,
                                  bool,
                                  quicr::bytes&&)
  {
    throw std::runtime_error("Unexpected");
  }

  std::size_t subscribed_objects_received = 0;
  std::size_t total_bytes_received = 0;
};
}

void
handle_terminate_signal(int)
{
  terminate = true;
  cv.notify_all();
}

int
main(int argc, char** argv)
{
  // clang-format off
  cxxopts::Options options("QPerf");
  options.add_options()
    ("n,namespace", "Namespace to publish on", cxxopts::value<std::string>())
    ("endpoint_id", "Name of the client", cxxopts::value<std::string>()->default_value("perf@cisco.com"))
    ("streams", "Number of streams per client", cxxopts::value<std::size_t>()->default_value("1"))
    ("chunk_size", "Chunk size", cxxopts::value<std::size_t>()->default_value("3000"))
    ("relay_url", "Relay port to connect on", cxxopts::value<std::string>()->default_value("relay.quicr.ctgpoc.com"))
    ("relay_port", "Relay port to connect on", cxxopts::value<std::uint16_t>()->default_value("33435"))
    ("p,priority", "Priority for sending publish messages", cxxopts::value<std::uint8_t>()->default_value("1"))
    ("e,expiry_age", "Expiry age of objects in ms", cxxopts::value<std::uint16_t>()->default_value("5000"))
    ("delay", "Startup delay in ms", cxxopts::value<std::uint32_t>()->default_value("1000"))
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

  const quicr::Namespace ns = { result["namespace"].as<std::string>() };
  const std::size_t streams = result["streams"].as<std::size_t>();
  const std::size_t chunk_size = result["chunk_size"].as<std::size_t>();
  const std::uint8_t priority = result["priority"].as<std::uint8_t>();
  const std::uint16_t expiry_age = result["expiry_age"].as<std::uint16_t>();
  const std::chrono::milliseconds delay(result["delay"].as<std::uint32_t>());

  const quicr::RelayInfo info{
    .hostname = result["relay_url"].as<std::string>(),
    .port = result["relay_port"].as<std::uint16_t>(),
    .proto = quicr::RelayInfo::Protocol::QUIC,
    .relay_id = "",
  };

  const qtransport::TransportConfig config{
    .tls_cert_filename = nullptr,
    .tls_key_filename = nullptr,
    .time_queue_max_duration = expiry_age,
    .use_reset_wait_strategy = false,
  };

  const auto logger = spdlog::stderr_color_mt("SUBPERF");
  quicr::Client client(
    info, result["endpoint_id"].as<std::string>(), chunk_size, config, logger);

  try {
    if (!client.connect()) {
      SPDLOG_LOGGER_CRITICAL(logger, "Failed to connect to relay '{0}:{1}'", info.hostname , info.port);
      return EXIT_FAILURE;
    }
  } catch (...) {
    return EXIT_FAILURE;
  }

  std::this_thread::sleep_for(delay);

  std::signal(SIGINT, handle_terminate_signal);

  quicr::namespace_map<std::shared_ptr<PerfSubscriberDelegate>> delegates;

  for (std::uint16_t i = 0; i < streams; ++i) {
    auto [entry, _] = delegates.emplace(
      quicr::Namespace{ns.name() + ((0x0_name + i) << (128 - ns.length())), ns.length()}, std::make_shared<PerfSubscriberDelegate>(logger));

    auto& [sub_ns, delegate] = *entry;
    client.subscribe(delegate,
                     sub_ns,
                     quicr::SubscribeIntent::immediate,
                     quicr::TransportMode::ReliablePerGroup,
                     "",
                     "",
                     {},
                     priority);
  }

  std::unique_lock lock(mutex);
  cv.wait(lock, [&] { return terminate.load() || sub_responses_received == streams; });

  if (terminate) return EXIT_FAILURE;

  SPDLOG_LOGGER_INFO(logger, "+==========================================+");
  SPDLOG_LOGGER_INFO(logger, "| Starting test");
  SPDLOG_LOGGER_INFO(logger, "+------------------------------------------+");
  SPDLOG_LOGGER_INFO(logger, "| *             Streams: {0}", streams);
  SPDLOG_LOGGER_INFO(logger, "| * Total Subscriptions: {0}", sub_responses_received.load());
  SPDLOG_LOGGER_INFO(logger, "+==========================================+");

  const auto start = std::chrono::high_resolution_clock::now();

  SPDLOG_LOGGER_INFO(logger, "Press Ctrl + C to end the test");
  cv.wait(lock, [&] { return terminate.load(); });

  const auto end = std::chrono::high_resolution_clock::now();
  const auto elapsed =
    std::chrono::duration_cast<std::chrono::seconds>(end - start);

  std::size_t total_bytes_received = 0;
  std::for_each(delegates.begin(), delegates.end(), [&](const auto& entry) {
    total_bytes_received += entry.second->total_bytes_received;
  });

  SPDLOG_LOGGER_INFO(logger, "+==========================================+");
  SPDLOG_LOGGER_INFO(logger, "| Test complete");
  SPDLOG_LOGGER_INFO(logger, "+------------------------------------------+");
  SPDLOG_LOGGER_INFO(logger, "| *             Duration: {0} seconds", elapsed.count());
  SPDLOG_LOGGER_INFO(logger, "| * Total Bytes received: {0}", total_bytes_received);
  SPDLOG_LOGGER_INFO(logger, "+==========================================+");

  return EXIT_SUCCESS;
}
