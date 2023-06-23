#pragma once
#include <hpke/random.h>
#include <mls/common.h>
#include <mls/state.h>
#include <bytes/bytes.h>
using namespace mls;

class MlsUserSession
{
  const CipherSuite suite{ CipherSuite::ID::P256_AES128GCM_SHA256_P256 };
  State mls_state;
  KeyPackage self_keypackage;

public:
  MlsUserSession(const bytes& group_id, const bytes& user);
  KeyPackage get_key_package();
  void set_self_keypackage(KeyPackage keypackage);
  State make_state(const bytes& group_id, const bytes& user_id, CipherSuite suite);
  std::tuple<HPKEPrivateKey, HPKEPrivateKey, SignaturePrivateKey, KeyPackage>
  make_client(bytes user_id, CipherSuite suite);
};