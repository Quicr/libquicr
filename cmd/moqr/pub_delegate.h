#pragma once
#include <cantina/logger.h>
#include <quicr/quicr_client_delegate.h>

#include <future>

class PubDelegate : public quicr::PublisherDelegate
{
public:
  PubDelegate(cantina::LoggerPointer logger_in,
              std::promise<bool> on_response_in);

  void onPublishIntentResponse(
    const quicr::Namespace& quicr_namespace,
    const quicr::PublishIntentResult& result) override;

private:
  cantina::LoggerPointer logger;
  std::optional<std::promise<bool>> on_response;
};