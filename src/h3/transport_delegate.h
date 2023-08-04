/*
 *  h3_transport_delegate.h
 *
 *  Copyright (C) 2023
 *  Cisco Systems, Inc.
 *  All Rights Reserved
 *
 *  Description:
 *      This defines a transport delegate for use with the H3/Quiche module.
 *      The delegate is expected to have a lifetime at least as long as
 *      the owning object for which the server pointer is provided in the
 *      constructor.  Since Quiche handles everything related to establishment
 *      of new connections, closure, etc., this interface is minimal.  The
 *      most important function is the notification of new messages.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include "transport/transport.h"

namespace quicr {

template<typename T>
class TransportDelegate : public qtransport::ITransport::TransportDelegate
{
public:
  TransportDelegate(T* target)
    : target{ target } {};

  void on_connection_status(const qtransport::TransportContextId&,
                            const qtransport::TransportStatus) override
  {
    // Nothing to do
  }
  void on_new_connection(const qtransport::TransportContextId&,
                         const qtransport::TransportRemote&) override
  {
    // Nothing to do
  }
  void on_new_stream(const qtransport::TransportContextId&,
                     const qtransport::StreamId&) override
  {
    // Nothing to do
  }
  void on_recv_notify(const qtransport::TransportContextId& context_id,
                      const qtransport::StreamId& stream_id) override
  {
    target->IncomingPacketNotification(context_id, stream_id);
  }

private:
  T* target;
};

} // namespace quicr
