#include "pub_delegate.h"
#include <sstream>

PubDelegate::PubDelegate(cantina::LoggerPointer logger_in,
                         std::promise<bool> on_response_in)
  : logger(std::move(logger_in))
  , on_response(std::move(on_response_in))
{
}

void
PubDelegate::onPublishIntentResponse(const quicr::Namespace& quicr_namespace,
                                     const quicr::PublishIntentResult& result)
{
  std::stringstream log_msg;
  log_msg << "onPublishIntentResponse: name: " << quicr_namespace
          << " status: " << static_cast<int>(result.status);

  logger->Log(log_msg.str());

  if (on_response) {
    on_response->set_value(result.status == quicr::messages::Response::Ok);
    on_response.reset();
  }
}