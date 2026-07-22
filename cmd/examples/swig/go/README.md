# libquicr SWIG Go Example

`qgoclient` is a minimal smoke test for the SWIG-generated Go bindings of
libquicr (generated from [`swig/quicr.i`](../../../../swig/quicr.i)). It
builds a `TrackNamespace`, compares namespaces using the renamed comparison
operators, constructs a `ClientConfig`, creates a `SessionManager`, and
calls the hand-written `AddClientTransport()` Go helper to get back a
`(Transport, Session)` pair as native Go multi-return values - two
ordinary, borrowed Go interface values with no ownership/cleanup of their
own required (see "Known limitations" below for why).

Once the session is ready, it also demonstrates a full subscribe+publish
round trip through a *SWIG director*: it constructs a
`SubscribeTrackHandler` and a `PublishTrackHandler` via
`NewDirectorSubscribeTrackHandler()`/`NewDirectorPublishTrackHandler()`,
registers both with `SessionManager.AddTrackHandler()`, and publishes a
small object once a second via `PublishTrackHandler.PublishObject()`. The
subscriber's `ObjectReceived()`/`StatusChanged()` methods - plain Go
methods on a plain Go struct, no interface or embedding required - are
called directly by C++ whenever the real, virtual
`quicr::SubscribeTrackHandler` methods of the same name fire; see
"Subscribing and publishing via directors" below.

## Prerequisites

- CMake (the version already required to build libquicr)
- [SWIG](https://www.swig.org/) >= 4.0 (`swig` on `PATH`)
- A Go toolchain (`go` on `PATH`); any reasonably recent Go version works
- Everything else needed to build libquicr itself (a C++20 compiler, OpenSSL, etc.)

SWIG and Go are treated as optional dependencies: if either is missing,
`cmd/examples/swig/go/CMakeLists.txt` skips this example with a
`Skipping SWIG Go example (qgoclient): ...` status message instead of
failing the build.

## Building

This example is only built when examples are enabled, so configure the
project with `QUICR_BUILD_EXAMPLES=ON`. `cmd/` (and therefore this example)
is only added under CTest's `BUILD_TESTING` guard, which defaults to `ON`
when libquicr is configured as the top-level project (the normal case):

```bash
cmake -S . -B build -DQUICR_BUILD_EXAMPLES=ON
cmake --build build --target qgoclient_build
```

That target:
1. Runs `swig -go -c++` on `swig/quicr.i` to generate the `quicr` Go package.
2. Compiles and links the generated C++ wrapper into a `quicr_go` shared
   library, linked against the real `quicr` CMake target (so every one of
   libquicr's transitive dependencies - spdlog, picoquic, picotls, OpenSSL,
   etc. - is resolved automatically, the same as any other example).
3. Runs `go build` to compile `main.go` against the generated `quicr`
   package, producing the `qgoclient` binary.

Building the whole project (`cmake --build build`) also builds it, since
it's added to the default `ALL` target.

## Running

```bash
./build/cmd/examples/swig/go/qgoclient
```

Expected output looks like:

```
libquicr SWIG Go example
track namespace:      conference/room1
namespace == itself:   true
namespace == other:    false
namespace < other:     true
client config:         endpoint_id=qgoclient connect_uri=moq://relay.us-west-2.m10x.org:33437
...
Connecting... (Ctrl+C to exit)
transport.Status(): 1   session.GetStatus(): 1
transport.Status(): 0   session.GetStatus(): 0
session ready: subscribe+publish handlers added
publisher: PublishObject(#0) -> status 1
subscriber: status changed to 0
publisher: status changed to 0
transport.Status(): 0   session.GetStatus(): 0
publisher: PublishObject(#1) -> status 0
subscriber: received object #1 group=0 object=1 payload="hello #1"
transport.Status(): 0   session.GetStatus(): 0
^C
interrupted, disconnecting...
subscriber: status changed to 4
publisher: status changed to 2
disconnected: transport.Status(): 5   session.GetStatus(): 0
```

(the very first `PublishObject()` call above commonly comes back with a
non-`kOk` status - `1`/`kInternalError` in this trace - since it races the
relay's own subscribe/announce handshake; every following call succeeds
once that settles, and the subscriber only starts seeing `ObjectReceived()`
callbacks from that point on)

`AddClientTransport()` only kicks off a real, asynchronous QUIC client
transport against the `connect_uri` in `main.go`; it doesn't wait for the
handshake to finish. `main.go` polls `transport.Status()`/
`session.GetStatus()` once a second and keeps the program running for as
long as they're still connecting or connected (status `1`/`kConnecting`
settling to `0`/`kReady` once the handshake completes against a reachable
relay), printing each poll. It exits on its own once either side reaches a
terminal state (e.g. `kDisconnected`/`kFailedToConnect` if nothing is
listening at `connect_uri`).

On Ctrl+C/SIGTERM, it disconnects gracefully instead of just killing the
process: `session.Disconnect()` closes the underlying `Transport`
connection internally (see `Session::Disconnect()` in `session.cpp`), and
`main.go` then keeps polling status - briefly, and bounded to 3s - until
that shutdown is actually reflected in `Status()`/`GetStatus()` (e.g.
`transport.Status()` reaching `5`/`kShutdown` above) before exiting, since
`Disconnect()`/`Close()` queue their work onto libquicr's own
callback/packet-loop threads rather than completing synchronously.

## Subscribing and publishing via directors

Once `session.GetStatus()` first reaches `SessionStatus_kReady`, `main.go`
builds a `SubscribeTrackHandler` and a `PublishTrackHandler` for the same
`conference/room1` / `video` full track name and registers both with the
session:

```go
subHandler := quicr.NewDirectorSubscribeTrackHandler(
    &subscriber{}, fullTrackName, priority, quicr.NewOptionalGroupOrder())
pubHandler := quicr.NewDirectorPublishTrackHandler(
    &publisher{}, fullTrackName, quicr.TrackMode_kStream, priority, ttl)

manager.AddTrackHandler(session, subHandler)
manager.AddTrackHandler(session, pubHandler)
```

`NewDirectorSubscribeTrackHandler()`/`NewDirectorPublishTrackHandler()` are
SWIG *directors*: they construct a small hidden C++ subclass of the real
`quicr::SubscribeTrackHandler`/`quicr::PublishTrackHandler` whose virtual
methods forward straight to Go. The first argument (`&subscriber{}`/
`&publisher{}` above) is any Go value; SWIG's generated dispatch checks
whether it happens to implement a method with the right name and
signature (e.g. `ObjectReceived(quicr.ObjectHeaders, []byte,
quicr.Std_optional_Sl_quicr_messages_StreamHeaderProperties_Sg_)`) and, if
so, calls it directly - no interface to declare and no base type to embed,
just a plain Go struct with matching methods (see `subscriber`/`publisher`
in `main.go`). Real C++ virtual dispatch does the rest: when the library
itself calls `handler->ObjectReceived(...)` internally, it lands on Go's
override exactly as if a C++ subclass had provided it.

`SubscribeTrackHandler`/`PublishTrackHandler` must be constructed this way
(rather than via their real `Create()` factories, which aren't exposed to
Go at all) specifically *because* directors only work when Go constructs
the director subclass itself - a plain `Create()`-built instance would
never route through Go's override in the first place. `PublishObject()`
isn't a director-overridable method (it's called *by* Go, not overridden
*from* Go), so it's just an ordinary wrapped method call:

```go
status := pubHandler.PublishObject(objectHeaders, payload)
```

`AddTrackHandler()`/`RemoveTrackHandler()` follow the exact same "plain,
borrowed pointer, nothing to release" pattern as `AddClientTransport()`/
`AddServerTransport()` (see "Known limitations" below): `SessionManager`
takes real ownership internally the moment `AddTrackHandler()` is called,
so Go never needs to delete or otherwise clean up `subHandler`/`pubHandler`
- just don't use a handler again after calling `RemoveTrackHandler()` for
it (see the `RemoveTrackHandler()` comment in `quicr.i` for why that's a
hard requirement, not just a convention).

## Layout under the build tree

```
build/cmd/examples/swig/go/
├── gopkg/quicr/       generated `quicr` Go package (quicr.go, cgo_link.go,
│                      go.mod) and the built quicr_go shared library
├── wrap/              the generated wrap .cxx/.h (kept out of gopkg/quicr
│                      so cgo doesn't try to auto-compile it a second time)
├── app/               a throwaway copy of main.go plus a generated go.mod
│                      with a `replace` pointing at gopkg/quicr
└── qgoclient          the final built binary
```

Everything under `build/` is generated; nothing there is checked in.

## Known limitations

SWIG's Go module has no `std_shared_ptr.i`/`%shared_ptr` support at all
(unlike its Python, Java, C#, Ruby, R, D, and Octave/Scilab modules), and
`%feature("smartptr")` - the mechanism those other languages rely on - has
no effect on Go's own code generation. An earlier version of this example
worked around that with a hand-rolled smart-pointer-like wrapper type, but
that turned out to be exactly the kind of ceremony Go code should never
need for a library like this one: every `Transport`/`Session` this API
hands out is *also* kept alive independently by `SessionManager`'s own
internal bookkeeping (see the `%go_shared_ptr` comment in `quicr.i`, near
`session_manager.h`, for the full explanation), for as long as the manager
itself keeps that transport/session around - which covers this API's
entire useful surface. So `AddClientTransport()`/`AddServerTransport()`
simply hand back plain, borrowed `Transport`/`Session` values instead:
ordinary Go interface values, with no wrapper type, no reference counting,
and nothing to release, delete, or garbage-collect on the Go side at all -
call their methods directly, the same as any other wrapped C++ object in
this API.

SWIG's Go module also has no mechanism to make a wrapped C++ function
return multiple Go values directly (Go's own `typemaps.i` documents this;
unlike most other SWIG target languages, there's no argout-style splicing
of extra return values for Go). `AddClientTransport()` (the `%extend`'d
`SessionManager` method - renamed to `AddClientTransportRaw()` for Go,
still exposed under its normal name for every other language, where it
returns a `TransportSessionPtrPair`) therefore has a hand-written Go
companion also called `AddClientTransport()`, added directly into the
generated `quicr.go` via `%insert(go_wrapper)` in `quicr.i`. It unpacks the
pair by hand into a native `(Transport, Session)` multi-return using the
pair's own default `GetFirst()`/`GetSecond()` accessors (safe here with no
extra work, unlike the shared_ptr-returning pair the earlier design used -
see `quicr.i` for that history), and additionally translates a null
`Transport`/`Session` to a real Go `nil`: check with an ordinary `== nil`,
never `Swigcptr() == 0` (SWIG's own generated code never produces a nil
interface value on its own account for a null C++ pointer - it always
returns a non-nil wrapper around it - so this translation is deliberate,
not automatic). `AddServerTransport()` gets the analogous, simpler
`AddServerTransportRaw()`-renaming/nil-translation treatment despite
returning only a single value, purely for that nil-translation
convenience.

`SessionManager.AddTrackHandler()`/`RemoveTrackHandler()` are `%extend`'d
replacements for the real (`%ignore`'d) `AddHandler()`/`RemoveHandler()`,
which take an opaque, unusable `std::shared_ptr<Session>`/
`std::shared_ptr<TrackHandler>` for the same reason `Transport`/`Session`
themselves need the `AddClientTransport()`/`AddServerTransport()`
treatment above. Unlike those two though, the replacements here can't
reuse the real methods' own names: SWIG's `%ignore` matches by qualified
name, not by declaration, so an ignored name stays unavailable even to a
same-named `%extend` method added afterwards - attempting it silently
drops the `%extend` version too (there is no warning; it simply never
shows up in the generated Go package). See the `AddTrackHandler()`/
`RemoveTrackHandler()` comments in `quicr.i`, near `session_manager.h`,
for the full explanation.

`ObjectReceived()`'s third parameter (`stream_mode`, a
`std::optional<messages::StreamHeaderProperties>`) is typed against a
deliberately opaque, field-less stand-in for `messages::
StreamHeaderProperties` in `quicr.i` - just enough for the method
signature to parse and the director override to resolve correctly, not a
real, usable wrapping of that type. `main.go` ignores this parameter
entirely (see the subscriber's `ObjectReceived()` in `main.go`); see
`swig/SWIG_WARNINGS.md` for this and every other tracked-but-not-yet-fixed
gap in the current bindings.
