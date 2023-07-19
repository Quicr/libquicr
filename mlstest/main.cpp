
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

  int is_creator = atoi(argv[1]);
  auto user_string = std::string(argv[2]);
  auto name = quicr::Name(std::string(argv[1]));
  log_msg << "Name = " << name.to_hex();

  QuicrClientHelper quicrclienthelper {user_string, logger};

  MlsUserSession session = MlsUserSession(from_ascii("1234"), from_ascii(user_string));
  name = quicr::Name("0x0011223344556600000100000000ABCD");
  if(is_creator) {
    // subscribe to namespace for processing user join/leave requests
    auto nspace = quicr::Namespace(name, 96);
    logger.log(qtransport::LogLevel::info,
               "Subscribing to namespace "+ nspace.to_hex());

    quicrclienthelper.subscribe(nspace, logger);

   logger.log(qtransport::LogLevel::info,
              "Sleeping for 30 seconds before unsubscribing");
   std::this_thread::sleep_for(std::chrono::seconds(30));

   quicrclienthelper.unsubscribe(nspace);

   logger.log(qtransport::LogLevel::info,
              "Sleeping for 15 seconds before exiting");
   std::this_thread::sleep_for(std::chrono::seconds(15));
    // subscribe to commits
  } else {
    // subscribe to namespace for processing user join/leave requests
    // subscribe to commits
    // subscribe to welcome
    // publish join_request with key package
    // todo: use numero-uno library to convert from uri to hex representation
    logger.log(qtransport::LogLevel::info,
               "Publishing to " + name.to_hex());

    quicrclienthelper.publishJoin(name, session);
  }

  std::this_thread::sleep_for(std::chrono::seconds(5));

  return 0;
}