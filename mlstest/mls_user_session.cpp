#include "mls_user_session.h"
#include <iostream>

MLsUserInfo MlsUserSession::setup_mls_userinfo(const std::string& user, const std::string& group, CipherSuite suite) {
  MLsUserInfo info;
  auto ext_list = ExtensionList{};
  auto capas = Capabilities::create_default();
  info.suite = suite;
  info.user = user;
  info.group = group;
  info.signing_key = SignaturePrivateKey::generate(suite);
  info.credential = Credential::basic(from_ascii(user));
  info.init_key =  HPKEPrivateKey::generate(suite);
  info.leaf_key = HPKEPrivateKey::generate(suite);
  auto leaf_node = LeafNode{ suite,
                             info.leaf_key.public_key,
                             info.signing_key.public_key,
                             info.credential,
                             capas,
                             Lifetime::create_default(),
                             {},
                             info.signing_key };

  info.keypackage = KeyPackage{ suite, info.init_key.public_key, leaf_node, {}, info.signing_key };
  return info;
}

// setup mls state for the creator
std::unique_ptr<MlsUserSession> MlsUserSession::create(const MLsUserInfo& info) {
  // create underlying mls state for the creator
  auto mls_state = State{ from_ascii(info.group), info.suite, info.leaf_key,
                          info.signing_key, info.keypackage.leaf_node, {}};
  auto keypackage = info.keypackage;
  return std::make_unique<MlsUserSession>(std::move(mls_state), info);
}


// setup mls state for the creator
std::unique_ptr<MlsUserSession> MlsUserSession::create_for_welcome(const MLsUserInfo& info, bytes&& welcome_data) {

  auto welcome = tls::get<mls::Welcome>(welcome_data);

  try {
    auto mls_state = State{ info.init_key, info.leaf_key, info.signing_key, info.keypackage,
                            welcome,  std::nullopt, {} };
    auto gid = mls_state.group_id();
    auto keypackage = info.keypackage;
    return std::make_unique<MlsUserSession>(std::move(mls_state), info);
  } catch(const std::exception &  ex) {
    std::cout << "Exception while building state" << std::endl;
    throw ex;
  }
}


// Add user to a group as creator
MlsUserSession::MlsUserSession(mls::State&& state, const MLsUserInfo& info)
  :  user_info(info),
  mls_state(state)
{}

const KeyPackage& MlsUserSession::get_key_package() const {
  return user_info.keypackage;
}


bytes MlsUserSession::fresh_secret() const { return random_bytes(user_info.suite.secret_size()); }

mls::State& MlsUserSession::get_state()
{
  return mls_state;
}

std::tuple<bytes, bytes> MlsUserSession::process_key_package(std::vector<uint8_t>&& data)
{
  auto kp = tls::get<KeyPackage>(data);
  auto add_proposal = mls_state.add_proposal(kp);
  auto [commit, welcome, _creator_state] = mls_state.commit(fresh_secret(), CommitOpts{ { add_proposal }, true, false, {} }, {});
  auto commit_data =  tls::marshal(commit);
  auto welcome_data = tls::marshal(welcome);
  // TODO: should we wait for commit to come back to us before overwriting the state ?
  mls_state = _creator_state;
  return std::make_tuple(welcome_data, commit_data);
}
