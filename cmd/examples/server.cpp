
#include <unordered_map>
#include <set>
#include <condition_variable>
#include <oss/cxxopts.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <moq/server.h>

#include "signal_handler.h"


namespace qserver_vars {
    std::mutex _state_mutex;

    /*
     * Map of subscribes (e.g., track alias) sent to announcements
     *      track_alias_set = announce_active[track_namespace][conn_id]
     */
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::set<uint64_t>>> announce_active;

    /*
     * Active subscribes for a given track, indexed (keyed) by track_alias,conn_id
     *     NOTE: This indexing intentionally prohibits per connection having more
     *           than one subscribe to a full track name.
     *
     *     Example: track_delegate = subscribes[track_alias][conn_id]
     */
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::shared_ptr<moq::ServerPublishTrackHandler>>> subscribes;

    /*
     * Subscribe ID to alias mapping
     *      Used to lookup the track alias for a given subscribe ID
     *
     *      track_alias = subscribe_alias_sub_id[conn_id][subscribe_id]
     */
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, uint64_t>> subscribe_alias_sub_id;

    /*
     * Map of subscribes set by namespace and track name hash
     *      Set<subscribe_who> = subscribe_active[track_namespace_hash][track_name_hash]
     */
    struct SubscribeWho {
        uint64_t conn_id;
        uint64_t subscribe_id;
        uint64_t track_alias;

        bool operator<(const SubscribeWho& other) const {
            return conn_id < other.conn_id && subscribe_id << other.subscribe_id;
        }
        bool operator==(const SubscribeWho& other) const {
            return conn_id == other.conn_id && subscribe_id == other.subscribe_id;
        }
        bool operator>(const SubscribeWho& other) const {
            return conn_id > other.conn_id && subscribe_id > other.subscribe_id;
        }
    };
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::set<SubscribeWho>>> subscribe_active;

    /*
     * Active publisher/announce subscribes that this relay has made to receive objects from publisher.
     *
     * track_delegate = pub_subscribes[track_alias][conn_id]
     */
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, std::shared_ptr<moq::ServerPublishTrackHandler>>> pub_subscribes;
}

/* -------------------------------------------------------------------------------------------------
 * Subscribe Track Delegate - Relay runs on subscribes and uses subscribe delegate to send data
 *      to subscriber
 * -------------------------------------------------------------------------------------------------
 */
class MySubscribeTrackHandler : public moq::SubscribeTrackHandler
{
  public:
  MySubscribeTrackHandler(const moq::FullTrackName& full_track_name)
      : SubscribeTrackHandler( full_track_name)
    {
    }

    virtual ~MySubscribeTrackHandler() = default;

    // TODO: Add prioirty and TTL
    virtual void ObjectReceived(const moq::ObjectHeaders& headers, Span<uint8_t> data) {
        std::lock_guard<std::mutex> _(qserver_vars::_state_mutex);

        auto track_alias = GetTrackAlias();
        if ( !track_alias.has_value() ) {
            SPDLOG_DEBUG( "Data without valid track alias");
            return;
        }
        
        auto sub_it = qserver_vars::subscribes.find( track_alias.value());

        if (sub_it == qserver_vars::subscribes.end()) {
            SPDLOG_DEBUG( "No subscribes, not relaying data size: {0} ", object.size());
            return;
        }

        for (const auto& [conn_id, td]: sub_it->second) {
            SPDLOG_DEBUG( "Relaying track_alias:  data size: {0}", object.size());

            td->PublishObject(headers,data);
        }
    }


   virtual void StatusChanged(Status status) {
     if ( status == Status::kOk ) {
       SPDLOG_INFO( "Track alias: is subscribed" );
     }
     else {
       std::string reason = "";
       switch (status) {
       case Status::kNotConnected:
         reason = "not connected";
         break;
       case Status::kSubscribeError:
         reason = "subscribe error";
         break;
       case Status::kNotAuthorized:
         reason = "not authorized";
         break;
       case Status::kNotSubscribed:
         reason = "not subscribed";
         break;
       case Status::kPendingSubscribeResponse:
         reason = "pending subscribe response";
         break;
       case Status::kSendingUnsubscribe:
         reason = "unsubscribing";
         break;
       default:
         break;
       }
       
       SPDLOG_INFO( "Track is NOT ready, status: {0}",  reason);
     }
   }
  
};


/* -------------------------------------------------------------------------------------------------
 * Server delegate is the instance delegate for connection handling
 * -------------------------------------------------------------------------------------------------
 */
class MyServer : public moq::Server
{
  public:
    MyServer(const moq::ServerConfig& cfg) : moq::Server( cfg ){}

    virtual ~MyServer() = default;
   
    virtual void NewConnectionAccepted(moq::ConnectionHandle connection_handle,
                                       const moq::Server::ConnectionRemoteInfo& remote) { }
   
  virtual void UnannounceReceived(moq::ConnectionHandle connection_handle,
                                  const moq::TrackNamespace& track_namespace){
#if 0
     //void cb_unannounce(qtransport::TransportConnId conn_id,
     //                  uint64_t track_namespace_hash,
     //                  std::optional<uint64_t> track_name_hash) override {

        if (track_name_hash.has_value()) { // subscribe done received
          SPDLOG_INFO( "Received subscribe done from conn_id: {0} for namespace hash: {1} name hash: {2}, "
                       "informational update only",
                       conn_id,
                       track_namespace_hash,
                       *track_name_hash);
        } else {
          SPDLOG_DEBUG( "Received unannounce from conn_id: {0}  for namespace hash: {1}, removing all tracks "
                        "associated with namespace",
                        conn_id,
                        track_namespace_hash);
          
            for (auto track_alias: qserver_vars::announce_active[track_namespace_hash][conn_id]) {
                auto ptd = qserver_vars::pub_subscribes[track_alias][conn_id];
                if (ptd != nullptr) {
                    SPDLOG_INFO( 
                      "Received unannounce from conn_id: {0} for namespace hash: {1}, removing track alias: {2}",
                      conn_id,
                      track_namespace_hash,
                      track_alias);

                    _moq_instance.lock()->unsubscribeTrack(conn_id, ptd);
                }
                qserver_vars::pub_subscribes[track_alias].erase(conn_id);
                if (qserver_vars::pub_subscribes[track_alias].empty()) {
                    qserver_vars::pub_subscribes.erase(track_alias);
                }
            }

            qserver_vars::announce_active[track_namespace_hash].erase(conn_id);
            if (qserver_vars::announce_active[track_namespace_hash].empty()) {
                qserver_vars::announce_active.erase(track_namespace_hash);
            }
        }
#endif
    }

#if 0
  virtual AnnounceResponse AnnounceReceived(moq::ConnectionHandle connection_handle,
                                            const moq::TrackNamespace& track_namespace,
                                            const moq::PublishAnnounceAttributes& publish_announce_attributes) {
    
    // TODO - sort out hash hash of namespace works ?
    uint64_t track_namespace_hash;
    
        SPDLOG_DEBUG( "Received announce from conn_id: {0} for namespace: {1}", conn_id, track_namespace );

        // Add to state if not exist
        auto [anno_conn_it, is_new] = qserver_vars::announce_active[track_namespace_hash].try_emplace(conn_id);

        if (!is_new) {
            SPDLOG_INFO( "Received announce from conn_id: {0} for namespace_hash: {0} is duplicate, ignoring",
                         conn_id, track_namespace_hash);
            return true;
        }

     
        AnnounceResponse ret;
        // TODO fill in and call correct returns
        

        auto& anno_tracks = qserver_vars::announce_active[track_namespace_hash][conn_id];

        // Check if there are any subscribes. If so, send subscribe to announce for all tracks matching namespace
        const auto sub_active_it = qserver_vars::subscribe_active.find(track_namespace_hash);
        if (sub_active_it != qserver_vars::subscribe_active.end()) {
            for (const auto& [track_name, who]: sub_active_it->second) {
                if (who.size()) { // Have subscribes
                    auto& a_who = *who.begin();
                    if (anno_tracks.find(a_who.track_alias) == anno_tracks.end()) {
                        SPDLOG_INFO( "Sending subscribe to announcer conn_id: {0} subscribe track_alias: {1}",
                                     conn_id, a_who.track_alias);

                        anno_tracks.insert(a_who.track_alias); // Add track to state

                        const auto sub_track_delegate = qserver_vars::subscribes[a_who.track_alias][a_who.conn_id];
                        std::string t_namespace {sub_track_delegate->getTrackNamespace().begin(),
                                                 sub_track_delegate->getTrackNamespace().end()};
                        std::string t_name { sub_track_delegate->getTrackName().begin(),
                                             sub_track_delegate->getTrackName().end()};

                        auto pub_track_delegate =
                          std::make_shared<subTrackDelegate>(t_namespace,
                                                             t_name,
                                                             2,
                                                             3000);

                        _moq_instance.lock()->subscribeTrack(conn_id, pub_track_delegate);
                        qserver_vars::pub_subscribes[a_who.track_alias][conn_id] = pub_track_delegate;
                    }
                }
            }
        }
    }
#endif


  virtual void ConnectionStatusChanged( moq::ConnectionHandle connection_handle, ConnectionStatus status){
      if (status == qtransport::TransportStatus::kReady) {
            SPDLOG_DEBUG( "Connection ready conn_id: {0} ", connection_handle );
      }
  }
  

  // TODO - move new API 
  virtual ClientSetupResponse ClientSetupReceived( moq::ConnectionHandle connection_handle,
                                                   const moq::ClientSetupAttributes& client_setup_attributes){
     // TODO form ClientSetupResponse and send 
   }

  

  virtual void UnsubscribeReceived(moq::ConnectionHandle connection_handle,
                                    uint64_t subscribe_id) {
#if 0
     //void cb_unsubscribe(qtransport::TransportConnId conn_id,
     //                   uint64_t subscribe_id) override {
        SPDLOG_INFO( "Unsubscribe conn_id: {0} subscribe_id: {1}", conn_id, subscribe_id);

        auto ta_conn_it = qserver_vars::subscribe_alias_sub_id.find(conn_id);
        if (ta_conn_it == qserver_vars::subscribe_alias_sub_id.end()) {
            SPDLOG_WARN( "Unable to find track alias connection for conn_id: {0} subscribe_id: {1}", conn_id, subscribe_id);
            return;
        }

        auto ta_it = ta_conn_it->second.find(subscribe_id);
        if (ta_it == ta_conn_it->second.end()) {
            SPDLOG_WARN( "Unable to find track alias for conn_id: {0} subscribe_id: {1}", conn_id, subscribe_id);
            return;
        }

        std::lock_guard<std::mutex> _(qserver_vars::_state_mutex);

        auto track_alias = ta_it->second;

        ta_conn_it->second.erase(ta_it);
        if (!ta_conn_it->second.size()) {
            qserver_vars::subscribe_alias_sub_id.erase(ta_conn_it);
        }


        auto& track_delegate = qserver_vars::subscribes[track_alias][conn_id];

        if (track_delegate == nullptr) {
            SPDLOG_WARN( "Unsubscribe unable to find track delegate for conn_id: {0} subscribe_id: {1}", conn_id, subscribe_id);
            return;
        }

        auto tfn = quicr::MoQInstance::TrackFullName{ track_delegate->getTrackNamespace(), track_delegate->getTrackName() };
        auto th = quicr::MoQInstance::TrackHash(tfn);

        qserver_vars::subscribes[track_alias].erase(conn_id);
        bool unsub_pub { false };
        if (!qserver_vars::subscribes[track_alias].size()) {
            unsub_pub = true;
            qserver_vars::subscribes.erase(track_alias);
        }

        qserver_vars::subscribe_active[th.track_namespace_hash][th.track_name_hash].erase(qserver_vars::SubscribeWho{
          .conn_id = conn_id, .subscribe_id = subscribe_id, .track_alias = th.track_fullname_hash });

        if (!qserver_vars::subscribe_active[th.track_namespace_hash][th.track_name_hash].size()) {
            qserver_vars::subscribe_active[th.track_namespace_hash].erase(th.track_name_hash);
        }

        if (!qserver_vars::subscribe_active[th.track_namespace_hash].size()) {
            qserver_vars::subscribe_active.erase(th.track_namespace_hash);
        }

        if (unsub_pub) {
            SPDLOG_INFO( "No subscribers left, unsubscribe publisher track_alias: {0}", track_alias);

            auto anno_ns_it = qserver_vars::announce_active.find(th.track_namespace_hash);
            if (anno_ns_it == qserver_vars::announce_active.end()) {
                return;
            }

            for (auto& [conn_id, tracks]: anno_ns_it->second) {
                if (tracks.find(th.track_fullname_hash) == tracks.end()) {
                    SPDLOG_INFO( "Unsubscribe to announcer conn_id: {0} subscribe track_alias: {1}",
                                 conn_id, th.track_fullname_hash);

                    tracks.erase(th.track_fullname_hash); // Add track alias to state

                    auto pub_delegate = qserver_vars::pub_subscribes[th.track_fullname_hash][conn_id];
                    if (pub_delegate != nullptr) {
                        _moq_instance.lock()->unsubscribeTrack(conn_id, pub_delegate);
                    }
                }
            }
        }
#endif
    }

  virtual SubscribeResponse SubscribeReceived(moq::ConnectionHandle connection_handle,
                                                uint64_t subscribe_id,
                                                uint64_t proposed_track_alias,
                                                const moq::FullTrackName& track_full_name,
                                                const moq::SubscribeAttributes& subscribe_attributes){
      // bool cb_subscribe(qtransport::TransportConnId conn_id,
      //                uint64_t subscribe_id,
      //                Span<uint8_t const> name_space,
      //                Span<uint8_t const> name) override
#if 1
        std::string const t_namespace(name_space.begin(), name_space.end());
        std::string const t_name(name.begin(), name.end());

        SPDLOG_INFO( "New subscribe conn_id: {0} subscribe_id: {1} track: {2}/{3}",
                     conn_id,
                     subscribe_id,
                     t_namespace,
                     t_name);
        
        auto track_delegate = std::make_shared<subTrackDelegate>(t_namespace, t_name, 2, 3000);
        auto tfn = quicr::MoQInstance::TrackFullName{ name_space, name };
        auto th = quicr::MoQInstance::TrackHash(tfn);
        qserver_vars::subscribes[th.track_fullname_hash][conn_id] = track_delegate;
        qserver_vars::subscribe_alias_sub_id[conn_id][subscribe_id] = th.track_fullname_hash;

        // record subscribe as active from this subscriber
        qserver_vars::subscribe_active[th.track_namespace_hash][th.track_name_hash].emplace(
          qserver_vars::SubscribeWho{ .conn_id = conn_id,
                                      .subscribe_id = subscribe_id,
                                      .track_alias = th.track_fullname_hash});

        // Create a subscribe track that will be used by the relay to send to subscriber for matching objects
        _moq_instance.lock()->bindSubscribeTrack(conn_id, subscribe_id, track_delegate);

        // Subscribe to announcer if announcer is active
        auto anno_ns_it = qserver_vars::announce_active.find(th.track_namespace_hash);
        if (anno_ns_it == qserver_vars::announce_active.end()) {
            SPDLOG_INFO( "Subscribe to track namespace: {0}, does not have any announcements.", t_namespace);
            return true;
        }

        for (auto& [conn_id, tracks]: anno_ns_it->second) {
            if (tracks.find(th.track_fullname_hash) == tracks.end()) {
                SPDLOG_INFO( "Sending subscribe to announcer conn_id: {0} subscribe track_alias: {1}",
                             conn_id, th.track_fullname_hash);

                tracks.insert(th.track_fullname_hash); // Add track alias to state

                auto pub_track_delegate = std::make_shared<subTrackDelegate>(t_namespace, t_name, 2, 3000 );
                _moq_instance.lock()->subscribeTrack(conn_id, pub_track_delegate);
                qserver_vars::pub_subscribes[th.track_fullname_hash][conn_id] = pub_track_delegate;
            }
        }
#endif
    }


};

/* -------------------------------------------------------------------------------------------------
 * Main program
 * -------------------------------------------------------------------------------------------------
 */
moq::ServerConfig init_config(cxxopts::ParseResult& cli_opts )
{
    moq::ServerConfig config;

    std::string qlog_path;
    if (cli_opts.count("qlog")) {
        qlog_path = cli_opts["qlog"].as<std::string>();
    }

#if 0
    if (cli_opts.count("debug") && cli_opts["debug"].as<bool>() == true) {
        SPDLOG_INFO(logger, "setting debug level");
        logger->set_level(spdlog::level::debug);
    }
#endif
    
    config.endpoint_id = cli_opts["endpoint_id"].as<std::string>();

    config.server_bind_ip = cli_opts["bind_ip"].as<std::string>();
    config.server_port = cli_opts["port"].as<uint16_t>();
    //config.server_proto = qtransport::TransportProtocol::QUIC;

    config.transport_config.debug = cli_opts["debug"].as<bool>();
    config.transport_config.tls_cert_filename = cli_opts["cert"].as<std::string>();
    config.transport_config.tls_key_filename = cli_opts["key"].as<std::string>();
    config.transport_config.use_reset_wait_strategy = false;
    config.transport_config.time_queue_max_duration = 5000;
    config.transport_config.quic_qlog_path = qlog_path;

    return config;
}


int
main(int argc, char* argv[])
{
    int result_code = EXIT_SUCCESS;

    cxxopts::Options options("qclient", "MOQ Example Client");
    options
      .set_width(75)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()
        ("h,help", "Print help")
          ("d,debug", "Enable debugging") // a bool parameter
      ("b,bind_ip", "Bind IP", cxxopts::value<std::string>()->default_value("127.0.0.1"))
        ("p,port", "Listening port", cxxopts::value<uint16_t>()->default_value("1234"))
          ("e,endpoint_id", "This relay/server endpoint ID", cxxopts::value<std::string>()->default_value("moq-server"))
            ("c,cert", "Certificate file", cxxopts::value<std::string>()->default_value("./server-cert.pem"))
              ("k,key", "Certificate key file", cxxopts::value<std::string>()->default_value("./server-key.pem"))
                ("q,qlog", "Enable qlog using path", cxxopts::value<std::string>())
      ; // end of options

    auto result = options.parse(argc, argv);

    if (result.count("help"))
    {
        std::cout << options.help({""}) << std::endl;
        return EXIT_SUCCESS;
    }

    // Install a signal handlers to catch operating system signals
    installSignalHandlers();

    // Lock the mutex so that main can then wait on it
    std::unique_lock<std::mutex> lock(moq_example::main_mutex);

    moq::ServerConfig config = init_config(result);

    auto delegate = std::make_shared<MyServer>();

    try {
        auto moqInstance = std::make_shared<MyServer>(config);
        moqInstance->Start();

        // Wait until told to terminate
        moq_example::cv.wait(lock, [&]() { return moq_example::terminate; });

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

    return result_code;
}
