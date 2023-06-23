#include "mls_session.h"

  // Add user to a group as creator
  MlsUserSession::MlsUserSession(const bytes& group_id, const bytes& user) :
    mls_state(make_state(group_id, user, suite)) {
  }

  KeyPackage MlsUserSession::get_key_package() {
    return self_keypackage;
  }
  void MlsUserSession::set_self_keypackage(KeyPackage keypackage) {
    self_keypackage = keypackage;
  }
  std::tuple<HPKEPrivateKey, HPKEPrivateKey, SignaturePrivateKey, KeyPackage> MlsUserSession::make_client(bytes user_id, CipherSuite suite)
  {
    auto ext_list = ExtensionList{};

    auto capas = Capabilities::create_default();
    auto identity_priv = SignaturePrivateKey::generate(suite);
    auto credential = Credential::basic(user_id);
    auto init_priv = HPKEPrivateKey::generate(suite);
    auto leaf_priv = HPKEPrivateKey::generate(suite);
    auto leaf_node = LeafNode{ suite,
                               leaf_priv.public_key,
                               identity_priv.public_key,
                               credential,
                               capas,
                               Lifetime::create_default(),
                               {},
                               identity_priv };
    auto key_package = KeyPackage{ suite, init_priv.public_key, leaf_node, {}, identity_priv };

    return std::make_tuple(init_priv, leaf_priv, identity_priv, key_package);
  }

  State MlsUserSession::make_state(const bytes& group_id, const bytes& user_id, CipherSuite suite) {
    auto [init_priv, leaf_priv, identity_priv, key_package] = make_client(user_id, suite);
    //create mls state
    return State{ group_id, suite, leaf_priv, identity_priv,key_package.leaf_node, {} };
  }