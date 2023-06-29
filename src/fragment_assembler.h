/*
 *  fragment_assembler.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved.
 *
 *  Description:
 *      This file defines an object that will accept datagram fragments and
 *      produce fully-assembled datagrams.
 *
 *      This object is not thread-safe.  The caller must ensure serialized
 *      calls to member functions.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <vector>
#include <map>
#include <cstdint>
#include "quicr/encode.h"
#include "quicr/name.h"
#include "quicr/quicr_common.h"

namespace quicr {

class FragmentAssembler {
protected:
    static constexpr std::size_t Max_Fragment_Names_Pending_Per_Buffer{5000};
    static constexpr std::size_t Max_Fragment_Buffers{20};

public:
    FragmentAssembler();
    ~FragmentAssembler() = default;

    bytes ConsumeFragment(messages::PublishDatagram& datagram);

  protected:
    bytes CheckCompleteDatagram(
      const std::map<std::size_t, bytes>& frag_map);

    std::size_t cindex;
    std::map<std::size_t, std::map<quicr::Name, std::map<std::size_t, bytes>>>
      fragments;
};

} // namespace quicr
