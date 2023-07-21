/*
 *  fragment_assembler.cpp
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file implements an object that will accept datagram fragments and
 *      produce fully-assembled datagrams.
 *
 *      Nested map to reassemble message fragments
 *
 *      Structure:
 *          fragments[<circular index>] = map[quicr_name] = map[offset] = data
 *
 *      Circular index is a small int value that increments from 1 to max. It
 *      wraps to 1 after reaching max size.  In this sense, it's a circular
 *      buffer. Upon moving to a new index the new index data will be purged (if
 *      any exists).
 *
 *      Fragment reassembly avoids timers and time interval based checks. It
 *      instead is based on received data. Every message quicr_name is checked
 *      to see if it's complete. If so, the published object callback will be
 *      executed. If not, it'll only update the map with the new offset value.
 *      Incomplete messages can exist in the cache for as long as the circular
 *      index hasn't wrapped to the same point in cache.  Under high
 *      load/volume, this can wrap within a minute or two.  Under very little
 *      load, this could linger for hours. This is okay considering the only
 *      harm is a little extra memory being used. Extra memory is a trade-off
 *      for being event/message driven instead of timer based with
 *      threading/locking/...
 *
 *  Portability Issues:
 *      None.
 */

#include "fragment_assembler.h"

namespace quicr {

/*
 *  FragmentAssembler::FragmentAssembler()
 *
 *  Description:
 *      Constructor for the FragmentAssembler.
 *
 *  Parameters:
 *      None.
 *
 *  Returns:
 *      Nothing.
 *
 *  Comments:
 *      None.
 */
FragmentAssembler::FragmentAssembler()
  : cindex{ 1 }
{
  // Nothing more to do
}

/*
 *  FragmentAssembler::ConsumeFragment()
 *
 *  Description:
 *      This function will accept a datagram fragment and attempt to assemble
 *      a complete datagram.
 *
 *  Parameters:
 *      datagram [in]
 *          The input datagram.
 *
 *  Returns:
 *      A byte vector containing a complete datagram if this fragment results
 *      in a complete datagram or empty vector if the complete datagram has not been
 *      received.
 *
 *  Comments:
 *      None.
 */
bytes
FragmentAssembler::ConsumeFragment(messages::PublishDatagram& datagram)
{
  bytes completed_datagram;

  // Check the current index first considering it's likely in the current buffer
  auto msg_iter = fragments[cindex].find(datagram.header.name);
  if (msg_iter != fragments[cindex].end()) {
    // Found
    msg_iter->second.emplace(datagram.header.offset_and_fin,
                             std::move(datagram.media_data));
    completed_datagram = CheckCompleteDatagram(msg_iter->second);
    if (!completed_datagram.empty()) fragments[cindex].erase(msg_iter);
  } else {
    // Not in current buffer, search all buffers
    bool found = false;
    for (auto& buf : fragments) {
      const auto& msg_iter = buf.second.find(datagram.header.name);
      if (msg_iter != buf.second.end()) {
        // Found
        msg_iter->second.emplace(datagram.header.offset_and_fin,
                                 std::move(datagram.media_data));
        completed_datagram = CheckCompleteDatagram(msg_iter->second);
        if (!completed_datagram.empty()) buf.second.erase(msg_iter);
        found = true;
        break;
      }
    }

    if (!found) {
      // If not found in any buffer, then add to current buffer
      fragments[cindex][datagram.header.name].emplace(
        datagram.header.offset_and_fin, std::move(datagram.media_data));
    }
  }

  // Move to next buffer if reached max
  if (fragments[cindex].size() >= Max_Fragment_Names_Pending_Per_Buffer) {
    if (cindex < Max_Fragment_Buffers)
      ++cindex;
    else
      cindex = 1;

    fragments.erase(cindex);
  }

  return completed_datagram;
}

/*
 *  FragmentAssembler::CheckCompleteDatagram()
 *
 *  Description:
 *      This function will check the given map entry for a completed datagram.
 *
 *  Parameters:
 *      frag_map [in]
 *          The map entry to datagram fragments that might be a completed
 *          datagrams.
 *
 *  Returns:
 *      A vector that will contain the completed datagram or be empty if it is
 *      not complete.
 *
 *  Comments:
 *      None.
 */
bytes
FragmentAssembler::CheckCompleteDatagram(
  const std::map<std::size_t, bytes>& frag_map)
{
  // Just return if the final fragment has not been received
  if ((frag_map.rbegin()->first & 0x1) != 0x1) return {};

  bytes reassembled;

  std::size_t seq_bytes = 0;
  for (const auto& item : frag_map) {
    // If there is a gap in offsets (missing data); return nullptr
    if ((item.first >> 1) - seq_bytes != 0) return {};

    reassembled.insert(
      reassembled.end(), item.second.begin(), item.second.end());
    seq_bytes += item.second.size();
  }

  return reassembled;
}

} // namespace quicr
