#include <csignal>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

#include "ChannelMsgDelegate.h"
#include "ChatClient.h"

namespace {
std::function<void(int)> shutdown_signal_handle;
void
handle_shutdown_signal(int signal)
{
  shutdown_signal_handle(signal);
}

const std::vector<std::string> channels{
  "Team1",
  "Team2",
  "Team3",
  "Team4",
};
} // namespace

int
main(int argc, char** argv)
{
  if (argc != 4) {
    return -1;
  }

  std::string server = argv[1];
  int port = std::atoi(argv[2]);
  std::string me = argv[3];

  std::signal(SIGINT, handle_shutdown_signal);
  std::signal(SIGKILL, handle_shutdown_signal);

  ChatClient client(server, port);
  shutdown_signal_handle = [&client](int) { client.Shutdown(); };
  client.OnReceive = [](auto channel, const auto& data) {
    std::string msg;
    msg.reserve(data.size());
    std::transform(data.begin(), data.end(), std::back_inserter(msg), [](uint8_t c) { return static_cast<char>(c); });

    std::cout << "[" << data.size() << "B:<<<<][" << channel << "] " << msg << std::endl;
  };

  client.Login(me);
  client.Subscribe(channels);
  client.Join(channels[0]);

  std::thread receive_thread(&ChatClient::ReceiveLoop, &client);

  // TODO: Not a very complicated send thread...
  std::thread send_thread([&] {
    std::string msg;

    std::cout << "Send message: ";
    std::cin >> msg;
    client.Send(msg);
  });

  receive_thread.join();
  send_thread.join();

  return 0;
}
