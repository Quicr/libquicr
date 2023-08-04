/*
 *  MODULE_NAME
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      Defines some types used by H3 client and session code.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <utility>
#include <memory>
#include <stdexcept>
#include "transport/transport.h"

namespace quicr::h3 {

// Define a type that encapsulates the transport context and stream ID
using StreamContext =
  std::pair<qtransport::TransportContextId, qtransport::StreamId>;

// Define a shared pointer type for the transport
using TransportPointer = std::shared_ptr<qtransport::ITransport>;

// Define an exception class that serves as a base for session/client exceptions
class QuicRH3Exception : public std::runtime_error
{
  using std::runtime_error::runtime_error;
};

} // namespace quicr::h3
