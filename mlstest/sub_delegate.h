#pragma once
#include <quicr/quicr_client_delegate.h>
#include "QuicrMessageProxy.h"
#include "testLogger.h"

/// quicr delegates
class SubDelegate : public quicr::SubscriberDelegate
{
public:
  SubDelegate(QuicrMessageProxy* qclientProxy, testLogger& logger)
    : logger(logger),
     client_helper(qclientProxy)
  {}

  void onSubscribeResponse(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult& result) override;

  void onSubscriptionEnded(
    [[maybe_unused]] const quicr::Namespace& quicr_namespace,
    [[maybe_unused]] const quicr::SubscribeResult::SubscribeStatus& reason)
    override;

  void onSubscribedObject([[maybe_unused]] const quicr::Name& quicr_name,
                          [[maybe_unused]] uint8_t priority,
                          [[maybe_unused]] uint16_t expiry_age_ms,
                          [[maybe_unused]] bool use_reliable_transport,
                          [[maybe_unused]] quicr::bytes&& data) override;

  void onSubscribedObjectFragment(
    [[maybe_unused]] const quicr::Name& quicr_name,
    [[maybe_unused]] uint8_t priority,
    [[maybe_unused]] uint16_t expiry_age_ms,
    [[maybe_unused]] bool use_reliable_transport,
    [[maybe_unused]] const uint64_t& offset,
    [[maybe_unused]] bool is_last_fragment,
    [[maybe_unused]] quicr::bytes&& data) override;

private:
  testLogger& logger;
  QuicrMessageProxy* client_helper;
};