#pragma once

#include "per_category_session.h"

namespace quicr {
class ClientRawSession_PerGroup : public ClientRawSession_PerCategory
{
public:
  using ClientRawSession_PerCategory::ClientRawSession_PerCategory;

protected:
  virtual std::optional<std::pair<Namespace, PublishContext&>>
  findPublishStream(Name name)
  {
    if (auto found = publish_state.find(name & ~0x0_name << 16u);
        found != publish_state.end()) {
      return *found;
    }
    return std::nullopt;
  }

  virtual void createPublishStream(PublishContext& context,
                                   bool use_reliable_transport)
  {
    if (context.name == context.prev_name || !use_reliable_transport ||
        context.stream_id == transport_control_stream_id ||
        context.stream_id == 0)
      return;

    uint64_t diff = uint32_t((context.name - context.prev_name) >> 16);
    if (!diff)
      return;

    transport->closeStream(transport_context_id, context.stream_id);
    context.stream_id =
      transport->createStream(transport_context_id, use_reliable_transport);
  }

  virtual bool detectJump(Name a, Name b) const
  {
    return uint32_t((a - b) >> 16) > 1u;
  }
};
}
