// Fake transport delegate

#pragma once

#include "transport/transport.h"

class FakeTransportDelegate : public qtransport::ITransport::TransportDelegate
{
    public:
        FakeTransportDelegate() = default;
        virtual ~FakeTransportDelegate() = default;

        virtual void on_connection_status(
                            const qtransport::TransportContextId &context_id,
                            const qtransport::TransportStatus status)
        {
        }
        virtual void on_new_connection(
                            const qtransport::TransportContextId &context_id,
                            const qtransport::TransportRemote &remote)
        {
        }
        virtual void onNewMediaStream(
                            const qtransport::TransportContextId &context_id,
                            const qtransport::MediaStreamId &mStreamId)
        {
        }
        virtual void on_recv_notify(
                            const qtransport::TransportContextId &context_id)
        {
        }
};
