# libquicr SWIG C# Example

`qcsclient` is a minimal smoke test for the SWIG-generated C# bindings of
libquicr (generated from [`swig/quicr.i`](../../../../swig/quicr.i)) - the
C# counterpart to [`../go`](../go) and [`../python`](../python). It builds
a `TrackNamespace`, compares namespaces using the renamed comparison
operators, constructs a `ClientConfig`, creates a `SessionManager`, and
calls `SessionManager.AddClientTransport()` directly to get back a
`TransportSessionPtrPair` - two ordinary, borrowed proxy objects with no
ownership/cleanup of their own required (see "Differences from the Go/
Python examples" below for why, and why the pair still needs
`.first`/`.second` rather than deconstruction).

Once the session is ready, it also demonstrates a full subscribe+publish
round trip through a *SWIG director*: it constructs a `Subscriber` and a
`Publisher` - ordinary C# subclasses of `quicr.SubscribeTrackHandler`/
`quicr.PublishTrackHandler`, defined directly in `Program.cs` - registers
both with `SessionManager.AddTrackHandler()`, and publishes a small object
once a second via `PublishTrackHandler.PublishObject()`. `Subscriber`'s
`ObjectReceived()`/`StatusChanged()` methods are called directly by C++
whenever the real, virtual `quicr::SubscribeTrackHandler` methods of the
same name fire on the underlying object; see "Subscribing and publishing
via directors" below.

## Prerequisites

- CMake (the version already required to build libquicr)
- [SWIG](https://www.swig.org/) >= 4.0 (`swig` on `PATH`)
- A [.NET SDK](https://dotnet.microsoft.com/) (`dotnet` on `PATH`); the
  generated project targets `net8.0` but rolls forward to whatever major
  runtime is actually installed (see "Known limitations" below)
- Everything else needed to build libquicr itself (a C++20 compiler, OpenSSL, etc.)

SWIG and the .NET SDK are treated as optional dependencies: if either is
missing, `cmd/examples/swig/csharp/CMakeLists.txt` skips this example with
a `Skipping SWIG C# example (qcsclient): ...` status message instead of
failing the build.

## Building

This example is only built when examples are enabled, so configure the
project with `QUICR_BUILD_EXAMPLES=ON`. `cmd/` (and therefore this example)
is only added under CTest's `BUILD_TESTING` guard, which defaults to `ON`
when libquicr is configured as the top-level project (the normal case):

```bash
cmake -S . -B build -DQUICR_BUILD_EXAMPLES=ON
cmake --build build --target qcsclient_build
```

That target:
1. Runs `swig -csharp -c++ -namespace quicr` on `swig/quicr.i` to generate
   the C# proxy classes (`quicr.cs`, `SessionManager.cs`, ...) under the
   `quicr` namespace.
2. Compiles and links the generated C++ wrapper into `libquicr.{dylib,so}`
   (`TYPE SHARED` via `swig_add_library`), linked against the real `quicr`
   CMake target (so every one of libquicr's transitive dependencies -
   spdlog, picoquic, picotls, OpenSSL, etc. - is resolved automatically,
   the same as any other example).
3. Generates a `qcsclient.csproj` referencing those proxy classes plus a
   copy of `Program.cs`, then runs `dotnet build` to produce `qcsclient.dll`.
4. Copies the native `libquicr.{dylib,so}` next to `qcsclient.dll` - see
   "Known limitations" below for why this copy, specifically, is required.

Building the whole project (`cmake --build build`) also builds it, since
it's added to the default `ALL` target.

## Running

```bash
dotnet build/cmd/examples/swig/csharp/app/out/qcsclient.dll \
    --track conference/room1 --track_name video
```

(add `--publish` on one of the two instances if running a subscriber and a
publisher against each other, as in the "Subscribing and publishing via
directors" walkthrough below)

Expected output looks like:

```
libquicr SWIG C# example
track namespace:      conference/room1
namespace == itself:   True
namespace == other:    False
namespace < other:     True
client config:         endpoint_id=qcsclient connect_uri=moq://relay.us-west-2.m10x.org:33437
Connecting... (Ctrl+C to exit)
transport.Status(): 0  session.GetStatus(): 0
session ready: subscribe+publish handlers added
subscriber: status changed to kOk
subscriber: received object #0 group=0 object=1 payload=hello #1
^C
interrupted, disconnecting...
disconnected: transport.Status(): 5  session.GetStatus(): 0
```

`AddClientTransport()` only kicks off a real, asynchronous QUIC client
transport against `connect_uri` in `Program.cs`; it doesn't wait for the
handshake to finish. `Program.cs` polls `transport.Status()`/
`session.GetStatus()` once a second and keeps the program running for as
long as they're still connecting or connected (status `1`/`kConnecting`
settling to `0`/`kReady` once the handshake completes against a reachable
relay), printing each poll. It exits on its own once either side reaches a
terminal state (e.g. `kDisconnected`/`kFailedToConnect` if nothing is
listening at `connect_uri`).

On Ctrl+C/SIGTERM, it disconnects gracefully instead of just killing the
process: `session.Disconnect()` closes the underlying `Transport`
connection internally (see `Session::Disconnect()` in `session.cpp`), and
`Program.cs` then keeps polling status - briefly, and bounded to 3s - until
that shutdown is actually reflected in `Status()`/`GetStatus()` (e.g.
`transport.Status()` reaching `5`/`kShutdown` above) before exiting, since
`Disconnect()`/`Close()` queue their work onto libquicr's own
callback/packet-loop threads rather than completing synchronously.

## Subscribing and publishing via directors

Once `session.GetStatus()` first reaches `Session.Status.kReady`,
`Program.cs` builds a `Subscriber`/`Publisher` for the same
`conference/room1` / `video` full track name and registers it with the
session:

```csharp
var subscriber = new Subscriber(fullTrackName, 0, new OptionalGroupOrder()); // priority
manager.AddTrackHandler(session, subscriber);
subscriber.Disown(); // see "Known limitations" below - required
```

`Subscriber`/`Publisher` (defined in `Program.cs`) are ordinary C#
subclasses of `quicr.SubscribeTrackHandler`/`quicr.PublishTrackHandler` -
real SWIG *directors*: constructing one builds a small hidden C++ subclass
of the real `quicr::SubscribeTrackHandler`/`quicr::PublishTrackHandler`
whose virtual methods forward straight back into whichever C# override
actually exists on that instance - the same subclassing model
[`../python`](../python) uses, via ordinary `: base(...)` construction
against `SubscribeTrackHandler`'s own (`protected`, subclass-only)
constructor. Unlike Go (which has no true subclassing story at all - a
plain Go struct with matching method names stands in for a real override
there, via `NewDirectorSubscribeTrackHandler()`/
`NewDirectorPublishTrackHandler()`), C# directors work exactly like real
C++ virtual dispatch already suggests: no interface to declare and no
delegate-registration step beyond the ordinary `class Subscriber :
SubscribeTrackHandler` subclassing already shown above. When the library
itself calls `handler->ObjectReceived(...)` internally, it lands on
`Subscriber.ObjectReceived()` exactly as if a real C++ subclass had
provided it.

`PublishObject()` isn't a director-overridable method (it's called *by* C#,
not overridden *from* C#), so it's just an ordinary wrapped method call:

```csharp
var status = publishHandler.PublishObject(objectHeaders, payload);
```

### Director callbacks and threading

`Subscriber.ObjectReceived()`/`StatusChanged()` are invoked directly on
whichever libquicr-internal C++ thread first notices there's a callback to
make - a background packet-I/O thread, never the thread that called
`AddTrackHandler()`. Unlike Python (which has a GIL that SWIG's director
dispatch always acquires for you first), C# has no equivalent global lock
to acquire or worry about here - just don't assume either method runs on
the same thread as `Main()`.

## Differences from the Go/Python examples

- **Real subclassing, like Python - not delegate structs, like Go.** See
  "Subscribing and publishing via directors" above.
- **Properties, not `GetX()`/`SetX()`.** Public C++ fields (e.g.
  `FullTrackName.name_space`, `ObjectHeaders.group_id`,
  `ClientConfig.endpoint_id`) are ordinary C# properties -
  `obj.field = value` - using the real, lowercase C++ member names (not
  capitalized to match C# convention) - matching Python's own behavior,
  unlike Go, which needs capitalized `GetX()`/`SetX()` method pairs only
  because Go itself has no property/attribute syntax of its own.
- **Nested enum naming.** `quicr::Session::Status` (nested inside `class
  Session`) is `quicr.Session.Status.kReady` in C# (a real nested enum
  type, matching where the real C++ declaration puts it and matching
  Python's own `quicr.Session.Status_kReady` in spirit) vs. the flattened
  `quicr.SessionStatus_kReady` Go produces for the exact same declaration.
  Plain namespace-scope enums like `quicr::TransportStatus` aren't nested
  in any class, so all three languages expose those identically in shape
  (`quicr.TransportStatus.kReady` in C#/`quicr.TransportStatus_kReady` in
  Go and Python).
- **No hand-written multi-return wrapper needed.** Go can't return more
  than one value from a wrapped C++ function directly, so
  `SessionManager.AddClientTransport()` needs a hand-written
  `%insert(go_wrapper)` companion in `quicr.i` just for Go. C# calls the
  real, `%extend`'d `AddClientTransport()` directly and gets back the
  `TransportSessionPtrPair` as-is, with ordinary `.first`/`.second`
  properties (matching the real C++ member names, the same as Python's own
  `pair.first`/`pair.second`) - not deconstructable via C# tuple patterns,
  since it's a real proxy class, not a `System.ValueTuple`.
- **No nil-translation wrapper needed.** Go needs a hand-written
  `AddClientTransportRaw()`/`AddServerTransportRaw()`-to-`nil` translation
  for a null `Transport`/`Session`, since SWIG's own generated Go code
  never produces a nil interface value on its own account for a null C++
  pointer. C#'s default proxy-object handling already returns a real
  `null` for any null wrapped pointer, in every case, with no extra code
  needed on either the C++ or C# side - the same as Python.

See `swig/csharp/typemaps.i` and `swig/csharp/type_extensions.i` for every
C#-specific typemap/feature this binding needed beyond what `swig/quicr.i`,
`swig/go/`, and `swig/python/` already provide language-agnostically or for
Go/Python specifically.

## Layout under the build tree

```
build/cmd/examples/swig/csharp/
├── pkg/               generated C# proxy classes (quicr.cs, quicrPINVOKE.cs,
│                      SessionManager.cs, ...) and the built libquicr.{dylib,so}
├── wrap/              the generated wrap .cxx/.h (kept out of pkg/ the
│                      same way the Go/Python examples keep it out of
│                      their own generated-package directories)
├── app/               a generated qcsclient.csproj (referencing pkg/ by
│                      its real build-tree path) plus a copy of Program.cs
│   └── out/           `dotnet build`'s own output: qcsclient.dll plus a
│                      copy of libquicr.{dylib,so} (see "Known
│                      limitations" below for why it's copied here)
└── qcsclient.stamp    empty marker file the qcsclient_build target
                       depends on/produces (see CMakeLists.txt)
```

Everything under `build/` is generated; nothing there is checked in.

## Known limitations

- **`AddTrackHandler()` requires an explicit `Disown()` call immediately
  afterward** - the C# analog of Python's `__disown__()`, and just as
  non-optional: every director-enabled C# proxy defaults to owning its
  underlying C++ object (deleting it once nothing in C# references it
  anymore), but `AddTrackHandler()` also builds a brand-new
  `std::shared_ptr<TrackHandler>` directly around that same raw pointer
  inside `SessionManager::AddTrackHandler()`'s `%extend` body in `quicr.i`.
  Left alone, that's two independent owners of the same pointer, and
  whichever lets go first double-frees. Unlike Python, SWIG's C# director
  code generation doesn't provide an equivalent method out of the box;
  `csharp/type_extensions.i` adds one by hand (a `Disown()` method spliced
  into `TrackHandler`'s own proxy class via `%typemap(cscode)`, flipping
  off the proxy's `swigCMemOwn` flag) specifically to close this gap.
- **The native library must sit next to the .NET executable.** .NET's
  default `[DllImport]` native-library probing checks the app's own base
  directory first; `CMakeLists.txt`'s build step copies
  `libquicr.{dylib,so}` into `app/out/` right after `dotnet build` for
  exactly this reason. Running `qcsclient.dll` from anywhere else without
  that copy alongside it fails at the first P/Invoke call.
- **`RollForward=LatestMajor`, not a pinned runtime.** The generated
  project targets `net8.0` to build against, but rolls forward to
  whichever major .NET runtime is actually installed at run time (a real,
  reproduced "You must install or update .NET to run this application"
  launch failure otherwise, on any machine whose only installed runtime is
  newer than 8.0 - e.g. a 10.0-only machine, with no 8.0 runtime installed
  alongside it).
- **`ObjectReceived()`'s third parameter (`stream_mode`, a
  `std::optional<messages::StreamHeaderProperties>`)** is a real, usable
  `quicr.OptionalStreamHeaderProperties` (`HasValue()`/`Value()`, the
  latter giving `.extensions`/`.subgroup_id_mode`/`.end_of_group`/
  `.default_priority`), or `null` when the optional is empty. `Program.cs`'s
  own `Subscriber.ObjectReceived()` just doesn't happen to read it, purely
  to keep this example minimal - the same as `main.go`/`main.py` do for
  their own counterparts. Getting this parameter to compile at all needed
  a C#-specific fix of its own: `messages::StreamHeaderProperties`'s fields
  are all `const` (see `messages.h`), making it copy-constructible but not
  copy-assignable, which SWIG's default C# by-value-parameter code
  (`arg = *argp;`, i.e. assignment) doesn't compile against - `csharp/
  typemaps.i` overrides just this one parameter's `%typemap(in)` with a
  placement-new + copy-construct instead, mirroring what SWIG's Go module
  already generates for the identical parameter by default.
- **`NewFullTrackName()`/`NewObjectHeaders()` don't set `priority`/`ttl`/
  `track_mode`** on the `ObjectHeaders` they build beyond what
  `PublishObject()` strictly needs - all three are `std::optional<T>`
  fields left at their default (empty) value, the same as `main.go`/
  `main.py`'s own equivalents. This is just this example keeping its
  `ObjectHeaders` construction minimal, unrelated to any SWIG-specific
  issue - `ObjectHeaders.priority`/`.ttl` are perfectly usable `byte?`/
  `ushort?` properties from C# if a real caller needs them (see
  `csharp/typemaps.i`'s `%csharp_optional_scalar` for how `std::optional<T>`
  scalar fields map to idiomatic C# `Nullable<T>` in general).
