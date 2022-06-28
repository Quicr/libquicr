#include "ChatClient.h"
#include "ChannelMsgDelegate.h"
#include "Message.h"

#include <iomanip>
#include <iostream>

ChatClient::ChatClient(const std::string& ip, int port)
  : delegate{ std::make_shared<ChannelMsgDelegate>() }
  , client{ std::make_unique<quicr::QuicRClient>(*delegate, ip, port) }
{
}

void
ChatClient::Login(const std::string& name)
{
  std::lock_guard<std::mutex> lock(loop_mutex);

  check_client();

  username = name;
  client->register_names({ username }, true);

  Start();
}

void
ChatClient::Start()
{
  done = false;
}

void
ChatClient::Shutdown()
{
  done = true;
  client->unregister_names({ username });
}

void
ChatClient::ReceiveLoop()
{
  check_client();

  while (!done) {
    std::lock_guard<std::mutex> lock(loop_mutex);

    auto data = delegate->Receive(active_channel);
    if (data.empty())
      continue;

    if (OnReceive)
      OnReceive(active_channel, data);
  }
}

void
ChatClient::Send(const std::string& str)
{
  check_client();

  if (active_channel.empty())
    return;

  Message msg(username, str);
  auto data = msg();

  // TODO: Publish to active_channel, or username?
  client->publish_named_data(active_channel, std::move(data), 0, 0);
}

void
ChatClient::Join(const std::string& channel)
{
  if (!active_channel.empty())
    client->unregister_names({ active_channel });

  active_channel = channel;

  // TODO: Is this right? It is required for publishing, but this feels wrong.
  client->register_names({ active_channel }, true);
}

void
ChatClient::Subscribe(const std::string& channel)
{
  std::lock_guard<std::mutex> lock(loop_mutex);

  check_client();

  client->subscribe({ channel }, false, false);
}

void
ChatClient::Subscribe(const std::vector<std::string>& channels)
{
  std::lock_guard<std::mutex> lock(loop_mutex);

  check_client();

  client->subscribe(channels, false, false);
}

void
ChatClient::Unsubscribe(const std::string& channel)
{
  std::lock_guard<std::mutex> lock(loop_mutex);

  check_client();

  client->unsubscribe({ channel });
}

void
ChatClient::Unsubscribe(const std::vector<std::string>& channels)
{
  std::lock_guard<std::mutex> lock(loop_mutex);

  check_client();

  client->unsubscribe(channels);
}

std::vector<std::string>
ChatClient::GetChannels() const
{
  check_client();

  return {};
}

void
ChatClient::check_client() const
{
  if (!client) {
    throw ChatClientException("Chat client must be initialized");
  }

  if (!delegate) {
    throw DelegateException("Message delegate must be initialized");
  }
}
