#include <quicr/quicr_client.h>
#include <signal.h>

#include <atomic>
#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>

using namespace quicr;
using namespace std::chrono_literals;
std::atomic<bool> done = false;

static std::string
to_hex(const std::vector<uint8_t>& data)
{
  std::stringstream hex(std::ios_base::out);
  hex.flags(std::ios::hex);
  for (const auto& byte : data) {
    hex << std::setw(2) << std::setfill('0') << int(byte);
  }
  return hex.str();
}

// Delegate Implementation
struct Forty : QuicRClient::Delegate
{
  void on_data_arrived(const std::string& name,
                       bytes&& data,
                       uint64_t group_id,
                       uint64_t object_id) override
  {
    std::lock_guard<std::mutex> lock(recv_q_mutex);
    recv_q.push(data);
  }

  void on_connection_close(const std::string& name)
  {
    std::cout << "consumer connection closed: " << name << "\n";
  }

  void log(LogLevel /*level*/, const std::string& message)
  {
    std::cerr << message << std::endl;
  }

  bytes recv()
  {
    std::lock_guard<std::mutex> lock(recv_q_mutex);
    if (recv_q.empty()) {
      return bytes{};
    }
    auto data = recv_q.front();
    recv_q.pop();
    return data;
  }

private:
  std::mutex recv_q_mutex;
  std::queue<bytes> recv_q;
};

void
read_loop(Forty* delegate)
{
  std::cout << "Client read audio loop init\n";
  while (!done) {
    auto data = delegate->recv();
    if (data.empty()) {
      continue;
    }
    std::cout << "[40B:<<<<] " << to_hex(data) << std::endl;
  }
  std::cout << "read_loop done\n";
}

void
send_loop(QuicRClient& qclient, const std::string& name)
{
  const uint8_t forty_bytes[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3,
                                  4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  uint32_t pkt_num = 0;

  while (!done) {
    auto data = bytes(forty_bytes, forty_bytes + sizeof(forty_bytes));
    std::cout << "[40B:>>>>>] " << to_hex(data) << std::endl;
    qclient.publish_named_data(name, std::move(data), 0, 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "done send_loop\n";
}

void
signal_callback_handler(int signum)
{
  done = true;
}

void
setup_signal_handlers()
{
  signal(SIGINT, signal_callback_handler);
}
int
main(int argc, char* argv[])
{
  std::string mode;
  std::string transport_type;
  std::string me;
  std::string you;
  uint64_t source_id = 0x1000;
  uint16_t server_port = 7777;
  std::string server_ip;
  if (argc < 5) {
    std::cerr << "Usage: forty <server> <port> <mode> <self-client-id> "
                 "<other-client-id>"
              << std::endl;
    std::cerr << "port: server ip for quicr origin/relay" << std::endl;
    std::cerr << "port: server port for quicr origin/relay" << std::endl;
    std::cerr << "mode: sendrecv/send/recv" << std::endl;
    std::cerr << "self-client-id: some string" << std::endl;
    std::cerr << "other-client-id: some string that is not self" << std::endl;
    return -1;
  }

  setup_signal_handlers();

  // server ip and port
  server_ip.assign(argv[1]);
  std::string port_str;
  port_str.assign(argv[2]);
  if (port_str.empty()) {
    std::cout << "Port is empty" << std::endl;
    exit(-1);
  }
  server_port = std::stoi(port_str, nullptr);

  // mode and names
  mode.assign(argv[3]);
  if (mode != "send" && mode != "recv" && mode != "sendrecv") {
    std::cout << "Bad choice for mode.. Bye" << std::endl;
    exit(-1);
  }
  me.assign(argv[4]);
  you.assign(argv[5]);

  // Delegate
  auto delegate = std::make_unique<Forty>();
  // QuicRClient
  auto qclient =
    std::make_unique<QuicRClient>(*delegate, server_ip, server_port);

  if (mode == "recv") {
    if (you.empty()) {
      std::cout << "Bad choice for other-client-id" << std::endl;
      exit(-1);
    }

    // all the params are not used in the lib yet
    qclient->subscribe(std::vector<std::string>{ you }, false, false);

    while (!qclient->is_transport_ready()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::cout << "Transport is ready" << std::endl;
    read_loop(delegate.get());
  } else if (mode == "send") {
    if (me.empty()) {
      std::cout << "Bad choice for self-client-id" << std::endl;
      exit(-1);
    }

    // all the params are not used in the lib yet
    qclient->register_names(std::vector<std::string>{ me }, true);

    while (!qclient->is_transport_ready()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::cout << "Transport is ready" << std::endl;

    send_loop(*qclient.get(), me);
  } else {
    if (me.empty() || you.empty()) {
      std::cout << "Bad choice for clientId(s)" << std::endl;
      exit(-1);
    }

    qclient->register_names(std::vector<std::string>{ me }, true);
    qclient->subscribe(std::vector<std::string>{ you }, false, false);

    while (!qclient->is_transport_ready()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::cout << "Transport is ready" << std::endl;

    std::thread reader(read_loop, delegate.get());
    send_loop(*qclient.get(), me);
    reader.join();
  }

  return 0;
}
