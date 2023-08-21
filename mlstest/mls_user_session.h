#pragma once
#include <hpke/random.h>
#include <mls/common.h>
#include <mls/state.h>
#include <bytes/bytes.h>

#include <memory>

using namespace mls;


// Information needed per user to populate MLS state
struct MLsUserInfo {
  std::string user;
  std::string group;
  CipherSuite suite;
  KeyPackage keypackage;
  HPKEPrivateKey init_key;
  HPKEPrivateKey leaf_key;
  SignaturePrivateKey signing_key;
  Credential credential;
};


class MlsUserSession
{
public:
  // create key-package
  static MLsUserInfo setup_mls_userinfo(const std::string& user, const std::string& group, CipherSuite suite);

  // setup mls state for the creator
  static std::unique_ptr<MlsUserSession> create(const MLsUserInfo& info);
  // setup mls state for the joiners
  static std::unique_ptr<MlsUserSession> create_for_welcome(const MLsUserInfo& info, bytes&& welcome_data);

  MlsUserSession(mls::State&& state, const MLsUserInfo& user_info);
  const KeyPackage& get_key_package() const;
  // group creator
  std::tuple<bytes, bytes> process_key_package(std::vector<uint8_t>&& data);
  State& get_state();

private:

  void setup_user(const bytes& user_id, CipherSuite suite);
  State make_state(bytes&& welcome_data);
  bytes fresh_secret() const ;

  MLsUserInfo user_info{};
  State mls_state;

};
