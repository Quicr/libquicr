#include "mls_user_session.h"

// Add user to a group as creator
  MlsUserSession::MlsUserSession(const bytes& group, const bytes& user)
  : group_id(group)
    , user_id(user)
  {
    // create leaf info
    setup_user(user, CipherSuite { CipherSuite::ID::P256_AES128GCM_SHA256_P256 });
  }

  const KeyPackage& MlsUserSession::get_key_package() const{
    return keypackage;
  }

  void MlsUserSession::setup_user(const bytes& user_id, CipherSuite suite)
  {
    auto ext_list = ExtensionList{};

    auto capas = Capabilities::create_default();
    signing_key = SignaturePrivateKey::generate(suite);
    credential = Credential::basic(user_id);
    init_key = HPKEPrivateKey::generate(suite);
    leaf_key = HPKEPrivateKey::generate(suite);
    auto leaf_node = LeafNode{ suite,
                               leaf_key.public_key,
                               signing_key.public_key,
                               credential,
                               capas,
                               Lifetime::create_default(),
                               {},
                               signing_key };
    keypackage = KeyPackage{ suite, init_key.public_key, leaf_node, {}, signing_key };
  }

  void MlsUserSession::make_state() {
    //create mls state
    mls_state = std::unique_ptr<State>(new State{group_id, suite, leaf_key, signing_key,keypackage.leaf_node, {}});

  }

  void MlsUserSession::process_key_package(std::vector<uint8_t>&& data)
  {
    auto kp = tls::get<KeyPackage>(data);
    mls_state->add_proposal(kp);

  }