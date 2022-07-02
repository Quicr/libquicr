#include <cassert>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string.h> // memcpy
#include <thread>

#if defined(__linux) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netdb.h>
#endif
#if defined(__linux__)
#include <net/ethernet.h>
#include <netpacket/packet.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <net/if_dl.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "quicr_transport.h"

#include "autoqlog.h"
#include "picoquic.h"
#include "picoquic_packet_loop.h"
#include "picoquic_utils.h"
#include "picosocks.h"
#include "picotls.h"

using namespace quicr::internal;

#define SERVER_CERT_FILE "cert.pem"
#define SERVER_KEY_FILE "key.pem"

///
/// log handler utility
///

template<typename... Ts>
void
log(quicr::LogLevel level, const Ts&... vals)
{
  auto ss = std::stringstream();
}

///
/// Quicr/Quic Stack callback handlers.
///

int
quicrq_app_loop_cb_check_fin(TransportContext* cb_ctx)
{
  int ret = 0;

  /* if a client, exit the loop if connection is gone. */
  quicrq_cnx_ctx_t* cnx_ctx = quicrq_first_connection(cb_ctx->qr_ctx);
  if (cnx_ctx == nullptr || quicrq_is_cnx_disconnected(cnx_ctx)) {
    ret = PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
  } else if (!quicrq_cnx_has_stream(cnx_ctx)) {
    // todo: don't close if no media has been posted yet
    // ret = quicrq_close_cnx(cnx_ctx);
  }

  return ret;
}

void
quicrq_app_check_source_time(TransportContext* cb_ctx,
                             packet_loop_time_check_arg_t* time_check_arg)
{
  // metric: log when this function gets called as influx point
  if (cb_ctx->transport->hasDataToSendToNet()) {
    time_check_arg->delta_t = 0;
    // log delta
    return;
  } else if (time_check_arg->delta_t > 3000) {
    // is this a good choice?
    time_check_arg->delta_t = 3000;
  }
  // log here delta
}

// media consumer object callback from quicr stack
int
object_stream_consumer_fn(
  quicrq_media_consumer_enum action,
  void* object_consumer_ctx,
  uint64_t /*current_time*/,
  uint64_t /*group_id*/,
  uint64_t /*object_id*/,
  const uint8_t* data,
  size_t data_length,
  quicrq_object_stream_consumer_properties_t* /*properties*/)
{
  auto cons_ctx = (ConsumerContext*)object_consumer_ctx;
  int ret = 0;
  switch (action) {
    case quicrq_media_datagram_ready: {
      if (!data) {
        std::cerr << "data is null" << std::endl;
        break;
      }
      auto payload = quicr::bytes(data, data + data_length);
      quicr::internal::QuicRTransport::Data recv_data = { cons_ctx->quicr_name,
                                                          std::move(payload) };
      cons_ctx->transport->recvDataFromNet(recv_data);
    } break;
    case quicrq_media_close:
      /* Remove the reference to the media context, as the caller will
       * free it. */
      cons_ctx->transport->on_media_close(cons_ctx);
      ret = 0;
      break;
    default:
      ret = -1;
      break;
  }

  return ret;
}

// main packet loop for the application
int
quicrq_app_loop_cb(picoquic_quic_t* /*quic*/,
                   picoquic_packet_loop_cb_enum cb_mode,
                   void* callback_ctx,
                   void* callback_arg)
{
  int ret = 0;
  auto* cb_ctx = (TransportContext*)callback_ctx;

  if (cb_ctx == nullptr) {
    std::cerr << "[quir-loopcb]: cb_ctx is null\n";
    return PICOQUIC_ERROR_UNEXPECTED_ERROR;
  }

  if (cb_ctx->transport->shutting_down) {
    std::cerr << "[quir-loopcb]: shutting down \n";
    return PICOQUIC_NO_ERROR_TERMINATE_PACKET_LOOP;
  }

  switch (cb_mode) {
    case picoquic_packet_loop_ready:
      if (callback_arg != nullptr) {
        auto* options = (picoquic_packet_loop_options_t*)callback_arg;
        options->do_time_check = 1;
      }
      if (cb_ctx->transport) {
        std::lock_guard<std::mutex> lock(
          cb_ctx->transport->quicConnectionReadyMutex);
        cb_ctx->transport->quicConnectionReady = true;
        std::cerr << "[quir-loopcb]: picoquic_packet_loop_ready " << std::endl;
      } else {
        // log error here
      }
      ret = 0;
      break;
    case picoquic_packet_loop_after_receive:
      /* Post receive callback */
      ret = quicrq_app_loop_cb_check_fin(cb_ctx);
      break;
    case picoquic_packet_loop_after_send:
      /* if a client, exit the loop if connection is gone. */
      ret = quicrq_app_loop_cb_check_fin(cb_ctx);
      break;
    case picoquic_packet_loop_port_update:
      break;
    case picoquic_packet_loop_time_check: {
      /* check local test sources */
      quicrq_app_check_source_time(cb_ctx,
                                   (packet_loop_time_check_arg_t*)callback_arg);
      quicr::internal::QuicRTransport::Data data;
      auto got = cb_ctx->transport->getDataToSendToNet(data);
      if (!got || data.app_data.empty()) {
        break;
      }
      auto& publish_ctx =
        cb_ctx->transport->get_publisher_context(data.quicr_name);
      if (!publish_ctx.object_source_ctx) {
        // log warn
        break;
      }

      uint64_t group_id = 0;
      uint64_t object_id = 0;

      ret =
        quicrq_publish_object(publish_ctx.object_source_ctx,
                              reinterpret_cast<uint8_t*>(data.app_data.data()),
                              data.app_data.size(),
                              1,
                              nullptr,
                              &group_id,
                              &object_id);

      if (ret != 0) {
        // log error
      }
      cb_ctx->transport->on_object_published(
        data.quicr_name, group_id, object_id);

    } break;
    default:
      ret = PICOQUIC_ERROR_UNEXPECTED_ERROR;
      break;
  }
  return ret;
}

QuicRTransport::~QuicRTransport()
{
  logger.log(LogLevel::debug, "[quicr]: ~QuicRTransport");
  shutting_down = true;
  // ensure transport thread finishes
  // and resources are cleaned up.
  if (quicTransportThread.joinable()) {
    quicTransportThread.join();
  }
}

void
QuicRTransport::close()
{
  if (closed) {
    return;
  }

  // clean up publish sources
  if (!publishers.empty()) {
    for (auto const& [name, pub_ctx] : publishers) {
      if (pub_ctx.object_source_ctx) {
        quicrq_publish_object_fin(pub_ctx.object_source_ctx);
        quicrq_delete_object_source(pub_ctx.object_source_ctx);
        // logger->info << "Removed source [" << source_id << std::flush;
      }
    }
  }

  publishers.clear();

  if (!consumers.empty()) {
    for (auto const& [name, cons_ctx] : consumers) {
      if (cons_ctx.object_consumer_ctx) {
        quicrq_unsubscribe_object_stream(cons_ctx.object_consumer_ctx);
      }
    }
  }

  consumers.clear();

  if (quicr_ctx) {
    quicrq_delete(quicr_ctx);
  }

  if (transport_context.cn_ctx) {
    if (!quicrq_cnx_has_stream(cnx_ctx)) {
      quicrq_close_cnx(cnx_ctx);
    }
  }

  closed = true;
}

bool
QuicRTransport::hasDataToSendToNet()
{
  std::lock_guard<std::mutex> lock(sendQMutex);
  return !sendQ.empty();
}

// Provide data to be sent over the transport
// Called by underlying transport
bool
QuicRTransport::getDataToSendToNet(Data& data_out)
{
  // get packet to send from Q
  Data data;
  {
    std::lock_guard<std::mutex> lock(sendQMutex);
    if (sendQ.empty()) {
      return false;
    }

    data = std::move(sendQ.front());
    sendQ.pop();
  }

  data_out = std::move(data);
  return true;
}

void
QuicRTransport::recvDataFromNet(Data& data_in)
{
  // todo: support group_id and object_id reporting
  application_delegate.on_data_arrived(
    data_in.quicr_name, std::move(data_in.app_data), 0, 0);
}

void
QuicRTransport::register_publish_sources(
  const std::vector<std::string>& publisher_names)
{
  if (!quicr_ctx) {
    throw std::runtime_error("quicr context is empty\n");
  }

  for (auto& publisher : publisher_names) {
    auto obj_src_context = quicrq_publish_object_source(
      quicr_ctx,
      reinterpret_cast<uint8_t*>(const_cast<char*>(publisher.data())),
      publisher.length(),
      nullptr);
    assert(obj_src_context);
    auto pub_context = new PublisherContext{ publisher, obj_src_context, this };

    // enable publishing
    auto ret = quicrq_cnx_post_media(
      cnx_ctx,
      reinterpret_cast<uint8_t*>(const_cast<char*>(publisher.data())),
      publisher.length(),
      true);
    if (ret) {
      logger.log(LogLevel::error, "Failed to add publisher: ");
      continue;
    }

    logger.log(LogLevel::info, "Registered Source " + publisher);
    publishers[publisher] = std::move(*pub_context);
  }
}

void
QuicRTransport::unregister_publish_sources(
  const std::vector<std::string>& publisher_names)
{
  if (publishers.empty()) {
    return;
  }
  auto it = publishers.begin();
  while (it != publishers.end()) {
    if (!publishers.count(it->first)) {
      ++it;
      continue;
    }
    auto src_ctx = publishers[it->first];
    if (src_ctx.object_source_ctx) {
      quicrq_publish_object_fin(src_ctx.object_source_ctx);
      quicrq_delete_object_source(src_ctx.object_source_ctx);
      logger.log(LogLevel::info, "Removed source [" + it->first + "]");
    }
    it = publishers.erase(it);
  }
}

void
QuicRTransport::subscribe(const std::vector<std::string>& names)
{
  if (names.empty()) {
    logger.log(LogLevel::warn, "Empty subscribe list");
    return;
  }

  for (auto& name : names) {
    auto consumer_media_ctx = new ConsumerContext{};
    memset(consumer_media_ctx, 0, sizeof(ConsumerContext));
    consumer_media_ctx->quicr_name = name;
    consumer_media_ctx->transport = this;
    constexpr auto use_datagram = true;
    constexpr auto in_order = true;

    consumer_media_ctx->object_consumer_ctx = quicrq_subscribe_object_stream(
      cnx_ctx,
      reinterpret_cast<uint8_t*>(const_cast<char*>(name.data())),
      name.length(),
      use_datagram,
      in_order,
      object_stream_consumer_fn,
      consumer_media_ctx);

    assert(consumer_media_ctx->object_consumer_ctx);

    consumers[name] = *consumer_media_ctx;
    // logger.log(LogLevel::info, "Subscriber ")
  }
}

void
QuicRTransport::unsubscribe(const std::vector<std::string>& names)
{
  if (consumers.empty()) {
    return;
  }

  auto it = consumers.begin();
  while (it != consumers.end()) {
    if (!consumers.count(it->first)) {
      ++it;
      continue;
    }
    auto cons_ctx = consumers[it->first];
    if (cons_ctx.object_consumer_ctx) {
      quicrq_unsubscribe_object_stream(cons_ctx.object_consumer_ctx);
    }
    it = consumers.erase(it);
  }
}

void
QuicRTransport::publish_named_data(const std::string& url, Data&& data)
{
  logger.log(LogLevel::debug, "[quicr]: publish media on " + url);
  std::lock_guard<std::mutex> lock(sendQMutex);
  sendQ.push(std::move(data));
}

void
QuicRTransport::on_object_published(const std::string& name,
                                    uint64_t group_id,
                                    uint64_t object_id)
{
  application_delegate.on_object_published(name, group_id, object_id);
}

void
QuicRTransport::on_media_close(ConsumerContext* cons_ctx)
{
  if (cons_ctx && cons_ctx->object_consumer_ctx) {
    if (consumers.empty()) {
      logger.log(LogLevel::warn, "on_media_close: Consumer Context missing");
      return;
    }
    auto it = consumers.begin();
    while (it != consumers.end()) {
      if (it->second.object_consumer_ctx == cons_ctx->object_consumer_ctx) {
        application_delegate.on_connection_close(it->first);
        it = consumers.erase(it);
      } else {
        ++it;
      }
    }
  }
}

QuicRTransport::QuicRTransport(QuicRClient::Delegate& delegate_in,
                               const std::string& sfuName,
                               const uint16_t sfuPort)
  : quicConnectionReady(false)
  , quicr_ctx(quicrq_create_empty())
  , application_delegate(delegate_in)
  , logger(delegate_in)
{
  logger.log(LogLevel::info, "Quicr Client Transport");

  picoquic_config_init(&config);
  picoquic_config_set_option(&config, picoquic_option_ALPN, QUICRQ_ALPN);
  // debug_set_stream(stdout);
  quic = picoquic_create_and_configure(
    &config, quicrq_callback, quicr_ctx, picoquic_current_time(), NULL);

  if (!quic) {
    throw std::runtime_error("unable to create picoquic context");
  }

  logger.log(LogLevel::info, "Created QUIC handle");

  picoquic_set_key_log_file_from_env(quic);
  picoquic_set_mtu_max(quic, config.mtu_max);

  // todo - take the path to log
  config.qlog_dir = "/tmp";
  if (config.qlog_dir != nullptr) {
    picoquic_set_qlog(quic, config.qlog_dir);
  }

  // update quicr context with the quic stack
  quicrq_set_quic(quicr_ctx, quic);

  struct sockaddr_storage addr = { 0 };
  int is_name = 0;
  char const* sni = nullptr;

  int ret =
    picoquic_get_server_address(sfuName.c_str(), sfuPort, &addr, &is_name);
  if (ret != 0) {
    throw std::runtime_error("Cannot find the servr address");
  } else if (is_name != 0) {
    sni = sfuName.c_str();
  }

  if ((cnx_ctx = quicrq_create_client_cnx(
         quicr_ctx, sni, (struct sockaddr*)&addr)) == nullptr) {
    throw std::runtime_error("cannot create connection to the server");
  }

  transport_context.transport = this;
  transport_context.qr_ctx = quicr_ctx;
  transport_context.cn_ctx = cnx_ctx;
}

void
QuicRTransport::start()
{
  quicTransportThread = std::thread(quicTransportThreadFunc, this);
}

bool
QuicRTransport::ready()
{
  bool ret;
  {
    std::lock_guard<std::mutex> lock(quicConnectionReadyMutex);
    ret = quicConnectionReady;
  }
  if (ret) {
    logger.log(LogLevel::info, "QuicrTransport::ready()");
  }
  return ret;
}

// Main quic process thread and the packet loop
int
QuicRTransport::runQuicProcess()
{
  logger.log(LogLevel::debug, "[quicr]: Starting Packet Loop");
  // run the packet loop
  int ret = picoquic_packet_loop(quic,
                                 0,
                                 0,
                                 config.dest_if,
                                 config.socket_buffer_size,
                                 config.do_not_use_gso,
                                 quicrq_app_loop_cb,
                                 &transport_context);

  std::cerr << "QuicrLoop Done Ret " << ret << std::endl;
  logger.log(LogLevel::info, "Quicr loop Done ");
  close();
  return ret;
}