#include "sub_delegate.h"
#include <sstream>
#include <iostream>

void SubDelegate::onSubscribeResponse(
  [[maybe_unused]] const quicr::Namespace& quicr_namespace,
  [[maybe_unused]] const quicr::SubscribeResult& result)
{

  std::stringstream log_msg;
  log_msg << "onSubscriptionResponse: name: " << quicr_namespace.to_hex()
          << "/" << int(quicr_namespace.length())
          << " status: " << int(static_cast<uint8_t>(result.status));

  logger.log(qtransport::LogLevel::info, log_msg.str());
}

void SubDelegate::onSubscriptionEnded(
  [[maybe_unused]] const quicr::Namespace& quicr_namespace,
  [[maybe_unused]] const quicr::SubscribeResult::SubscribeStatus& reason)

{

  std::stringstream log_msg;
  log_msg << "onSubscriptionEnded: name: " << quicr_namespace.to_hex() << "/"
          << int(quicr_namespace.length());

  logger.log(qtransport::LogLevel::info, log_msg.str());
}

void SubDelegate::onSubscribedObject([[maybe_unused]] const quicr::Name& quicr_name,
                   [[maybe_unused]] uint8_t priority,
                   [[maybe_unused]] uint16_t expiry_age_ms,
                   [[maybe_unused]] bool use_reliable_transport,
                   [[maybe_unused]] quicr::bytes&& data)
{
  std::stringstream log_msg;
  std::cerr << "onSubscribedObject" << std::endl;
  log_msg << "recv object: name: " << quicr_name.to_hex()
          << " data sz: " << data.size();

  if (data.size())
    log_msg << " data: " << data.data();

  logger.log(qtransport::LogLevel::info, log_msg.str());
  client_helper->handle(quicr_name, std::move(data));
}

void SubDelegate::onSubscribedObjectFragment(
  [[maybe_unused]] const quicr::Name& quicr_name,
  [[maybe_unused]] uint8_t priority,
  [[maybe_unused]] uint16_t expiry_age_ms,
  [[maybe_unused]] bool use_reliable_transport,
  [[maybe_unused]] const uint64_t& offset,
  [[maybe_unused]] bool is_last_fragment,
  [[maybe_unused]] quicr::bytes&& data)
{
}