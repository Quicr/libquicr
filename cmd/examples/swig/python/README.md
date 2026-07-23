# libquicr SWIG Python Example

`qpyclient` is a minimal smoke test for the SWIG-generated Python bindings
of libquicr (generated from [`swig/quicr.i`](../../../../swig/quicr.i)) -
the Python counterpart to [`../go`](../go). It builds a `TrackNamespace`,
compares namespaces using the renamed comparison operators, constructs a
`ClientConfig`, creates a `SessionManager`, and calls
`SessionManager.AddClientTransport()` directly to get back a
`(Transport, Session)` pair - two ordinary, borrowed SWIG proxy objects
with no ownership/cleanup of their own required (see "Differences from the
Go example" below for why, and why the pair still can't just be tuple-
unpacked despite that).

Once the session is ready, it also demonstrates a full subscribe+publish
round trip through a *SWIG director*: it constructs a `Subscriber` and a
`Publisher` - ordinary Python subclasses of `quicr.SubscribeTrackHandler`/
`quicr.PublishTrackHandler`, defined directly in `main.py` - registers both
with `SessionManager.AddTrackHandler()`, and publishes a small object once
a second via `PublishTrackHandler.PublishObject()`. `Subscriber`'s
`ObjectReceived()`/`StatusChanged()` methods are called directly by C++
whenever the real, virtual `quicr::SubscribeTrackHandler` methods of the
same name fire on the underlying object; see "Subscribing and publishing
via directors" below.

## Prerequisites

- CMake (the version already required to build libquicr)
- [SWIG](https://www.swig.org/) >= 4.0 (`swig` on `PATH`)
- Python 3 with development headers/library (`python3-config`/the
  `Python3` CMake package needs to find both the interpreter and
  `Python.h`/`libpython`, not just a bare interpreter)
- Everything else needed to build libquicr itself (a C++20 compiler, OpenSSL, etc.)

SWIG and Python are treated as optional dependencies: if either is
missing, `cmd/examples/swig/python/CMakeLists.txt` skips this example with
a `Skipping SWIG Python example (qpyclient): ...` status message instead of
failing the build.

## Building

This example is only built when examples are enabled, so configure the
project with `QUICR_BUILD_EXAMPLES=ON`. `cmd/` (and therefore this example)
is only added under CTest's `BUILD_TESTING` guard, which defaults to `ON`
when libquicr is configured as the top-level project (the normal case):

```bash
cmake -S . -B build -DQUICR_BUILD_EXAMPLES=ON
cmake --build build --target qpyclient_build
```

That target:
1. Runs `swig -python -c++` on `swig/quicr.i` to generate the `quicr`
   Python module (`quicr.py` plus the `_quicr` C extension's wrapper
   source).
2. Compiles and links the generated C++ wrapper into `_quicr` (a Python
   extension module, `TYPE MODULE` via `swig_add_library`), linked against
   the real `quicr` CMake target (so every one of libquicr's transitive
   dependencies - spdlog, picoquic, picotls, OpenSSL, etc. - is resolved
   automatically, the same as any other example) and `Python3::Module`.
3. Copies `main.py` into the build tree next to the generated module.

Unlike the Go example, there's no separate Python "build" step for
`main.py` itself - it runs directly off the interpreter. Building the whole
project (`cmake --build build`) also builds this target, since it's added
to the default `ALL` target.

## Running

Python has no per-directory module-resolution story equivalent to Go's
`go.mod`/`replace`, so the generated `quicr` module has to be put on
`PYTHONPATH` explicitly:

```bash
PYTHONPATH=build/cmd/examples/swig/python/pypkg \
    python3 build/cmd/examples/swig/python/app/main.py \
    --track conference/room1 --track_name video
```

(add `--publish` on one of the two instances if running a subscriber and a
publisher against each other, as in the "Subscribing and publishing via
directors" walkthrough below)

Expected output looks like:

```
libquicr SWIG Python example
track namespace:      conference/room1
namespace == itself:   True
namespace == other:    False
namespace < other:     True
client config:         endpoint_id=qpyclient connect_uri=moq://relay.us-west-2.m10x.org:33437
Connecting... (Ctrl+C to exit)
transport.Status(): 0  session.GetStatus(): 0
...
session ready: subscribe+publish handlers added
subscriber: status changed to 0
subscriber: received object #0 group=0 object=1 payload=b'hello #1'
transport.Status(): 0  session.GetStatus(): 0
subscriber: received object #1 group=0 object=2 payload=b'hello #2'
^C
interrupted, disconnecting...
subscriber: status changed to 4
disconnected: transport.Status(): 5  session.GetStatus(): 0
```

`AddClientTransport()` only kicks off a real, asynchronous QUIC client
transport against `connect_uri` in `main.py`; it doesn't wait for the
handshake to finish. `main.py` polls `transport.Status()`/
`session.GetStatus()` once a second and keeps the program running for as
long as they're still connecting or connected (status `1`/`kConnecting`
settling to `0`/`kReady` once the handshake completes against a reachable
relay), printing each poll. It exits on its own once either side reaches a
terminal state (e.g. `kDisconnected`/`kFailedToConnect` if nothing is
listening at `connect_uri`).

On Ctrl+C/SIGTERM, it disconnects gracefully instead of just killing the
process: `session.Disconnect()` closes the underlying `Transport`
connection internally (see `Session::Disconnect()` in `session.cpp`), and
`main.py` then keeps polling status - briefly, and bounded to 3s - until
that shutdown is actually reflected in `Status()`/`GetStatus()` (e.g.
`transport.Status()` reaching `5`/`kShutdown` above) before exiting, since
`Disconnect()`/`Close()` queue their work onto libquicr's own
callback/packet-loop threads rather than completing synchronously.

## Subscribing and publishing via directors

Once `session.GetStatus()` first reaches `quicr.Session.Status_kReady`,
`main.py` builds a `Subscriber`/`Publisher` for the same
`conference/room1` / `video` full track name and registers it with the
session:

```python
handler = Subscriber(full_track_name, 0, quicr.OptionalGroupOrder())  # priority
manager.AddTrackHandler(session, handler)
handler = handler.__disown__()  # see "Known limitations" below - required
```

`Subscriber`/`Publisher` (defined in `main.py`) are ordinary Python
subclasses of `quicr.SubscribeTrackHandler`/`quicr.PublishTrackHandler` -
real SWIG *directors*: constructing one builds a small hidden C++ subclass
of the real `quicr::SubscribeTrackHandler`/`quicr::PublishTrackHandler`
whose virtual methods forward straight back into whichever Python override
actually exists on that instance. Unlike Go (which has no true subclassing
story at all - a plain Go struct with matching method names stands in for
a real override there, via `NewDirectorSubscribeTrackHandler()`/
`NewDirectorPublishTrackHandler()`), Python directors work exactly like
real C++ virtual dispatch already suggests: no interface to declare and no
base type to embed beyond the ordinary `class Subscriber(quicr.
SubscribeTrackHandler):` subclassing already shown above. When the library
itself calls `handler->ObjectReceived(...)` internally, it lands on
`Subscriber.ObjectReceived()` exactly as if a real C++ subclass had
provided it.

`PublishObject()` isn't a director-overridable method (it's called *by*
Python, not overridden *from* Python), so it's just an ordinary wrapped
method call:

```python
status = handler.PublishObject(object_headers, payload)
```

### Director callbacks and the GIL

`Subscriber.ObjectReceived()`/`StatusChanged()` are invoked directly on
whichever libquicr-internal C++ thread first notices there's a callback to
make - a background packet-I/O thread, never the thread that called
`AddTrackHandler()`. SWIG's own director-dispatch code always acquires the
GIL itself before calling into Python for a director override, regardless
of any build flag, so nothing extra needs to be acquired inside
`Subscriber`/`Publisher` themselves - just don't assume either method runs
on Python's main thread.

This is unrelated to the `-threads` SWIG flag `CMakeLists.txt` passes for
this module (`%module(directors="1") quicr` plus `-threads` together):
that flag only affects *ordinary* wrapped calls (`session.GetStatus()`,
`transport.Status()`, `handler.PublishObject()`, ...) - without it, SWIG's
generated wrapper code holds the GIL for such a call's entire duration,
which would otherwise block the director callback thread above (or any
other Python thread) from ever running concurrently with it.

## Differences from the Go example

- **No hand-written multi-return wrapper needed.** Go can't return more
  than one value from a wrapped C++ function directly, so
  `SessionManager.AddClientTransport()` needs a hand-written
  `%insert(go_wrapper)` companion in `quicr.i` just for Go. Python calls
  the real, `%extend`'d `AddClientTransport()` directly and gets back the
  `TransportSessionPtrPair` as-is - **but this pair still can't be
  tuple-unpacked** (`transport, session = pair` raises `ValueError: too
  many values to unpack`); use `pair.first`/`pair.second` instead. See
  "`std::pair` proxy objects look tuple-like in Python but silently break
  tuple unpacking" in [`swig/SWIG_WARNINGS.md`](../../../../swig/SWIG_WARNINGS.md)
  for the full explanation.
- **No nil-translation wrapper needed.** Go needs a hand-written
  `AddClientTransportRaw()`/`AddServerTransportRaw()`-to-`nil` translation
  for a null `Transport`/`Session`, since SWIG's own generated Go code
  never produces a nil interface value on its own account for a null C++
  pointer. Python's default proxy-object handling already returns a real
  `None` for any null wrapped pointer, in every case, with no extra code
  needed on either the C++ or Python side.
- **Properties, not `GetX()`/`SetX()`.** Public C++ fields (e.g.
  `FullTrackName.name_space`, `ObjectHeaders.group_id`,
  `ClientConfig.endpoint_id`) are ordinary Python attributes -
  `obj.field = value` - not capitalized getter/setter method pairs; Go
  needs those method pairs only because Go itself has no
  property/attribute syntax of its own.
- **Nested enum naming.** `quicr::Session::Status` (nested inside `class
  Session`) is `quicr.Session.Status_kReady` in Python (a class attribute,
  matching where the real C++ declaration puts it) vs. the flattened
  `quicr.SessionStatus_kReady` Go produces for the exact same declaration.
  Plain namespace-scope enums like `quicr::TransportStatus` aren't nested
  in any class, so both languages expose those identically
  (`quicr.TransportStatus_kReady`).

See [`swig/SWIG_WARNINGS.md`](../../../../swig/SWIG_WARNINGS.md) for the
full list of every SWIG-related bug, gap, and cross-language asymmetry
found while building this example - including one, real crash (a
double-free/segfault, not just an exception) that has no Go-side
counterpart to compare against, described there in detail.

## Layout under the build tree

```
build/cmd/examples/swig/python/
├── pypkg/             generated `quicr` Python module: quicr.py plus the
│                      compiled _quicr extension (_quicr.cpython-3xx-*.so)
├── wrap/              the generated wrap .cxx/.h (kept out of pypkg/ the
│                      same way the Go example keeps it out of gopkg/quicr)
├── app/               a throwaway copy of main.py
└── qpyclient.stamp    empty marker file the qpyclient_build target
                       depends on/produces (see CMakeLists.txt)
```

Everything under `build/` is generated; nothing there is checked in.

## Known limitations

- **`AddTrackHandler()` requires an explicit `__disown__()` call
  immediately afterward** - this is not optional, and skipping it is a
  real, reproduced double-free/segfault (`Fatal Python error: Segmentation
  fault` inside `_wrap_delete_SubscribeTrackHandler`), not just a leak or a
  clean exception. See "Double free / segfault:
  `AddTrackHandler()`'s raw-pointer-into-`shared_ptr` pattern silently
  double-owns the object in Python" in `swig/SWIG_WARNINGS.md` for the
  full root cause - the short version is that `AddTrackHandler()` builds a
  new `std::shared_ptr<TrackHandler>` directly around the same raw pointer
  Python's own default proxy ownership already believes it solely owns,
  and `__disown__()` is SWIG's own purpose-built way to hand that
  ownership over cleanly.
- **`ObjectReceived()`'s third parameter (`stream_mode`, a
  `std::optional<messages::StreamHeaderProperties>`)** is a real, usable
  `quicr.StreamHeaderProperties` (`.extensions`/`.subgroup_id_mode`/
  `.end_of_group`/`.default_priority`, all ordinary Python properties), or
  `None` when the optional is empty. `main.py`'s own
  `Subscriber.ObjectReceived()` just doesn't happen to read it, purely to
  keep this example minimal - the same as `main.go` does for its own Go
  counterpart.
- **`new_object_headers()` doesn't set `priority`/`ttl`/`track_mode`** on
  the `ObjectHeaders` it builds - all three are `std::optional<T>` fields
  left at their default (empty) value, the same as `main.go`'s own
  `newObjectHeaders()`. This is just this example keeping its
  `ObjectHeaders` construction minimal, unrelated to any SWIG-specific
  issue - unlike the `stream_mode` parameter above, these three are
  perfectly usable from Python if a real caller needs them.
