#pragma once

#include "async_queue.h"

#include <cantina/logger.h>
#include <quicr/quicr_client_delegate.h>

#include <future>


struct QuicrObject
{
  quicr::Name name;
  quicr::bytes data;
};

class SubDelegate : public quicr::SubscriberDelegate
{
public:
  SubDelegate(cantina::LoggerPointer logger_in,
              std::shared_ptr<AsyncQueue<QuicrObject>> queue_in,
              std::promise<bool> on_response_in);

  void onSubscribeResponse(const quicr::Namespace& quicr_namespace,
                           const quicr::SubscribeResult& result) override;

  void onSubscriptionEnded(
    const quicr::Namespace& quicr_namespace,
    const quicr::SubscribeResult::SubscribeStatus& reason) override;

  void onSubscribedObject(const quicr::Name& quicr_name,
                          uint8_t priority,
                          uint16_t expiry_age_ms,
                          bool use_reliable_transport,
                          quicr::bytes&& data) override;

  void onSubscribedObjectFragment(const quicr::Name& quicr_name,
                                  uint8_t priority,
                                  uint16_t expiry_age_ms,
                                  bool use_reliable_transport,
                                  const uint64_t& offset,
                                  bool is_last_fragment,
                                  quicr::bytes&& data) override;

private:
  cantina::LoggerPointer logger;
  std::shared_ptr<AsyncQueue<QuicrObject>> queue;
  std::optional<std::promise<bool>> on_response;
};