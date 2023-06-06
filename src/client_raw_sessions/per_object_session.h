#pragma once

#include "per_category_session.h"

namespace quicr {
class ClientRawSession_PerObject : public ClientRawSession_PerCategory
{
public:
  using ClientRawSession_PerCategory::ClientRawSession_PerCategory;

protected:
  virtual std::optional<std::pair<Namespace, PublishContext&>>
  findPublishStream(Name name)
  {
    auto found = publish_state.find(name);
    if (found == publish_state.end()) {
      LOG_INFO(logger,
               "No publish intent for '" << name << "' missing, dropping");
      return std::nullopt;
    }

    return *found;
  }

  virtual void createPublishStream(PublishContext& context,
                                   bool use_reliable_transport)
  {
    if (context.name != context.prev_name)
      if (uint64_t diff = uint64_t(context.name - context.prev_name)) {
        transport->closeStream(_context_id, context.stream_id);
        context.state = PublishContext::State::Pending;
      }

    ClientRawSession_PerCategory::createPublishStream(context,
                                                      use_reliable_transport);
  }
};

}
