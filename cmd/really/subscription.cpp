#include "subscription.h"

Subscriptions::Subscriptions()
{
  subscriptions.resize(129 /* 128 + 1 to handle zero and 128 */);
}

void
Subscriptions::add(const quicr::Name& name, uint8_t len, const Remote& remote)
{

  const auto prefix = quicr::Namespace{ name, len };

  auto mapPtr = subscriptions[len].find(prefix.name());
  if (mapPtr == subscriptions[len].end()) {
    std::set<Remote> list;
    list.insert(remote);
    std::pair<quicr::Name, std::set<Remote>> pair;
    pair = make_pair(prefix.name(), list);
    subscriptions[len].insert(pair);
  } else {
    auto& list = mapPtr->second;
    if (list.find(remote) == list.end()) {
      list.insert(
        remote); // TODO(trigaux) - rethink if list is right thing here
    }
  }
}

void
Subscriptions::remove(const quicr::Name& name,
                      uint8_t len,
                      const Remote& remote)
{
  const auto prefix = quicr::Namespace{ name, len };

  auto mapPtr = subscriptions[len].find(prefix.name());
  if (mapPtr != subscriptions[len].end()) {
    auto& list = mapPtr->second;
    if (list.find(remote) == list.end()) {
      list.erase(remote);
    }
  }
}

std::list<Subscriptions::Remote>
Subscriptions::find(const quicr::Name& name)
{
  std::list<Remote> all_remotes;

  // TODO(trigaux): Fix this to not have to iterate for each mask bit
  for (uint8_t len = 0; len <= 128; len++) {
    const auto prefix = quicr::Namespace{ name, len };

    auto mapPtr = subscriptions[len].find(prefix.name());
    if (mapPtr != subscriptions[len].end()) {
      const auto& list = mapPtr->second;
      all_remotes.insert(all_remotes.end(), list.begin(), list.end());
    }
  }

  return all_remotes;
}
