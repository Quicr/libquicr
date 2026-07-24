#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

"""qpyclient is a minimal smoke test for the SWIG-generated Python bindings
(see swig/quicr.i) - the Python counterpart to ../go/main.go. It exercises
plain structs/strings/enums, SessionManager, and - once connected - a full
subscribe+publish round trip through a SWIG *director*: a Python class
(Subscriber below) that actually subclasses quicr.SubscribeTrackHandler and
overrides ObjectReceived()/StatusChanged(), called directly by C++ whenever
quicr::SubscribeTrackHandler's real virtual methods fire, with no polling
involved.

Unlike Go (which has no true subclassing story at all - see main.go's own
NewDirectorSubscribeTrackHandler()/NewDirectorPublishTrackHandler() comment
- so a *plain* Go struct with matching method names stands in for a real
override there), Python directors work exactly like real C++ virtual
dispatch already suggests: Subscriber/Publisher below are ordinary Python
subclasses of quicr.SubscribeTrackHandler/quicr.PublishTrackHandler, and
SWIG's generated director glue takes care of routing the real C++ virtual
call to whichever Python override actually exists - if a subclass doesn't
override a given virtual method, the C++ base implementation runs exactly
as if a real C++ subclass had simply not overridden it either.

AddClientTransport()/AddServerTransport() return plain, borrowed references
(ordinary SWIG proxy objects, same as any other wrapped C++ object in this
API) - not an owning smart pointer of any kind. SessionManager keeps its
own owning reference internally (see the %go_shared_ptr comment in
quicr.i, near session_manager.h - the reasoning is entirely language-
agnostic despite the name) for as long as it manages that transport/
session, so there is nothing to release or otherwise clean up on the
Python side here at all: once you have a Transport/Session, you can just
call methods on it directly.

A failed call can yield a null Transport and/or Session; check with an
ordinary `is None`. Unlike Go, this translation to a real Python None for
a null C++ pointer is SWIG's own default behavior for every wrapped
pointer return in Python - there is no Python equivalent of Go's
AddClientTransport()/AddServerTransport() needing their own hand-written
nil-translating wrappers (see "Differences from the Go example" in
README.md).

SubscribeTrackHandler/PublishTrackHandler do NOT work the same way as
Transport/Session, and it matters a lot more here: AddTrackHandler()
below takes `handler` as a plain TrackHandler*, then - entirely on the
C++ side, inside SessionManager::AddTrackHandler()'s %extend body in
quicr.i - builds a brand-new std::shared_ptr<TrackHandler> directly around
that raw pointer, which is only ever safe (see enable_shared_from_this's
own requirements) if nothing else believes it owns that pointer already.
But it does: Subscriber(...)/Publisher(...) below construct real Python
*director* objects (see above), and every ordinary SWIG Python proxy -
director or not - defaults to owning its underlying C++ object, i.e. its
own __del__/deallocation runs `delete` on it once nothing in Python
references it anymore. That's now a second, independent owner of the
exact same pointer alongside C++'s new shared_ptr, and it *will* double-
free and crash (a real, reproduced `Fatal Python error: Segmentation
fault` inside `_wrap_delete_SubscribeTrackHandler`, hit while writing this
example - see "Double ownership crash: AddTrackHandler() and Python's
default proxy ownership" in SWIG_WARNINGS.md) the moment Python happens
to garbage-collect `handler` - typically exactly when this script's own
`handler` local goes out of scope at process exit, but by no means
guaranteed to wait that long.

The fix, applied below right after every AddTrackHandler() call: SWIG
generates a `__disown__()` method on every director-enabled proxy class
(SubscribeTrackHandler/PublishTrackHandler included) purpose-built for
exactly this "a C++ container just took real ownership of this director
object out from under me" handoff - it flips the proxy's own `thisown`
flag off (so Python's own deallocation no longer calls `delete`) and
returns a `weakref.proxy` standing in for the same object, which is why
`handler = handler.__disown__()` immediately below reassigns `handler`
itself rather than just calling `__disown__()` for its side effect: from
that point on, `handler`'s only remaining owner is the shared_ptr C++
just built above, exactly as intended.
"""

import argparse
import signal
import sys
import time

import quicr


class Subscriber(quicr.SubscribeTrackHandler):
    """Subscriber's ObjectReceived()/StatusChanged() are called directly by
    C++ (via a SWIG director) whenever the real quicr::SubscribeTrackHandler
    virtual methods of the same name fire on the underlying C++ object -
    this is a real C++ subclass under the hood (see SwigDirector_
    SubscribeTrackHandler in the generated wrap .cxx), constructed the
    moment Subscriber(...) below runs SubscribeTrackHandler.__init__.

    IMPORTANT - see "Director callbacks and the GIL" in README.md: these
    two methods are invoked directly on whichever libquicr-internal C++
    thread first notices there's a callback to make (a background
    packet-I/O thread, not the thread that called AddTrackHandler()), so
    unlike every *ordinary* wrapped call below (session.GetStatus(),
    transport.Status(), pub_handler.PublishObject(), ...), these two
    method bodies are already running with the GIL held for you by SWIG's
    own director-dispatch code before a single line of this class's own
    Python runs - there is nothing extra to acquire here, just don't
    assume you're on Python's main thread.
    """

    def ObjectReceived(self, object_headers, data, _stream_mode):
        print(
            f"subscriber: received object #{self.received} "
            f"group={object_headers.group_id} object={object_headers.object_id} "
            f"payload={bytes(data)!r}"
        )
        self.received += 1

    def StatusChanged(self, status):
        print(f"subscriber: status changed to {status}")


class Publisher(quicr.PublishTrackHandler):
    """Publisher only overrides StatusChanged for a bit of visibility into
    the announce handshake; PublishObject() itself isn't virtual (see the
    PublishTrackHandler comment in quicr.i for why) - it's called directly
    on this instance, same as any other wrapped method.
    """

    def StatusChanged(self, status):
        print(f"publisher: status changed to {status}")


def transport_stopped(status):
    return status == quicr.TransportStatus_kShutdown


def session_stopped(status):
    # Unlike TransportStatus (a plain namespace-scope C++ enum, exposed by
    # SWIG as a module-level quicr.TransportStatus_kXxx constant in every
    # language, Go included), quicr::Session::Status is nested *inside*
    # class Session - Go's SWIG backend still flattens that into a single
    # top-level identifier (quicr.SessionStatus_kNotConnected, concatenating
    # the enclosing class name and the enum's own name), but Python instead
    # keeps it exactly where C++ says it lives: as a Session class attribute,
    # quicr.Session.Status_kNotConnected (see "Nested enums: Session.Status
    # vs. Go's SessionStatus" in SWIG_WARNINGS.md).
    return status == quicr.Session.Status_kNotConnected


def new_full_track_name(namespace_tuple, name):
    """Builds a quicr.FullTrackName from a plain namespace tuple and a name
    string. FullTrackName is an aggregate with no constructor of its own
    (see track_name.h) - name_space/name are ordinary public fields set via
    the usual setters, name via the SetNameBytes() convenience %extend'd
    onto FullTrackName in quicr.i specifically so callers don't have to
    build a ByteVector by hand one byte at a time.
    """
    ftn = quicr.FullTrackName()
    ftn.name_space = quicr.TrackNamespace(namespace_tuple)
    ftn.SetNameBytes(name.encode())
    return ftn


def new_object_headers(group_id, object_id, payload_len):
    """Builds the minimal quicr.ObjectHeaders PublishObject() needs:
    group/object id, payload length, and status. priority/ttl/track_mode
    are left as their default (empty) std::optional - see "Known
    limitations" in README.md for why these three fields specifically
    aren't set here the way main.go's own newObjectHeaders() doesn't set
    them either, despite the two being unrelated issues under the hood.
    """
    headers = quicr.ObjectHeaders()
    headers.group_id = group_id
    headers.object_id = object_id
    headers.subgroup_id = 0
    headers.payload_length = payload_len
    headers.status = quicr.ObjectStatus_kAvailable
    return headers


def graceful_disconnect(transport, session):
    session.Disconnect()
    transport.Shutdown()

    timeout = 3.0
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        t_status = transport.Status()
        s_status = session.GetStatus()
        if session_stopped(s_status) and transport_stopped(t_status):
            print(f"disconnected: transport.Status(): {t_status:<2} session.GetStatus(): {s_status:<2}")
            return
        time.sleep(0.05)

    t_status = transport.Status()
    s_status = session.GetStatus()
    print(f"transport/session did not reach a terminal state within {timeout}s, exiting anyway")
    print(f"disconnected: transport.Status(): {t_status:<2} session.GetStatus(): {s_status:<2}")


def main():
    parser = argparse.ArgumentParser(description="libquicr SWIG Python example")
    parser.add_argument("--track", default="", help="/ separated tuple of strings")
    parser.add_argument("--track_name", default="", help="Track name")
    parser.add_argument(
        "--publish",
        action="store_true",
        help="This client is a publisher, and will create a PublishTrackHandler.",
    )
    args = parser.parse_args()

    print("libquicr SWIG Python example")

    namespace_tuple = args.track.split("/")
    namespace = quicr.TrackNamespace(namespace_tuple)
    print(f"track namespace:      {namespace.Str()}")

    duplicate = quicr.TrackNamespace(namespace_tuple)
    different = quicr.TrackNamespace(["conference", "room2"])
    print(f"namespace == itself:   {quicr.TrackNamespaceEquals(namespace, duplicate)}")
    print(f"namespace == other:    {quicr.TrackNamespaceEquals(namespace, different)}")
    print(f"namespace < other:     {quicr.TrackNamespaceLess(namespace, different)}")

    config = quicr.ClientConfig()
    config.endpoint_id = "qpyclient"
    config.connect_uri = "moq://relay.us-west-2.m10x.org:33437"
    print(f"client config:         endpoint_id={config.endpoint_id} connect_uri={config.connect_uri}")

    manager = quicr.SessionManager()

    # Unlike Go (which needs a hand-written AddClientTransport() Go
    # companion purely to unpack this pair into a native multi-return -
    # see quicr.i's own %insert(go_wrapper) comment, near
    # session_manager.h, for why that's a Go-only problem), Python calls
    # the real SessionManager.AddClientTransport() directly and gets back
    # a std::pair<Transport*, Session*> that SWIG's own (language-generic,
    # not hand-written here) std::pair support already exposes with
    # .first/.second attributes (matching the real C++ member names,
    # unlike Go's own capitalized GetFirst()/GetSecond(), needed there
    # only because Go has no property/attribute syntax of its own).
    # It also defines __len__/__getitem__ (index % 2, i.e. never raising
    # IndexError), which looks tuple-like but deliberately isn't one -
    # `transport, session = pair` fails with "too many values to unpack"
    # since Python's own old-style iteration protocol fallback (used when
    # __iter__ is absent, as here) keeps calling __getitem__ with
    # increasing indices *until* IndexError, which this never raises -
    # so .first/.second below, not unpacking, is the only correct way to
    # pull the two values out here (caught by actually running this, not
    # by anything at swig or C++-compile time).
    pair = manager.AddClientTransport(config)
    transport = pair.first
    session = pair.second
    if transport is None or session is None:
        print("AddClientTransport returned a null transport/session", file=sys.stderr)
        sys.exit(1)

    interrupted = False

    def handle_signal(_signum, _frame):
        nonlocal interrupted
        interrupted = True

    signal.signal(signal.SIGINT, handle_signal)
    signal.signal(signal.SIGTERM, handle_signal)

    handler = None
    handlers_added = False
    next_object_id = 0

    print("Connecting... (Ctrl+C to exit)")
    while True:
        for _ in range(10):
            if interrupted:
                break
            time.sleep(0.1)

        if interrupted:
            print("\ninterrupted, disconnecting...")
            if handlers_added:
                manager.RemoveTrackHandler(session, handler)
            graceful_disconnect(transport, session)
            return

        t_status = transport.Status()
        s_status = session.GetStatus()

        if not handlers_added and s_status == quicr.Session.Status_kReady:
            print(f"transport.Status(): {t_status:<2} session.GetStatus(): {s_status:<2}")
            full_track_name = new_full_track_name(namespace_tuple, args.track_name)
            if args.publish:
                handler = Publisher(full_track_name, quicr.TrackMode_kStream, 0, 500)  # priority, ttl (ms)
                manager.AddTrackHandler(session, handler)
            else:
                handler = Subscriber(full_track_name, 0, quicr.OptionalGroupOrder())  # priority
                handler.received = 0
                manager.AddTrackHandler(session, handler)
            # Required the moment AddTrackHandler() above returns - see the
            # "SubscribeTrackHandler/PublishTrackHandler do NOT work the
            # same way as Transport/Session" paragraph in this file's own
            # module docstring for exactly why a real, reproduced crash
            # happens without this.
            handler = handler.__disown__()
            handlers_added = True
            print("session ready: subscribe+publish handlers added")

        if handlers_added and s_status == quicr.Session.Status_kReady and args.publish:
            payload = f"hello #{next_object_id}".encode()
            status = handler.PublishObject(new_object_headers(0, next_object_id, len(payload)), payload)
            print(f"publisher: PublishObject(#{next_object_id}) -> status {status}")
            next_object_id += 1

        if transport_stopped(t_status) or session_stopped(s_status):
            print("transport/session reached a terminal state, exiting")
            return


if __name__ == "__main__":
    main()
