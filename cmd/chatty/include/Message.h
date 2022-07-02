#pragma once

#include <quicr/quicr_client.h>

class Message
{
public:
  Message(const std::string& name, const std::string& msg);

  quicr::bytes operator()();

private:
  std::string sender;
  std::string data;
};
