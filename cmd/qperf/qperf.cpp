#include <cxxopts.hpp>
#include <quicr/quicr_client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <stop_token>
#include <thread>

namespace {
std::condition_variable cv;
std::mutex mutex;
std::atomic_bool terminate = false;
std::atomic_size_t publish_intents_received = 0;

struct PerfPublishDelegate : public quicr::PublisherDelegate
{
  void onPublishIntentResponse(const quicr::Namespace&,
                               const quicr::PublishIntentResult&) override
  {
    ++publish_intents_received;
    cv.notify_one();
  }
};

template<typename D, typename I, typename F, typename... Args>
inline void
loop_for(const D& duration, const I& interval, const F& func, Args&&... args)
{
  auto t = I::zero();
  while (!terminate && t < duration) {
    const auto start = std::chrono::high_resolution_clock::now();
    func(std::forward<Args>(args)...);
    const auto end = std::chrono::high_resolution_clock::now();

    std::this_thread::sleep_for(interval -
                                std::chrono::duration_cast<I>(end - start));
    t += interval;
  }
}

qtransport::time_stamp_us
now()
{
  return std::chrono::time_point_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now());
};

std::string
format_bitrate(const std::uint32_t& bitrate)
{
  if (bitrate > 1e9) {
    return std::to_string(double(bitrate) / 1e9) + " Gbps";
  }
  if (bitrate > 1e6) {
    return std::to_string(double(bitrate) / 1e6) + " Mbps";
  }
  if (bitrate > 1e3) {
    return std::to_string(double(bitrate) / 1e3) + " Kbps";
  }

  return std::to_string(bitrate) + " bps";
}
}

void
handle_terminate_signal(int)
{
  terminate = true;
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
    ("s,msg_size", "Byte size of message", cxxopts::value<std::uint16_t>()->default_value("1024"))
    ("relay_url", "Relay port to connect on", cxxopts::value<std::string>()->default_value("relay.quicr.ctgpoc.com"))
    ("relay_port", "Relay port to connect on", cxxopts::value<std::uint16_t>()->default_value("33435"))
    ("d,duration", "The duration of the test in seconds", cxxopts::value<std::uint32_t>()->default_value("120"))
    ("i,interval", "The interval in microseconds to send publish messages", cxxopts::value<std::uint32_t>()->default_value("1000"))
    ("p,priority", "Priority for sending publish messages", cxxopts::value<std::uint8_t>()->default_value("1"))
    ("e,expiry_age", "Expiry age of objects in ms", cxxopts::value<std::uint16_t>()->default_value("5000"))
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
  const std::uint16_t msg_size = result["msg_size"].as<std::uint16_t>();
  const std::uint8_t priority = result["priority"].as<std::uint8_t>();
  const std::uint16_t expiry_age = result["expiry_age"].as<std::uint16_t>();
  const std::chrono::microseconds interval(
    result["interval"].as<std::uint32_t>());
  const std::chrono::seconds duration(result["duration"].as<std::uint32_t>());

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

  auto logger = std::make_shared<cantina::Logger>("perf", "PERF");
  quicr::Client client(
    info, result["endpoint_id"].as<std::string>(), chunk_size, config, logger);

  try {
    if (!client.connect()) {
      LOGGER_CRITICAL(logger,
                      "Failed to connect to relay '" << info.hostname << ":"
                                                     << info.port << "'");
      return EXIT_FAILURE;
    }
  } catch (...) {
    return EXIT_FAILURE;
  }

  std::signal(SIGINT, handle_terminate_signal);

  std::vector<quicr::Namespace> namespaces;

  for (std::uint16_t i = 0; i < streams; ++i) {
    auto& publish_ns = namespaces.emplace_back(
      ns.name() + ((0x0_name + i) << (128 - ns.length())), ns.length());

    client.publishIntent(std::make_shared<PerfPublishDelegate>(),
                         publish_ns,
                         "",
                         "",
                         {},
                         quicr::TransportMode::ReliablePerTrack);
  }

  std::unique_lock lock(mutex);
  cv.wait(lock, [&] { return publish_intents_received.load() == streams; });

  const std::uint64_t bitrate = (msg_size * 8) / (interval.count() / 1e6);
  const std::uint64_t expected_objects = 1e6 / interval.count();

  // clang-format off
  LOGGER_INFO(logger, "+==========================================+");
  LOGGER_INFO(logger, "| Starting test of duration " << duration.count() << " seconds");
  LOGGER_INFO(logger, "+-------------------------------------------");
  LOGGER_INFO(logger, "| *            Streams: " << streams);
  LOGGER_INFO(logger, "| *     Approx bitrate: " << format_bitrate(bitrate));
  LOGGER_INFO(logger, "| *      Total bitrate: " << format_bitrate(bitrate * streams));
  LOGGER_INFO(logger, "| * Expected Objects/s: " << expected_objects);
  LOGGER_INFO(logger, "| *    Total Objects/s: " << (expected_objects * streams));
  LOGGER_INFO(logger, "+==========================================+");
  // clang-format on

  std::atomic_size_t finished_publishers = 0;
  std::vector<std::uint8_t> buffer(msg_size, 0);
  std::vector<std::thread> threads;
  std::size_t total_objects_published = 0;

  const auto start = std::chrono::high_resolution_clock::now();
  for (quicr::Namespace& pub_ns : namespaces) {
    threads.emplace_back([&] {
      pthread_setname_np(std::string(pub_ns).c_str());

      quicr::Name name = pub_ns;

      ::loop_for(duration, interval, [&] {
        std::vector<std::uint8_t> msg_bytes = buffer;

        std::lock_guard _(mutex);
        client.publishNamedObject(name,
                                  priority,
                                  expiry_age,
                                  std::move(msg_bytes),
                                  { {
                                    "perf:publish",
                                    now(),
                                  } });
        ++total_objects_published;

        name = pub_ns.name() |
               (~(~0x0_name << (128 - pub_ns.length())) & (name + 1));
      });

      ++finished_publishers;
      cv.notify_one();
    });
  }

  cv.wait(lock,
          [&] { return terminate.load() || finished_publishers == streams; });

  const auto end = std::chrono::high_resolution_clock::now();
  const auto elapsed =
    std::chrono::duration_cast<std::chrono::seconds>(end - start);

  // clang-format off
  LOGGER_INFO(logger, "+==========================================+");
  if (terminate) {
    LOGGER_INFO(logger, "| Received interrupt, exiting early");
  } else {
    LOGGER_INFO(logger, "| Test complete");
  }
  LOGGER_INFO(logger, "+-------------------------------------------");
  LOGGER_INFO(logger, "| *          Duration: " << elapsed.count() << " seconds");
  LOGGER_INFO(logger, "| * Published Objects: " << total_objects_published);
  LOGGER_INFO(logger, "+==========================================+");
  // clang-format on

  for (auto& thread : threads) {
    thread.join();
  }

  return EXIT_SUCCESS;
}
