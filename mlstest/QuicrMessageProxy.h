#pragma once
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>


class QuicrMessageProxy {
public:
  virtual ~QuicrMessageProxy() = default;
  virtual void handle(const quicr::Name& name, quicr::bytes&& data) = 0;


};