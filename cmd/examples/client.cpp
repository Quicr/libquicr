
#include <iostream>
#include <chrono>
#include <iomanip>
#include <oss/cxxopts.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <moq/client.h>

#include "signal_handler.h"

namespace qclient_vars {
  //std::optional<qtransport::TransportConnId> conn_id;

    bool publish_clock {false};
}

std::string get_time_str()
{
    std::ostringstream oss;

    auto now = std::chrono::system_clock::now();
    auto now_us = std::chrono::time_point_cast<std::chrono::microseconds>(now);
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    struct tm tm_result{};
    localtime_r(&t, &tm_result);
    oss << std::put_time(&tm_result, "%F %T")
        << "."
        << std::setfill('0')
        << std::setw(6)
        << (now_us.time_since_epoch().count()) % 1'000'000;

    return oss.str();
}

/* -------------------------------------------------------------------------------------------------
 * Track delegate is used for either publish or subscribe. All handling for the track is via the
 *      delegate.
 * -------------------------------------------------------------------------------------------------
 */
class MySubscribeTrackHandler : public moq::SubscribeTrackHandler
{
public:
  MySubscribeTrackHandler(const moq::FullTrackName& full_track_name)
    : SubscribeTrackHandler( full_track_name )
  {
  }
  
  virtual ~MySubscribeTrackHandler() = default;
  
  virtual void ObjectReceived(const moq::ObjectHeaders& object_headers, Span<uint8_t> data) {
    std::string msg(data.begin(), data.end());
    SPDLOG_INFO( "Received message: {0}", msg);
  }

  virtual void StatusChanged(Status status) {
    switch ( status ) {
    case Status::kOk : {
      if (   auto track_alias = GetTrackAlias();  track_alias.has_value() ) {
        SPDLOG_INFO( "Track alias: {0} is ready to read",  track_alias.value() );
      }
    }
      break;
    default:
      break; 
    }
  }

#if 0
    void cb_sendCongested(bool cleared, uint64_t objects_in_queue) override {}

    void cb_sendReady() override {
        SPDLOG_LOGGER_INFO(_logger, "Track alias: {0} is ready to send", _track_alias.value());
    }

    void cb_sendNotReady(TrackSendStatus status) override {
        SPDLOG_LOGGER_INFO(_logger, "Track alias: {0} is NOT ready to send status: {1}", _track_alias.value(), static_cast<int>(status));
    }

    void cb_readReady() override
    {
        SPDLOG_LOGGER_INFO(_logger, "Track alias: {0} is ready to read",  _track_alias.value());
    }
    void cb_readNotReady(TrackReadStatus status) override {}
#endif
};

#if 0 
/* -------------------------------------------------------------------------------------------------
 * Client MOQ instance delegate is used to control and interact with the connection.
 * -------------------------------------------------------------------------------------------------
 */
class clientDelegate : public quicr::MoQInstanceDelegate
{
  public:
    clientDelegate(std::shared_ptr<spdlog::logger> logger) : _logger(std::move(logger)) {}

    virtual ~clientDelegate() = default;

    void cb_newConnection(qtransport::TransportConnId conn_id,
                          Span<uint8_t const> endpoint_id,
                          const qtransport::TransportRemote& remote) override {}

    void cb_connectionStatus(qtransport::TransportConnId conn_id,
                             Span<uint8_t const> endpoint_id,
                             qtransport::TransportStatus status) override
    {
        auto ep_id = std::string(endpoint_id.begin(), endpoint_id.end());

        if (status == qtransport::TransportStatus::Ready) {
            SPDLOG_LOGGER_INFO(_logger, "Connection ready conn_id: {0} endpoint_id: {1}", conn_id, ep_id);

            qclient_vars::conn_id = conn_id;
        }
    }
    void cb_clientSetup(qtransport::TransportConnId conn_id, quicr::messages::MoqClientSetup client_setup) override {}
    void cb_serverSetup(qtransport::TransportConnId conn_id, quicr::messages::MoqServerSetup server_setup) override {}

  private:
    std::shared_ptr<spdlog::logger> _logger;
};

/* -------------------------------------------------------------------------------------------------
 * Publisher Thread to perform publishing
 * -------------------------------------------------------------------------------------------------
 */
void do_publisher(const std::string t_namespace,
             const std::string t_name,
             const std::shared_ptr<quicr::MoQInstance>& moqInstance,
             const bool& stop)
{
    auto _logger = spdlog::stderr_color_mt("PUB");

    auto mi = moqInstance;

    auto track_delegate = std::make_shared<trackDelegate>(t_namespace, t_name, 2, 3000, _logger);

    SPDLOG_LOGGER_INFO(_logger, "Started publisher track: {0}/{1}",  t_namespace, t_name);

    bool published_track { false };
    bool sending { false };
    uint64_t group_id { 0 };
    uint64_t object_id { 0 };

    while (not stop) {
        if (!published_track && qclient_vars::conn_id) {
            SPDLOG_LOGGER_INFO(_logger, "Publish track: {0}/{1}", t_namespace, t_name);
            mi->publishTrack(*qclient_vars::conn_id, track_delegate);
            published_track = true;
        }

        if (track_delegate->getSendStatus() != quicr::MoQTrackDelegate::TrackSendStatus::OK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (!sending) {
            SPDLOG_LOGGER_INFO(_logger, "--------------------------------------------------------------------------");

            if (qclient_vars::publish_clock) {
                SPDLOG_LOGGER_INFO(_logger, " Publishing clock timestamp every second");
            } else {
                SPDLOG_LOGGER_INFO(_logger, " Type message and press enter to send");
            }

            SPDLOG_LOGGER_INFO(_logger, "--------------------------------------------------------------------------");
            sending = true;
        }

        std::string msg;
        if (qclient_vars::publish_clock) {
            std::this_thread::sleep_for(std::chrono::milliseconds(999));
            msg = get_time_str();
            SPDLOG_LOGGER_INFO(_logger, msg);
        } else { // stdin
            getline(std::cin, msg);
            SPDLOG_LOGGER_INFO(_logger, "Send message: {0}", msg);
        }

        if (object_id % 5 == 0) {       // Set new group
            object_id = 0;
            group_id++;
        }

        track_delegate->sendObject(
          group_id, object_id++, { reinterpret_cast<uint8_t*>(msg.data()), msg.size() });
    }

    mi->unpublishTrack(*qclient_vars::conn_id, track_delegate);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SPDLOG_LOGGER_INFO(_logger, "Publisher done track: {0}/{1}", t_namespace, t_name);
}

/* -------------------------------------------------------------------------------------------------
 * Subscriber thread to perform subscribe
 * -------------------------------------------------------------------------------------------------
 */
void do_subscriber(const std::string t_namespace,
              const std::string t_name,
              const std::shared_ptr<quicr::MoQInstance>& moqInstance,
              const bool& stop)
{
    auto _logger = spdlog::stderr_color_mt("SUB");

    auto mi = moqInstance;

    auto track_delegate = std::make_shared<trackDelegate>(t_namespace, t_name, 2, 3000, _logger);

    SPDLOG_LOGGER_INFO(_logger, "Started subscriber track: {0}/{1}", t_namespace, t_name);

    bool subscribe_track { false };
    while (not stop) {
        if (!subscribe_track && qclient_vars::conn_id) {
            SPDLOG_LOGGER_INFO(_logger, "Subscribe track: {0}/{1}", t_namespace, t_name);
            mi->subscribeTrack(*qclient_vars::conn_id, track_delegate);
            subscribe_track = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    mi->unsubscribeTrack(*qclient_vars::conn_id, track_delegate);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SPDLOG_LOGGER_INFO(_logger, "Subscriber done track: {0}/{1}", t_namespace, t_name);
}

/* -------------------------------------------------------------------------------------------------
 * Main program
 * -------------------------------------------------------------------------------------------------
 */
moq::ClientConfig init_config(cxxopts::ParseResult& cli_opts,
                 bool& enable_pub,
                 bool& enable_sub)
{
    moq::ClientConfig config;

    std::string qlog_path;
    if (cli_opts.count("qlog")) {
        qlog_path = cli_opts["qlog"].as<std::string>();
    }

    if (cli_opts.count("debug") && cli_opts["debug"].as<bool>() == true) {
        SPDLOG_LOGGER_INFO(logger, "setting debug level");
        logger->set_level(spdlog::level::debug);
    }

    if (cli_opts.count("pub_namespace") && cli_opts.count("pub_name")) {
        enable_pub = true;
        SPDLOG_LOGGER_INFO(logger, "Publisher enabled using track namespace: {0} name: {1}", cli_opts["pub_namespace"].as<std::string>(), cli_opts["pub_name"].as<std::string>());
    }

    if (cli_opts.count("clock") && cli_opts["clock"].as<bool>() == true) {
        SPDLOG_LOGGER_INFO(logger, "Running in clock publish mode");
        qclient_vars::publish_clock = true;
    }

    if (cli_opts.count("sub_namespace") && cli_opts.count("sub_name")) {
        enable_sub = true;
        SPDLOG_LOGGER_INFO(logger, "Subscriber enabled using track namespace: {0} name: {1}", cli_opts["sub_namespace"].as<std::string>(), cli_opts["sub_name"].as<std::string>());
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();
    config.server_host_ip = cli_opts["host"].as<std::string>();
    config.server_port = cli_opts["port"].as<uint16_t>();
    config.server_proto = qtransport::TransportProtocol::QUIC;
    config.transport_config.debug = cli_opts["debug"].as<bool>();;
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.tls_cert_filename = "";
    config.transport_config.tls_key_filename = "";
    config.transport_config.quic_qlog_path = qlog_path;

    return config;
}

int
main(int argc, char* argv[])
{
    int result_code = EXIT_SUCCESS;

    auto logger = spdlog::stderr_color_mt("qclient");

    cxxopts::Options options("qclient", "MOQ Example Client");
    options
      .set_width(75)
      .set_tab_expansion()
      //.allow_unrecognised_options()
      .add_options()
        ("h,help", "Print help")
          ("d,debug", "Enable debugging") // a bool parameter
      ("r,host", "Relay host/IP", cxxopts::value<std::string>()->default_value("localhost"))
        ("p,port", "Relay port", cxxopts::value<uint16_t>()->default_value("1234"))
          ("e,endpoint_id", "This client endpoint ID", cxxopts::value<std::string>()->default_value("moq-client"))
            ("q,qlog", "Enable qlog using path", cxxopts::value<std::string>())
      ; // end of options

    options.add_options("Publisher")
      ("pub_namespace", "Track namespace", cxxopts::value<std::string>())
        ("pub_name", "Track name", cxxopts::value<std::string>())
          ("clock", "Publish clock timestamp every second instead of using STDIN chat")
      ;

    options.add_options("Subscriber")
      ("sub_namespace", "Track namespace", cxxopts::value<std::string>())
        ("sub_name", "Track name", cxxopts::value<std::string>())
      ;

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help({"", "Publisher", "Subscriber"}) << std::endl;
        return true;
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock lock(moq_example::main_mutex);

    bool enable_pub { false };
    bool enable_sub { false };
    quicr::MoQInstanceClientConfig config = init_config(result, enable_pub, enable_sub, logger);

    auto delegate = std::make_shared<clientDelegate>(logger);

    try {
        //auto moqInstance = quicr::MoQInstance{config, delegate, logger};
        auto moqInstance = std::make_shared<quicr::MoQInstance>(config, delegate, logger);

        moqInstance->run_client();

        bool stop_threads { false };
        std::thread pub_thread, sub_thread;
        if (enable_pub) {
            pub_thread = std::thread (do_publisher,
                                     result["pub_namespace"].as<std::string>(),
                                     result["pub_name"].as<std::string>(),
                                     moqInstance, std::ref(stop_threads));
        }

        if (enable_sub) {
            sub_thread = std::thread (do_subscriber,
                                     result["sub_namespace"].as<std::string>(),
                                     result["sub_name"].as<std::string>(),
                                     moqInstance, std::ref(stop_threads));
        }

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

        stop_threads = true;
        SPDLOG_LOGGER_INFO(logger, "Stopping threads...");

        if (pub_thread.joinable()) {
            pub_thread.join();
        }

        if (sub_thread.joinable()) {
            sub_thread.join();
        }

        moqInstance->stop();

        SPDLOG_LOGGER_INFO(logger, "Client done");
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));

        // Unlock the mutex
        lock.unlock();
    } catch (const std::invalid_argument& e) {
        std::cerr << "Invalid argument: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected exception: " << e.what() << std::endl;
        result_code = EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Unexpected exception" << std::endl;
        result_code = EXIT_FAILURE;
    }

    SPDLOG_LOGGER_INFO(logger, "Exit");

    return result_code;
}

#endif
