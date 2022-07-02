#pragma once

#include <quicr/quicr_client.h>

#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>

struct DelegateException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

class ChannelMsgDelegate : public quicr::QuicRClient::Delegate
{
public:
  quicr::bytes Receive(const std::string& channel);

private:
  virtual void on_data_arrived(const std::string& name,
                               quicr::bytes&& data,
                               std::uint64_t group_id,
                               std::uint64_t object_id) override;

  virtual void on_connection_close(const std::string& name) override;

  virtual void log(quicr::LogLevel level, const std::string& message) override;

  virtual void on_object_published(const std::string& name, uint64_t group_id, uint64_t object_id) override;

private:
  std::mutex queue_mutex;
  std::map<std::string, std::queue<quicr::bytes>> received_byte_queues;
};
