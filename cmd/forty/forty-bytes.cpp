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
std::atomic<bool> done{};

bool chat_mode = false;

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
  void on_data_arrived(const std::string& /*name*/,
                       bytes&& data,
                       uint64_t /*group_id*/,
                       uint64_t /*object_id*/) override
  {
    std::lock_guard<std::mutex> lock(recv_q_mutex);
    recv_q.push(data);
  }

  virtual void on_connection_close(const std::string& name) override
  {
    std::cout << "consumer connection closed: " << name << "\n";
  }

  virtual void on_object_published(const std::string& name,
                                   uint64_t group_id,
                                   uint64_t object_id) override
  {
    std::cout << name << " object_published: group:" << group_id
              << " object_id " << object_id << std::endl;
  }

  virtual void log(LogLevel /*level*/, const std::string& message) override
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
  std::string chat_message;
  while (!done) {
    auto data = delegate->recv();
    if (data.empty()) {
      continue;
    }
    if (chat_mode) {
        auto msg = std::string(data.begin(), data.end());
        std::cout << chat_message << std::endl;
        if (msg == "end") {
            std::cout << "[<<<<] " << chat_message << std::endl;
            chat_message= "";
        } else {
            chat_message += msg;
        }

    } else {
        std::cout << "[40B:<<<<] " << to_hex(data) << std::endl;
    }

  }
  std::cout << "read_loop done\n";
}

void
send_loop(QuicRClient& qclient, std::string& name)
{
  const uint8_t forty_bytes[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3,
                                  4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7,
                                  8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
  auto group_id = 0;
  auto object_id = 0;
  auto GROUP_SIZE = 50;

  while (!done) {

    if (chat_mode) {
      std::string msg;
      std::cout << "Send message: ";
      std::cin >> msg;
      msg += "end";
      auto msg_bytes = bytes(msg.begin(), msg.end());
      qclient.publish_named_data(name, std::move(msg_bytes), group_id, object_id, 0, 0);
    } else {
      auto data = bytes(forty_bytes, forty_bytes + sizeof(forty_bytes));
      std::cout << "[40B:>>>>>] " << to_hex(data) << std::endl;
      qclient.publish_named_data(name, std::move(data), group_id, object_id, 0x81, 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      object_id += 1;
    }

    if(object_id >= GROUP_SIZE) {
      group_id += 1;
      object_id = 0;
      std::cout << "[40B:>>>>>] New GROUP " << group_id << std::endl;
    }

  }
  std::cout << "done send_loop\n";
  auto qname = QuicrName{ name, 0 };
  qclient.unregister_names({ qname });
}

void
signal_callback_handler(int /*signum*/)
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
  uint16_t server_port = 7777;
  std::string server_ip;
  if (argc < 5) {
    std::cerr << "Usage: forty <server> <port> <mode> <self-client-id> "
                 "<other-client-id> <mask-length>"
              << std::endl;
    std::cerr << "port: server ip for quicr origin/relay" << std::endl;
    std::cerr << "port: server port for quicr origin/relay" << std::endl;
    std::cerr << "mode: sendrecv/send/recv" << std::endl;
    std::cerr << "self-client-id: some string" << std::endl;
    std::cerr << "other-client-id: some string that is not self" << std::endl;
    std::cerr << "mask-length: Length of mask when subscribing in octets"
              << std::endl;
    return -1;
  }

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

  // mask length
  std::string mask_str;
  size_t mask = 0;
  if (argv)
    mask_str.assign(argv[6]);
  if (!mask_str.empty()) {
    mask = std::stoi(mask_str, nullptr);
  }

  // Delegate
  auto delegate = std::make_unique<Forty>();
  // QuicRClient
  auto qclient =
    std::make_unique<QuicRClient>(*delegate, server_ip, server_port);

  setup_signal_handlers();

  if (mode == "recv") {
    if (you.empty()) {
      std::cout << "Bad choice for other-client-id" << std::endl;
      exit(-1);
    }

    // all the params are not used in the lib yet
    auto qname = QuicrName{ you, mask };
    auto intent = SubscribeIntent{SubscribeIntent::Mode::wait_up, 0, 0};
    qclient->subscribe(std::vector<QuicrName>{ qname },
                       intent,
                       false,
                       false);

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
    qclient->register_names({ { me, 0 } }, true);

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

    qclient->register_names({ { me, 0 } }, true);
    auto intent = SubscribeIntent{SubscribeIntent::Mode::immediate, 0, 0};
    qclient->subscribe({ { you, mask } },
                       intent,
                       false,
                       false);

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
