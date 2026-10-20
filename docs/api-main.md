# Media over QUIC (MoQ) Publisher/Subscriber API

LibQuicR provides client and server APIs for publishing and subscribing to
Media over QUIC tracks. \ref quicr::SessionManager owns transports and their
\ref quicr::Session objects. Configure a client with \ref quicr::ClientConfig
or a server with \ref quicr::ServerConfig, then provide the corresponding
session callback implementation.

## Sessions

Class | Description
------|------------
\ref quicr::SessionManager | Creates transports, owns sessions, and registers track and namespace handlers.
\ref quicr::Session | Represents one MoQ protocol session and exposes its current status and operations.
\ref quicr::Session::ClientCallbacks | Receives setup and protocol callbacks for a client session.
\ref quicr::Session::ServerCallbacks | Receives setup and protocol callbacks for an accepted server session.

## Track handlers

Create a handler for one \ref quicr::FullTrackName and register it with
\ref quicr::SessionManager::AddHandler. Override the handler callbacks needed
by the application.

Class | Description
------|------------
\ref quicr::SubscribeTrackHandler | Receives objects and subscription status for a subscribed track.
\ref quicr::PublishTrackHandler | Publishes objects and reports publication status for a track.

See the [API Guide](api-guide.html) for more details and the
[Examples](examples.html) for complete applications.

@example simple_publish/main.cpp
@example simple_subscribe/main.cpp
