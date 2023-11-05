#pragma once
#include <memory>

#include "server_delegate.h"
#include "sub_delegate.h"
#include "pub_delegate.h"
#include "uri_convertor.h"

// MoQ Endpoint

struct MoqEndPoint {
  MoqEndPoint(std::string& relay_in,
              uint16_t port,
              std::shared_ptr<NumeroURIConvertor> uri_convertor,
              std::shared_ptr<ServerDelegate> delegate,
              std::shared_ptr<cantina::Logger> logger);
  ~MoqEndPoint() = default;

  void run();

private:
  std::shared_ptr<NumeroURIConvertor> uri_convertor { nullptr };
  std::shared_ptr<ServerDelegate> server_delegate { nullptr };
  std::shared_ptr<cantina::Logger> logger { nullptr };
  std::unique_ptr<QSession> qsession { nullptr };
};
