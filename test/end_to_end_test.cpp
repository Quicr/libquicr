#include <memory>
#include <vector>

#include <doctest/doctest.h>
#include <quicr/quicr_client.h>
#include <quicr/quicr_server.h>

#include "../src/encode.h"
#include "fake_transport.h"

/*
 *
 */
struct TestManager
{

  quicr::QuicRServer server;
  quicr::QuicRClient client;
};
