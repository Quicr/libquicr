#include "test_server.h"

using namespace quicr;
using namespace quicr_test;

TestServer::TestServer(const ServerConfig& config)
  : Server(config)
{
}
