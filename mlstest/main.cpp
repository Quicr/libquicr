
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

using namespace mls;

int main(int argc, char* argv[])
{
  if (argc != 3) {
    std::cerr
      << "MLS address and port set in MLS_RELAY and MLS_PORT env "
         "variables."
      << std::endl;
    std::cerr << std::endl;
    std::cerr << "Usage quicr_mlstest creator(1)/joiner(0) user_id "<< std::endl;
    std::cerr << "Ex: quicr_mlstest 1 alice" << std::endl;
    exit(-1);
  }


  testLogger logger;
  std::stringstream log_msg;
  namespaceConfig nspace_config;

  bool is_creator;
  std::istringstream(argv[1]) >> is_creator;
  //int is_creator = atoi(argv[1]);
  auto user_string = std::string(argv[2]);
  auto name = quicr::Name(std::string(argv[1]));
  log_msg << "Name = " << name.to_hex();

  QuicrClientHelper quicrclienthelper {user_string, logger, is_creator};

  //MlsUserSession session = MlsUserSession(from_ascii("1234"), from_ascii(user_string));
  //auto state = session.make_state();

  for (auto const& [nspace, op] : nspace_config.subscribe_op_map) {
    if (is_creator) {
      quicrclienthelper.subscribe(nspace, logger);
      logger.log(qtransport::LogLevel::info,
                 "Subscribing to namespace "+ nspace.to_hex());
      std::this_thread::sleep_for(std::chrono::seconds(30));

      logger.log(qtransport::LogLevel::info,
                 "Sleeping for 30 seconds before unsubscribing");
    } else {
      switch (op) {
        case SUBSCRIBE_OP_TYPE::KeyPackage: {
          auto name = nspace.name();
          logger.log(qtransport::LogLevel::info,
                     "Publishing to " + name.to_hex());
          quicrclienthelper.publishJoin(name);
          break;
        }
        default:
          break;
      }
    }
  }

  std::this_thread::sleep_for(std::chrono::seconds(5));

  return 0;
}