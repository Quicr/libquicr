#include <cassert>
#include <iostream>

#include "subscription.h"


Subscriptions::Subscriptions() {
  subscriptions.resize(129 /* 128 + 1 to handle zero and 128 */);
}

void Subscriptions::add(const quicr::Name& name, const int len, const Remote& remote ) {

  quicr::Namespace prefix (name, len);

  auto mapPtr =  subscriptions[len].find(prefix.name());
  if ( mapPtr == subscriptions[len].end() ) {
    std::set<Remote> list;
    list.insert( remote );
    std::pair<quicr::Name,std::set<Remote>> pair;
    pair = make_pair( prefix.name() , list );
    subscriptions[len].insert(pair );
  }
  else {
    std::set<Remote>& list = mapPtr->second;
    if ( list.find( remote ) == list.end() ) {
      list.insert( remote ); // TODO - rethink if list is right thing here 
    }
  }
}
  
void Subscriptions::remove(const quicr::Name& name, const int len, const Remote& remote ) {
  quicr::Namespace prefix (name, len);

  auto mapPtr = subscriptions[len].find(prefix.name() );
  if ( mapPtr != subscriptions[len].end() ) {
    std::set<Remote>& list = mapPtr->second;
    if ( list.find( remote ) == list.end() ) {
      list.erase( remote ); 
    }
  }
}
  
std::list<Subscriptions::Remote> Subscriptions::find(  const quicr::Name& name  ) {
  std::list<Remote> ret;


  // TODO: Fix this to not have to iterate for each mask bit
  for ( int len=0; len <= 128 ; len++ ) {
    quicr::Namespace prefix (name, len);

    auto mapPtr = subscriptions[len].find( prefix.name() );
    if ( mapPtr != subscriptions[len].end() ) {
      std::set<Remote>& list = mapPtr->second;
      for( const Remote& remote : list ) {
        Remote dest = remote;
        ret.push_back( dest );
      }
    }
  }

  return ret;
}
