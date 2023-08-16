
#include "quicr_client_secure.h"
#include <doctest/doctest.h>

TEST_CASE("Test mlstest")
{
  CHECK_EQ(1,1);
}

TEST_CASE("Subscribe/Publish KeyPackage")
{
  auto user_string = std::string("FFFOOO");
  auto name = quicr::Name(std::string("FFF000"));
  bool is_creator = true;
  common_utils utils;
  utils.log_msg << "Name = " << name.to_hex();

  QuicrClientHelper creator_quicrclienthelper{ user_string, utils.logger, is_creator };
  for (auto const& [nspace, op] : utils.nspace_config.subscribe_op_map) {
    if (utils.creator.isUserCreator()) {
      utils.creator.subscribe(nspace, utils.logger);
      utils.logger.log(qtransport::LogLevel::info,
                       "Subscribing to namespace " + nspace.to_hex());
      //std::this_thread::sleep_for(std::chrono::seconds(30));

      //utils.logger.log(qtransport::LogLevel::info,
                      // "Sleeping for 30 seconds before unsubscribing");
    }
    if (!utils.participants[0].isUserCreator()) {
      switch (op) {
        case SUBSCRIBE_OP_TYPE::KeyPackage: {
          auto name = nspace.name();
          utils.logger.log(qtransport::LogLevel::info,
                     "Publishing to " + name.to_hex());
          utils.participants[0].publishJoin(name);
          break;
        }
        default:
          break;
      }
      std::this_thread::sleep_for(std::chrono::seconds(5));
      utils.logger.log(qtransport::LogLevel::info,
                       "Sleeping for 5 seconds before unsubscribing");

    }
  }
}