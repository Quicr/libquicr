# Media over QUIC (MoQ) Publisher/Subscriber APIs

LibQuicR provides native C++ and C APIs for client and server sessions,
publishing, subscribing, fetches, namespace operations, and track-status
queries.

## C++ API

\ref quicr::SessionManager owns transports and their \ref quicr::Session
objects. Configure a client with \ref quicr::ClientConfig or a server with
\ref quicr::ServerConfig, provide the corresponding session callbacks, and
register track or namespace handlers through
\ref quicr::SessionManager::AddHandler.

\ref quicr::SubscribeTrackHandler receives objects and subscription status.
\ref quicr::PublishTrackHandler publishes objects and reports publication
status.

See the [C++ API Guide](api-guide.html) and
[Examples](examples.html) for details.

## C API

The native C API is declared in `<quicr/quicr.h>` and linked through the CMake
target `quicr::c`. It provides opaque manager, session, name, extension, and
handler types with callback tables and explicit ownership operations.

See the [C API Guide](c-api-guide.html) for ownership, callback, reply, handler
lifecycle, and shutdown requirements.

@example simple_publish/main.cpp
@example simple_subscribe/main.cpp
@example simple_publish/main.c
@example simple_subscribe/main.c
