---
title: QuicR C API Guide
date: 2026-NOV-13
---

# API overview

The native libquicr C API is declared by `<quicr/quicr.h>`. It is a direct C
interface to the same sessions and handlers as the C++ API and is distinct from
the legacy c-bridge. The current wire implementation uses the `moqt-18` ALPN
and draft-18 message encodings.

With CMake, link the C target:

```cmake
target_link_libraries(my_application PRIVATE quicr::c)
```

The API has the same main layers as the C++ API:

- `quicr_session_manager_t` owns transports and active sessions.
- `quicr_session_t` represents one MOQT connection in client or server mode.
- Handlers represent long-lived track, namespace, fetch, and status requests.
- Callback tables deliver events and answer peer requests.

C11-or-later C translation units also receive type-generic handler operations
implemented with `_Generic`. The corresponding typed functions are available
in other language modes.

Most fallible functions return `quicr_status_t`. `QUICR_STATUS_OK` means the
API call itself succeeded. Asynchronous handler status and
`quicr_publish_object_status_t` report protocol outcomes separately.

## Names, aliases, and objects

### Full track names

A `quicr_track_namespace_t` is an ordered tuple of binary entries. A
`quicr_full_track_name_t` combines a copied namespace with a binary track name:

```c
quicr_byte_span_t entries[] = {
    { (const uint8_t *)"conference", 10 },
    { (const uint8_t *)"1234", 4 },
};
quicr_track_namespace_t *ns = NULL;
quicr_full_track_name_t *ftn = NULL;

quicr_track_namespace_create(entries, 2, &ns);
quicr_full_track_name_create(
    ns, (quicr_byte_span_t){ (const uint8_t *)"video", 5 }, &ftn);
```

The namespace and full track name are independent after creation. Entry and
track-name accessors return borrowed spans valid until their owning object is
destroyed. Destroy each owned object with its matching destroy function.

### Track aliases

Track aliases are 64-bit, session-scoped identifiers used on the wire. The
library derives aliases for locally created handlers. The C API does not
currently expose the C++ handler methods that override proposed aliases.
Inbound publish attributes expose the peer's alias through
`quicr_publish_attributes_track_alias()`.

### Objects and delivery modes

`quicr_object_headers_t` describes an object using group, subgroup, and object
identifiers plus payload length, status, priority, TTL, delivery mode, and
extensions. Initialize it before setting fields:

```c
quicr_object_headers_t headers;
quicr_object_headers_init(&headers);
headers.group_id = 7;
headers.object_id = 12;
headers.payload_length = payload.size;
headers.status = QUICR_OBJECT_STATUS_AVAILABLE;
```

`QUICR_TRACK_MODE_DATAGRAM` sends one object in a QUIC DATAGRAM.
`QUICR_TRACK_MODE_STREAM` sends objects on subgroup streams. Datagram objects
must fit in one negotiated datagram after MOQT and QUIC framing; use stream
mode for larger objects or reliable delivery.

`quicr_object_headers_t` points to borrowed `quicr_extensions_t` views. Publish
calls copy their nested bytes before returning. Received views remain valid
only for the callback.

`quicr_track_extensions_t` is an owned collection. Create it, add or replace
mutable and immutable entries, inspect entries, then destroy it. Values are
copied on insertion. Entry spans remain valid until mutation or destruction.

## Ownership and threading

`quicr_session_manager_t` owns transports, sessions, session handles, and
handler registrations. A `quicr_session_t *` is borrowed from its manager and
must never be freed. A client session remains valid from a successful
`quicr_session_manager_add_client()` until `session_removed` or manager
destruction. A server session is valid from `new_server_session` through
`session_removed`; the handle passed to `session_removed` becomes invalid when
that callback returns.

Unlike C++ shared pointers, C handler objects remain application-owned while
registered. Every handler follows the same lifecycle:

1. Create a detached handler.
2. Add it to one live session.
3. Use it and receive callbacks.
4. Remove it from the session.
5. Destroy it.

C11 callers can use `quicr_session_manager_add_handler()`,
`quicr_session_manager_remove_handler()`, `quicr_track_handler_get_status()`,
and `quicr_track_handler_destroy()`. Typed functions provide the same
operations for each handler family.

Registration does not transfer ownership. Destroying a registered handler
returns `QUICR_STATUS_BUSY`. Removal prevents new callbacks, but a callback
already in flight may still make destruction return `BUSY`; return from the
callback and retry. A NULL handler can be destroyed successfully.

Session and handler callbacks may run synchronously on an API caller thread,
including while session locks are held, or on a transport notification thread.
Callbacks can run concurrently across objects or transports. Return promptly
and synchronize shared application state. Do not re-enter operations on the
same manager, session, or handler unless a specific API permits it.

Unless a declaration says otherwise, input bytes and strings are copied before
the call returns. `user_data` is only passed through; keep it valid for the
callback table's documented lifetime.

## Session callbacks and replies

All callback fields are optional. Callback pointer arguments, byte spans,
attributes, headers, properties, and metrics are borrowed only until return.
Copy anything needed later. Never destroy a callback-owning object from its
callback.

Set the callback table's `user_data` field to recover application state.
Session callback data must remain valid until `session_removed` or manager
destruction. Handler callback data must remain valid until successful handler
destruction.

Setup, publish-namespace, and fetch attributes expose documented fields
directly. Publish and subscribe attributes are callback-scoped opaque objects;
inspect them with `quicr_publish_attributes_*()` and
`quicr_subscribe_attributes_*()`. Returned token values, full track names, and
track extensions remain borrowed for that callback only.

Callbacks that answer peer requests return tagged reply structures. Initialize
the whole reply and write only the active member:

```c
return (quicr_subscribe_reply_t){
    .result = QUICR_REPLY_RESULT_REJECT,
    .error = {
        .code = QUICR_REQUEST_ERROR_UNAUTHORIZED,
        .reason = "not permitted",
    },
};
```

On `ACCEPT`, use `response` for subscribe, fetch, track-status, and
track-namespaces replies. On `REJECT`, use `error`. Response-less replies use
`error` only when rejecting. Reply strings, nested extensions, and namespace
lists are borrowed and copied before the callback returns.

An accepted `publish_received` reply is special. Create its response from a
subscribe handler with `quicr_publish_response_create()`, optionally configure
priority, order, filter, forwarding, timeouts, or new-group request ID, and
return its pointer as the active `response` member. The library consumes it
synchronously but does not take ownership. Destroy it after the callback
returns.

C reply callbacks are synchronous. The C API does not expose the C++
`Reply::Defer()` facility, so applications must return a reply before the
callback completes.

## Creating a client session

Initialize configuration, create a manager, and add a client:

```c
quicr_session_manager_t *manager = NULL;
quicr_session_t *session = NULL;

quicr_session_manager_callbacks_t manager_callbacks = { 0 };
quicr_session_manager_create(&manager_callbacks, &manager);

quicr_client_config_t config;
quicr_client_config_init(&config);
config.endpoint_id = "camera-1";
config.connect_uri = "moqt://relay.example:33435";

quicr_session_client_callbacks_t callbacks = { 0 };
quicr_session_manager_add_client(
    manager, &config, &callbacks, &session);
```

The initializers overwrite the whole structure and initialize nested transport
settings. Configuration strings are nullable and borrowed during the add call;
the implementation copies or consumes them before returning. Callback tables
are also copied.

`moq://` and `moqt://` select native QUIC. An `https://` URI selects
WebTransport. Wait for `QUICR_SESSION_STATUS_READY` before starting track
operations. Prefer `status_changed`; polling `quicr_session_get_status()` is
also supported.

## Subscribing to a track

`quicr_filter_t` is a tagged union. Set `type` and only its matching member:

- `QUICR_FILTER_TYPE_NONE` has no active union member.
- `QUICR_FILTER_TYPE_SUBSCRIPTION_FILTER` uses `subscription`, initialized by
  `quicr_subscription_filter_init()`.
- `QUICR_FILTER_TYPE_TRACK_FILTER` uses `track`, initialized by
  `quicr_track_filter_init()`.

Handler creation copies the filter. Subscription filters select next-group,
largest-object, absolute-start, or absolute-range delivery. Track filters carry
a property type, track limit, and timeout.

Supply only the callbacks the application needs. Received data is borrowed:

```c
static void object_received(const quicr_object_headers_t *headers,
                            quicr_byte_span_t data,
                            const quicr_stream_header_properties_t *properties,
                            void *user_data)
{
    (void)headers;
    (void)properties;
    consume_or_copy(data, user_data);
}

quicr_subscribe_track_handler_callbacks_t callbacks = {
    .user_data = app,
    .object_received = object_received,
};
quicr_subscribe_track_handler_t *subscriber = NULL;
quicr_subscribe_track_handler_create(
    &callbacks, ftn, 127, QUICR_GROUP_ORDER_ASCENDING, NULL, &subscriber);
quicr_session_manager_add_subscribe_track_handler(
    manager, session, subscriber);
```

The callback table can also receive lifecycle status, metrics, status-only
objects, subgroup endings, and raw datagrams. A forwarding subscribe handler
adds subgroup-start and encoded-stream-byte callbacks. The C API does not
currently expose the C++ partial-object callback.

Registered subscribe handlers can be paused, resumed, updated, and used for
new-group requests. Remove the handler before destroying it.

## Publishing a track

Create and register a publish handler, then wait until it can publish:

```c
quicr_publish_track_handler_t *publisher = NULL;
quicr_publish_track_handler_create(
    NULL, ftn, QUICR_TRACK_MODE_STREAM, 127, 127, 5000, NULL, &publisher);
quicr_session_manager_add_publish_track_handler(
    manager, session, publisher);

while (!quicr_publish_track_handler_can_publish(publisher)) {
    /* Wait or react to status_changed; also stop on terminal status. */
}
```

Use `quicr_publish_track_handler_can_publish()` rather than testing only for
handler status `OK`. Inspect the API status and publish outcome separately:

```c
quicr_publish_object_status_t outcome;
quicr_status_t call = quicr_publish_track_handler_publish_object(
    publisher, &headers, payload, NULL, &outcome);
if (call != QUICR_STATUS_OK ||
    outcome != QUICR_PUBLISH_OBJECT_STATUS_OK) {
    /* Handle API or publication failure. */
}
```

The stream-header properties argument is optional. For stream mode, finish open
subgroups with `quicr_publish_track_handler_end_subgroup()` or
`quicr_publish_track_handler_end_all_subgroups()` before removal when
completion matters.

## Fetch

A standalone fetch uses `quicr_fetch_track_handler_create()` with an inclusive
start and end range. Fetch handlers receive objects through the same complete
object, status-only object, subgroup, datagram, and metrics callbacks as
subscribe handlers. Register, remove, and destroy them through the common
handler lifecycle.

Fetch handlers can be paused, resumed, and used for new-group requests. The
server session callbacks receive standalone fetches, joining fetches, and fetch
cancellations and return synchronous accept-or-reject replies.

The C API does not currently expose the C++ `PublishFetchHandler` and
`BindFetchTrack()` serving path. Applications that require that path must use
the C++ API until a C equivalent is added.

## Namespace operations

Create a `quicr_publish_namespace_handler_t` to publish a namespace. It can
associate application-owned publish-track handlers through
`quicr_publish_namespace_handler_publish_track()`. The association does not
transfer C object ownership; call the matching `unpublish_track()` before
destroying either object.

Namespace subscriptions use `quicr_subscribe_namespace_handler_t`:

- `QUICR_SUBSCRIBE_NAMESPACE_MODE_NAMESPACES` receives matching namespace
  announcements.
- `QUICR_SUBSCRIBE_NAMESPACE_MODE_TRACKS` receives matching track
  publications.

The callbacks report matching namespaces becoming available or ending.
Register, remove, and destroy namespace handlers through the common lifecycle.

Server callbacks answer inbound namespace publications, namespace-prefix
subscriptions, and track-prefix subscriptions. Accepted namespace-list replies
are copied before the callback returns.

## Track status

Create a `quicr_track_status_track_handler_t` for a full track name, register
it, and wait for its status callback. Read the optional result with
`quicr_track_status_handler_get_response()`. The caller may provide owned
`quicr_track_extensions_t` storage when copied response extensions are needed.
Protocol rejection details are available through
`quicr_track_status_handler_get_error()`.

Receiving peers answer `track_status_received` with a
`quicr_track_status_reply_t`. This handler-based API reflects the current
implementation; the C++ guide's older request-ID description should be updated
to match the current C++ `TrackStatusHandler` API.

## Creating a server

Initialize a server configuration, provide manager and session callback tables,
and add the server transport:

```c
quicr_server_config_t config;
quicr_server_config_init(&config);
config.endpoint_id = "relay-1";
config.bind_ip = "::";
config.port = 33435;
config.transport_config.tls_cert_filename = certificate_path;
config.transport_config.tls_key_filename = private_key_path;

quicr_session_manager_callbacks_t manager_callbacks = {
    .user_data = app,
    .new_server_session = new_server_session,
    .session_removed = session_removed,
};
quicr_session_server_callbacks_t session_callbacks = { 0 };
quicr_session_manager_t *manager = NULL;

quicr_session_manager_create(&manager_callbacks, &manager);
quicr_session_manager_add_server(
    manager, &config, &session_callbacks);
```

Accepted sessions arrive through `new_server_session`. Server callbacks answer
incoming SETUP, SUBSCRIBE, PUBLISH, namespace, fetch, track-status, and group
requests. The same callback table may serve several sessions concurrently, so
synchronize application state.

Certificate and key material remain external files. Validate deployed
certificates for validity dates, key strength, signature algorithm,
hostname/SAN, and the intended trust model.

## Metrics and errors

Publish, subscribe, and fetch metrics arrive through their handler callback
tables. The C API currently exposes per-handler metrics but not the C++ session
connection-metrics callback. Copy samples that must outlive the callback.

Check every fallible call. `INVALID_ARGUMENT` covers bad pointers, values, or
object state. `NOT_FOUND` is returned only where documented for missing results
or live state. `BUSY` means a handler cannot yet be destroyed. Allocation
failures and unexpected C++ exceptions map to `OUT_OF_MEMORY` and
`INTERNAL_ERROR`.

Each publish-namespace, subscribe-namespace, and track-status `get_error()`
function copies an error code and optional NUL-terminated reason. Query or
retry the reason buffer size like this:

```c
quicr_error_code_t code;
size_t size = 0;
quicr_status_t status =
    quicr_subscribe_namespace_handler_get_error(handler, &code, NULL, &size);
if (status == QUICR_STATUS_OK && size != 0) {
    char *reason = malloc(size);
    status = quicr_subscribe_namespace_handler_get_error(
        handler, &code, reason, &size);
    /* BUFFER_TOO_SMALL reports the new required size in size. */
    free(reason);
}
```

When no protocol error is stored, these getters return
`QUICR_STATUS_NOT_FOUND`. When a reason buffer is supplied, `reason_size` is
both input capacity and output required size, including the terminating NUL.

## Shutdown

`quicr_session_goaway()` sends GOAWAY with an optional copied replacement URI,
a drain timeout, and an optional first-unprocessed request ID. For a cutoff
derived from a registered handler, C11 code can call
`quicr_session_goaway_handler()`. Other language modes use the corresponding
typed handler function. GOAWAY drains or redirects work;
`quicr_session_disconnect()` explicitly closes the connection.

Order cleanup so no borrowed object outlives its owner:

1. Finish stream subgroups and protocol work when needed.
2. Remove every registered handler from its session.
3. Destroy handlers, retrying after in-flight callbacks when the result is
   `QUICR_STATUS_BUSY`.
4. Destroy publish responses, extension collections, full track names, and
   namespaces owned by the application.
5. Disconnect sessions when an explicit close is required.
6. Destroy the manager last.

Manager destruction shuts down its transports and invalidates all session
handles, but it does not destroy application-owned handler C objects. Keep
callback `user_data` alive until the corresponding final lifetime point.

Complete client examples are in `examples/simple_publish/main.c` and
`examples/simple_subscribe/main.c`.
