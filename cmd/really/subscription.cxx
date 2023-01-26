#include <cassert>
#include <iostream>

#include "subscription.h"


Subscriptions::Subscriptions() {
  subscriptions.resize(129 /* 128 + 1 to handle zero and 128 */);
}

void Subscriptions::add(const qName& name, const int len, const Remote& remote ) {
  qName group;

  bzero(group.data, sizeof(group.data));
  get_masked_quicRName(name, group, len);

  auto mapPtr =  subscriptions[len].find(group );
  if ( mapPtr == subscriptions[len].end() ) {
    std::set<Remote> list;
    list.insert( remote );
    std::pair<qName,std::set<Remote>> pair;
    pair = make_pair( group , list );
    subscriptions[len].insert(pair );
  }
  else {
    std::set<Remote>& list = mapPtr->second;
    if ( list.find( remote ) == list.end() ) {
      list.insert( remote ); // TODO - rethink if list is right thing here 
    }
  }
}
  
void Subscriptions::remove(const qName& name, const int len, const Remote& remote ) {
  qName group;
  get_masked_quicRName(name, group, len);

  auto mapPtr = subscriptions[len].find(group );
  if ( mapPtr != subscriptions[len].end() ) {
    std::set<Remote>& list = mapPtr->second;
    if ( list.find( remote ) == list.end() ) {
      list.erase( remote ); 
    }
  }
}
  
std::list<Subscriptions::Remote> Subscriptions::find(  const qName& name  ) {
  std::list<Remote> ret;
  qName group;

  // TODO: Fix this to not have to iterate for each mask bit
  for ( int len=0; len <= 128 ; len++ ) {
    get_masked_quicRName(name, group, len);

//    std::clog << getMsgShortNameHexString(group.data) << std::endl;

    auto mapPtr = subscriptions[len].find( group );
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

void Subscriptions::get_masked_quicRName(const qName &src, qName &dst, const int len) {

  // Set the data len to the size of bytes to keep at 8 bit boundaries.  The last byte is a wildcard
  //   that will be added back so that it can be masked.
  u_char dst_len = len > 0 ? (len / 8) : 0;
  u_char dst_bits = len % 8;

  bzero(&dst, sizeof(dst.data));

  if (dst_len == 0) {
    return;
  }

  std::memcpy(dst.data, src.data, dst_len);   // Copy all bytes to keep as-is on 8bit boundaries

  // Handle the last byte
  if (dst_bits) {
    dst.data[dst_len] = src.data[dst_len] >> (8 - dst_bits) << (8 - dst_bits);
  }

