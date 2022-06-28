#include "ChannelMsgDelegate.h"

#include <iostream>

quicr::bytes
ChannelMsgDelegate::Receive(const std::string& channel)
{
  std::lock_guard<std::mutex> lock(queue_mutex);

  auto found = received_byte_queues.find(channel);
  if (found == received_byte_queues.end()) {
    return {};
  }

  auto& received_byte_queue = found->second;
  if (received_byte_queue.empty()) {
    return {};
  }

  auto data = received_byte_queue.front();
  received_byte_queue.pop();

  if (received_byte_queue.empty())
    received_byte_queues.erase(found);

  return data;
}

void
ChannelMsgDelegate::on_data_arrived(const std::string& name,
                             quicr::bytes&& data,
                             uint64_t /* group_id */,
                             uint64_t /* object_id */)
{
  std::lock_guard<std::mutex> lock(queue_mutex);
  received_byte_queues[name].push(data);
}

void
ChannelMsgDelegate::on_connection_close(const std::string& name)
{
  log(quicr::LogLevel::info, "Connection to channel \"" + name + "\" closed");
}

void
ChannelMsgDelegate::log(quicr::LogLevel /*level*/, const std::string& message)
{
  std::clog << "[log] " << message << std::endl;
}
