
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
 * Track delegate is used for subscribe. All handling for the track is via the
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
};

/* -------------------------------------------------------------------------------------------------
 * Track delegate is used for either publish. All handling for the track is via the
 *      delegate.
 * -------------------------------------------------------------------------------------------------
 */  
class MyPublishTrackHandler : public moq::PublishTrackHandler
{
public:
  MyPublishTrackHandler(const moq::FullTrackName& full_track_name,
                        moq::TrackMode track_mode,
                        uint8_t default_priority,
                        uint32_t default_ttl )
    : moq::PublishTrackHandler( full_track_name,
                                track_mode,
                                default_priority,
                                default_ttl)
  {
  }
  
  virtual ~MyPublishTrackHandler() = default;

  virtual void StatusChanged(Status status) {
    switch ( status ) {
    case Status::kOK : {
      if (   auto track_alias = GetTrackAlias();  track_alias.has_value() ) {
        SPDLOG_INFO( "Track alias: {0} is ready to read",  track_alias.value() );
      }
    }
      break;
    default:
      break; 
    }
  }
};

/* -------------------------------------------------------------------------------------------------
 * Client MOQ instance delegate is used to control and interact with the connection.
 * -------------------------------------------------------------------------------------------------
 */
class MyClient : public moq::Client
{
public:
  MyClient(const moq::ClientConfig& cfg): moq::Client(cfg) {}
  
  virtual ~MyClient() = default;
  
  virtual void StatusChanged(Status status) {
    if ( status == Status::kReady ) {
      SPDLOG_INFO( "Connection ready" );
    }
  }
  
};


/* -------------------------------------------------------------------------------------------------
 * Publisher Thread to perform publishing
 * -------------------------------------------------------------------------------------------------
 */

void do_publisher(const moq::FullTrackName& full_track_name,
                  const std::shared_ptr<moq::Client>& moqInstance,
                  const bool& stop)
{
#if 0 
    auto mi = moqInstance;

    auto track_delegate = std::make_shared<trackDelegate>(full_track_name);

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
#endif
}


/* -------------------------------------------------------------------------------------------------
 * Subscriber thread to perform subscribe
 * -------------------------------------------------------------------------------------------------
 */
void do_subscriber(const moq::FullTrackName& full_track_name,
              const std::shared_ptr<moq::Client>& moqInstance,
              const bool& stop)
{
  //auto mi = moqInstance;

    auto track_delegate = std::make_shared<MySubscribeTrackHandler>( full_track_name );

    SPDLOG_INFO( "Started subscriber");

    bool subscribe_track { false };
    

    while (not stop) {
      if ( (!subscribe_track) && ( moqInstance->GetStatus() == MyClient::Status::kReady) ) {
            SPDLOG_INFO("Subscribing to track" );
            moqInstance->SubscribeTrack( track_delegate );
            subscribe_track = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    moqInstance->UnsubscribeTrack( track_delegate);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SPDLOG_INFO( "Subscriber done track");

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
        SPDLOG_INFO( "setting debug level");
        spdlog::set_level(spdlog::level::debug);
    }

    if (cli_opts.count("pub_namespace") && cli_opts.count("pub_name")) {
        enable_pub = true;
        SPDLOG_INFO( "Publisher enabled using track namespace: {0} name: {1}",
                     cli_opts["pub_namespace"].as<std::string>(), cli_opts["pub_name"].as<std::string>());
    }

    if (cli_opts.count("clock") && cli_opts["clock"].as<bool>() == true) {
        SPDLOG_INFO( "Running in clock publish mode");
        qclient_vars::publish_clock = true;
    }

    if (cli_opts.count("sub_namespace") && cli_opts.count("sub_name")) {
        enable_sub = true;
        SPDLOG_INFO( "Subscriber enabled using track namespace: {0} name: {1}",
                     cli_opts["sub_namespace"].as<std::string>(), cli_opts["sub_name"].as<std::string>());
    }

    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();
    config.connect_uri = cli_opts["url"].as<std::string>();
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

    //auto logger = spdlog::stderr_color_mt("qclient");

    cxxopts::Options options("qclient", "MOQ Example Client");
    options
      .set_width(75)
      .set_tab_expansion()
      //.allow_unrecognised_options()
      .add_options()
        ("h,help", "Print help")
          ("d,debug", "Enable debugging") // a bool parameter
      ("r,uri", "Relay URL", cxxopts::value<std::string>()->default_value("moqt::/localhost:1234"))
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
    moq::ClientConfig config = init_config(result, enable_pub, enable_sub );

    auto pub_name_space = result["pub_namespace"].as<std::string>();
    auto pub_name = result["pub_name"].as<std::string>();
    std::vector<uint8_t> pub_name_space_vec(pub_name_space.begin(), pub_name_space.end());
    std::vector<uint8_t> pub_name_vec(pub_name.begin(), pub_name.end());
    moq::FullTrackName full_pub_track_name { pub_name_space_vec , pub_name_vec, 42 /* track_alias */  };

    auto sub_name_space = result["sub_namespace"].as<std::string>();
    auto sub_name = result["sub_name"].as<std::string>();
    std::vector<uint8_t> sub_name_space_vec(sub_name_space.begin(), sub_name_space.end());
    std::vector<uint8_t> sub_name_vec(sub_name.begin(), sub_name.end());
    moq::FullTrackName full_sub_track_name { sub_name_space_vec , sub_name_vec, 44 /* track_alias */  };

    try {
        auto moqInstance = std::make_shared<MyClient>(config);

        moqInstance->Connect();

        bool stop_threads { false };
        std::thread pub_thread, sub_thread;
        
        if (enable_pub) {    
          pub_thread = std::thread (do_publisher, full_pub_track_name, 
                                     moqInstance, std::ref(stop_threads));
        }

        if (enable_sub) {
            sub_thread = std::thread (do_subscriber,
                                     full_pub_track_name,
                                      moqInstance, std::ref(stop_threads));
        }

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

        stop_threads = true;
        SPDLOG_INFO( "Stopping threads...");

        if (pub_thread.joinable()) {
            pub_thread.join();
        }

        if (sub_thread.joinable()) {
            sub_thread.join();
        }

        moqInstance->Disconnect();

        SPDLOG_INFO( "Client done");
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

    SPDLOG_INFO( "Exit");

    return result_code;
}

