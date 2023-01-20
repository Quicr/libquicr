#include "Message.h"

#include <algorithm>

Message::Message(const std::string& name, const std::string& msg)
  : sender(name)
  , data(msg)
{
}

quicr::bytes
Message::operator()()
{

  quicr::bytes bytes;
  bytes.reserve(data.size());

  std::transform(std::begin(data),
                 std::end(data),
                 std::back_inserter(bytes),
                 [](char c) { return static_cast<std::uint8_t>(c); });

  return bytes;
}
