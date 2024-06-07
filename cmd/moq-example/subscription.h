#pragma once

#include <list>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <qname>
#include <transport/transport.h>

class Subscriptions
{
public:
  struct Remote
  {
    uint64_t subscribe_id {0};
    uint64_t conn_id {0};           /// Connection ID is used only for detecting if subscription is to the same connection

    bool operator==(const Remote& o) const
    {
      return subscribe_id == o.subscribe_id;
    }

    bool operator<(const Remote& o) const
    {
      return std::tie(subscribe_id) <
             std::tie(o.subscribe_id);
    }
  };

  Subscriptions();

  void get_masked_quicRName(const quicr::Name& src,
                            quicr::Name& dst,
                            const int len);

  void add(const quicr::Name& name, uint8_t len, const Remote& remote);

  void remove(const quicr::Name& name, uint8_t len, const Remote& remote);

  std::list<Remote> find(const quicr::Name& name);

private:
  std::vector<std::map<quicr::Name, std::set<Remote>>> subscriptions;
};
