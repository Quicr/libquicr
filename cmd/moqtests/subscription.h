#include <list>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include <qname>
#include <transport/transport.h>
#include "quicr/moq_message_types.h"

class Subscriptions
{
public:
  struct SubscriberInfo
  {
    uint64_t subscribe_id;
    uint64_t conn_id {0};           /// Connection ID is used only for detecting if subscription is to the same connection
    uint64_t stream_id;
    quicr::messages::TrackAlias track_id;

      bool operator==(const SubscriberInfo& o) const
      {
          return subscribe_id == o.subscribe_id;
      }

      bool operator<(const SubscriberInfo& o) const
      {
          return std::tie(subscribe_id) <
                 std::tie(o.subscribe_id);
      }
  };

  Subscriptions();

  void get_masked_quicRName(const quicr::Name& src,
                            quicr::Name& dst,
                            const int len);

  void add(const quicr::Name& name, uint8_t len, const SubscriberInfo& remote);

  void remove(const quicr::Name& name, uint8_t len, const SubscriberInfo& remote);

  std::list<SubscriberInfo> find(const quicr::Name& name);

private:
  // [ {name, subscriber_info}, ... ]
  std::vector<std::map<quicr::Name, std::set<SubscriberInfo>>> subscriptions;
};
