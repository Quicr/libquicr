#ifndef QUICR_QUICR_CLIENT_SECURE_H
#define QUICR_QUICR_CLIENT_SECURE_H

#include <chrono>
#include <cstring>
#include <iostream>
#include <quicr/quicr_client.h>
#include <quicr/quicr_common.h>
#include <sstream>
#include <thread>

#include <hpke/random.h>
#include <mls/common.h>
#include <mls/state.h>
#include <bytes/bytes.h>

#include "mls_user_session.h"
#include "pub_delegate.h"
#include "quicr_client_helper.h"
#include "testLogger.h"
#include "fake_config.h"
#include <array>

using namespace mls;

struct common_utils
{
  testLogger logger;
  std::stringstream log_msg;
  namespaceConfig nspace_config;
  QuicrClientHelper creator{ std::string("FFFOOO"), logger, true };
  std::array<QuicrClientHelper, 1> participants = { QuicrClientHelper(std::string("FFFOO1"), logger, false)};
};

#endif // QUICR_QUICR_CLIENT_SECURE_H
