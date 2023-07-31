#pragma once
#include <hpke/random.h>
#include <mls/common.h>
#include <mls/state.h>
#include <bytes/bytes.h>

#include <memory>

using namespace mls;

class MlsUserSession
{
protected:
  const CipherSuite suite{ CipherSuite::ID::P256_AES128GCM_SHA256_P256 };

private:
  bytes group_id;
  bytes user_id;
  // Leaf Information
  KeyPackage keypackage;
  HPKEPrivateKey init_key;
  HPKEPrivateKey leaf_key;
  SignaturePrivateKey signing_key;
  Credential credential;

  std::unique_ptr<State> mls_state = nullptr;


  void setup_user(const bytes& user_id, CipherSuite suite);


  // create from welcome
  State make_state(bytes&& welcome_data);


public:

  MlsUserSession(const bytes& group_id, const bytes& user);
  const KeyPackage& get_key_package() const ;
  // group creator
  void make_state();
  void process_key_package(std::vector<uint8_t>&& data);
};