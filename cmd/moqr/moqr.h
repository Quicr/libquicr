#pragma once
#include <optional>
#include "qsession.h"

struct MoqRelay {
  MoqRelay();
  ~MoqRelay() = default;
  void HandleQSessionMessages(QuicrObject&& obj);
private:
  std::optional<std::thread> handler_thread;
  static constexpr auto inbound_object_timeout = std::chrono::milliseconds(100);
  std::shared_ptr<AsyncQueue<QuicrObject>> inbound_objects;
  std::shared_ptr<QSession> quicr_session = nullptr;
};

