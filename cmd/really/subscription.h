#include <list>
#include <set>
#include <map>
#include <vector>
#include <optional>

#include <transport/transport.h>
#include <qname>

class Subscriptions {
public:

  struct Remote {
    uint64_t subscribe_id;
    uint64_t context_id;
    uint64_t stream_id;

    bool operator==(const Remote &o) const {
      return subscribe_id == o.subscribe_id
             && context_id == o.context_id
             && stream_id == o.stream_id;
    }

    bool operator<(const Remote &o) const {
      return std::tie(subscribe_id, context_id, stream_id)
              < std::tie(o.subscribe_id, o.context_id, o.stream_id);
    }
  };

  Subscriptions();

  void get_masked_quicRName(const quicr::Name &src, quicr::Name &dst, const int len);

  void add(const quicr::Name& name, uint8_t len, const Remote& remote );

  void remove(const quicr::Name& name, uint8_t len, const Remote& remote );

  std::list<Remote> find(  const quicr::Name& name  ) ;

 private:
  std::vector< std::map<quicr::Name,std::set<Remote>> > subscriptions;

};


