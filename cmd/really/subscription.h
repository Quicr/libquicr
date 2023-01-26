#include <list>
#include <set>
#include <map>
#include <vector>
#include <optional>

#include <transport/transport.h>

class Subscriptions {
public:

  struct qName {
    uint8_t       data[16];

    bool operator==(const qName &o) const {
      return std::memcmp(data, o.data, sizeof(data));
    }

    bool operator<(const qName &o) const {
      return std::memcmp(data, o.data, sizeof(data));
    }
  };

  struct Remote {
    uint64_t subscribe_id;
    u_int64_t context_id;
    u_int64_t stream_id;

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

  void get_masked_quicRName(const qName &src, qName &dst, const int len);

  void add(const qName& name, const int len, const Remote& remote );
  
  void remove(const qName& name, const int len, const Remote& remote );
  
  std::list<Remote> find(  const qName& name  ) ;
    
 private:
  std::vector< std::map<qName,std::set<Remote>> > subscriptions;

};


