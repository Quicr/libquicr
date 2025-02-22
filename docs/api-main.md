# Media over QUIC (MoQ) Publisher/Subscriber API

This library provides a publisher and subscriber API using MoQ. It provides
a class for [Client](#quicr::Client) and [Server](#quicr::Server) that
will handle all MoQ protocol state machine functions. Both classes contain virtual methods (e.g., callbacks)
that **SHOULD** be implemented.

Subscriptions and publications are handled via [Subscribe Track Handler](#quicr::SubscribeTrackHandler)
and [Publish Track Handler](#quicr::PublishTrackHandler). Both classes contain virtual methods
(e.g., callbacks) that **SHOULD** be implemented.

## Client

 Class                        | Description
------------------------------|-----------------------------------------------------------
 quicr::Client       | Client handler, which is specific to a QUIC IP connection
 quicr::ClientConfig | Client configuration

## Server

 Class                        | Description
------------------------------|------------------------------------------------------------------------
 quicr::Server       | Server handler, which is specific to the QUIC IP listening IP and port
 quicr::ServerConfig | Server configuration

## Track Handlers

Both client and server provide quicr::Transport::PublishTrack() and quicr::Transport::SubscribeTrack()
methods to start a new subscription and/or publication. Use the below handler classes when calling
the methods. Each track handler is constructed for a single full track name (e.g., namespace and name).

 Class                          | Description
--------------------------------|------------------------------------------------------------------------
 quicr::SubscribeTrackHandler     | Subscribe track handler for subscribe related operations and callbacks
 quicr::PublishTrackHandler       | Publish track handler for publish related operations and callbacks
 quicr::FetchTrackHandler         | Fetch track handler for fetch realted operations and callbacks


See [API Guide](api-guide.html) for more details on the API. See [Examples](examples.html) for detailed example of how to get started
using the APIs. 

@example server.cpp
@example client.cpp
