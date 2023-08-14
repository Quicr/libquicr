#pragma once

#include "stream_session_base.h"

namespace quicr {
class ClientPerObjectRawSession : public ClientStreamRawSessionBase
{
public:
  using ClientStreamRawSessionBase::ClientStreamRawSessionBase;

protected:
  virtual std::optional<std::pair<Namespace, PublishContext&>>
  findPublishStream(Name name)
  {
    if (auto found = publish_state.find(name); found != publish_state.end()) {
      return { {found->first, (*found).second} };
    }

    return std::nullopt;
  }

  virtual void createPublishStream(PublishContext& context,
                                   bool use_reliable_transport)
  {
    if (!use_reliable_transport || context.stream_id == transport_control_stream_id || context.stream_id == 0) return;

    transport->closeStream(transport_context_id, context.stream_id);
    context.stream_id = transport->createStream(transport_context_id, use_reliable_transport);
  }

  virtual bool detectJump(Name a, Name b) const
  {
    auto jump = a - b;
    return jump.bits<uint16_t>(0, 16) > 1u;
  }
};

}
