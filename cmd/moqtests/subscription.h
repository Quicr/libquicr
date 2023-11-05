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
    uint64_t context_id;
    uint64_t stream_id;
    quicr::messages::TrackId track_id;

    bool operator==(const SubscriberInfo& o) const
    {
      return subscribe_id == o.subscribe_id && context_id == o.context_id &&
             stream_id == o.stream_id;
    }

    bool operator<(const SubscriberInfo& o) const
    {
      return std::tie(subscribe_id, context_id, stream_id) <
             std::tie(o.subscribe_id, o.context_id, o.stream_id);
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
