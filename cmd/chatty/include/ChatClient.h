#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <quicr/quicr_client.h>

class ChannelMsgDelegate;

struct ChatClientException : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

class ChatClient
{
public:
  ChatClient(const std::string& ip, int port);
  ~ChatClient() = default;

  void Login(const std::string& name);

  void Start();

  void Shutdown();

  void ReceiveLoop();

  void Send(const std::string& msg);

  // TODO: This is probably unneeded.
  void Join(const std::string& channel);

  void Subscribe(const std::string& channel);
  void Subscribe(const std::vector<std::string>& channels);

  void Unsubscribe(const std::string& channel);
  void Unsubscribe(const std::vector<std::string>& channels);

  std::vector<std::string> GetChannels() const;

  std::function<void(const std::string& name, const quicr::bytes& data)> OnReceive;

private:
  void check_client() const;

  std::atomic_bool done = false;
  mutable std::mutex loop_mutex;

  std::shared_ptr<ChannelMsgDelegate> delegate;
  std::unique_ptr<quicr::QuicRClient> client;

  // TODO: This is probably unneeded.
  std::string username;

  // TODO: This is probably unneeded.
  std::string active_channel;
};
