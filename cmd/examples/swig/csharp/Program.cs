// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

// qcsclient is a minimal smoke test for the SWIG-generated C# bindings of
// libquicr (see swig/quicr.i) - the C# counterpart to ../go/main.go and
// ../python/main.py. It exercises plain structs/strings/enums,
// SessionManager, and - once connected - a full subscribe+publish round
// trip through a SWIG *director*: a C# class (Subscriber/Publisher below)
// that actually subclasses quicr.SubscribeTrackHandler/PublishTrackHandler
// and overrides ObjectReceived()/StatusChanged(), called directly by C++
// whenever the real virtual methods of the same name fire, with no
// polling involved - the same subclassing model Python uses (see
// main.py's own module docstring), unlike Go, which has no true
// subclassing story and stands a plain struct with matching method names
// in for a real override instead.
//
// AddClientTransport() returns the Transport/Session it creates as plain,
// borrowed references (ordinary wrapped C++ object handles, same as any
// other wrapped pointer in this API) - not an owning smart pointer of any
// kind. SessionManager keeps its own owning reference internally for as
// long as it manages that transport/session, so there is nothing to
// release or Dispose() on the C# side here at all: once you have a
// Transport/Session, you can just call methods on it directly.
//
// A failed call can yield a null Transport and/or Session; check with an
// ordinary `== null` - SWIG's C# proxy classes already translate a null
// C++ pointer to a real C# null for every wrapped pointer return, so
// there is no C#-specific nil-translation wrapper needed here (unlike
// Go's own AddClientTransportRaw()-to-nil translation in quicr.i).
//
// SubscribeTrackHandler/PublishTrackHandler do NOT work the same way as
// Transport/Session: AddTrackHandler() below takes `handler` as a plain
// TrackHandler, then - entirely on the C++ side, inside
// SessionManager::AddTrackHandler()'s %extend body in quicr.i - builds a
// brand-new std::shared_ptr<TrackHandler> directly around that same raw
// pointer. Subscriber/Publisher below are director-enabled proxy
// objects, and every SWIG C# proxy - director or not - defaults to
// owning its underlying C++ object (Dispose()/finalize runs `delete` on
// it once nothing in C# references it anymore). That's a second,
// independent owner of the exact same pointer alongside C++'s new
// shared_ptr, and it will double-free the moment either one lets go
// first - the same bug Python hits (see "Double ownership crash" in
// main.py's own module docstring), with the same fix: call Disown()
// (this API's own hand-written equivalent of Python's __disown__() -
// see csharp/type_extensions.i) immediately after every
// AddTrackHandler() call, which flips off this proxy's own
// delete-on-Dispose flag so the shared_ptr C++ just built is left as the
// sole remaining owner.

using System;
using System.Threading;
using quicr;
// Plain namespace-scope free functions (quicr::operator==/< etc., renamed
// to TrackNamespaceEquals/TrackNamespaceLess in quicr.i) land as static
// methods on a proxy class that - because %module quicr and -namespace
// quicr name it identically - is *itself* named quicr, nested inside the
// quicr namespace `using quicr;` above already imports (i.e. its real
// full name is quicr.quicr). Plain `using quicr;` only brings *types*
// into unqualified scope, not a type's own static members, and merely
// writing `quicr.TrackNamespaceEquals(...)` doesn't work either - `quicr`
// at that position resolves to the *namespace* (always in scope, valid
// with no `using` at all), not the identically-named type nested inside
// it, so the compiler looks for a nested namespace/type called
// TrackNamespaceEquals and fails (a real, reproduced CS0234, not just a
// hypothetical). `using static` sidesteps the ambiguity entirely by
// requiring the fully-qualified quicr.quicr, once, right here.
using static quicr.quicr;

namespace qcsclient;

// Subscriber's ObjectReceived()/StatusChanged() are called directly by
// C++ (via a SWIG director) whenever the real quicr::SubscribeTrackHandler
// virtual methods of the same name fire on the underlying C++ object -
// this is a real C++ subclass under the hood (see SwigDirector_
// SubscribeTrackHandler in the generated wrap.cxx), constructed the
// moment `new Subscriber(...)` below runs SubscribeTrackHandler's own
// (protected, base-class-only) constructor.
//
// Unlike main.py's own Subscriber (Python's GIL means every director
// call already arrives with the GIL held for you), C# has no
// equivalent global lock to worry about - but these two methods are
// still invoked directly on whichever libquicr-internal C++ thread
// first notices there's a callback to make (a background packet-I/O
// thread, never the thread that called AddTrackHandler()), so don't
// assume either one runs on the same thread as Main().
sealed class Subscriber : SubscribeTrackHandler
{
    public int Received { get; private set; }

    public Subscriber(FullTrackName fullTrackName, byte priority, OptionalGroupOrder groupOrder)
        : base(fullTrackName, priority, groupOrder)
    {
    }

    public override void ObjectReceived(ObjectHeaders objectHeaders, byte[] data, OptionalStreamHeaderProperties streamMode)
    {
        Console.WriteLine($"subscriber: received object #{Received} group={objectHeaders.group_id} " +
                           $"object={objectHeaders.object_id} payload={System.Text.Encoding.UTF8.GetString(data)}");
        Received++;
    }

    public override void StatusChanged(SubscribeTrackHandler.Status status)
    {
        Console.WriteLine($"subscriber: status changed to {status}");
    }
}

// Publisher only overrides StatusChanged for a bit of visibility into the
// announce handshake; PublishObject() itself isn't virtual (see the
// PublishTrackHandler comment in quicr.i for why) - it's called directly
// on this instance, same as any other wrapped method.
sealed class Publisher : PublishTrackHandler
{
    public Publisher(FullTrackName fullTrackName, TrackMode trackMode, byte defaultPriority, uint defaultTtl)
        : base(fullTrackName, trackMode, defaultPriority, defaultTtl)
    {
    }

    public override void StatusChanged(PublishTrackHandler.Status status)
    {
        Console.WriteLine($"publisher: status changed to {status}");
    }
}

internal static class Program
{
    private static bool TransportStopped(TransportStatus status) => status == TransportStatus.kShutdown;

    private static bool SessionStopped(Session.Status status) => status == Session.Status.kNotConnected;

    // Builds a quicr.FullTrackName from a plain namespace tuple and a name
    // string. FullTrackName is an aggregate with no constructor of its own
    // (see track_name.h) - name_space/name are ordinary public fields set
    // via the usual properties, name via the SetNameBytes() convenience
    // %extend'd onto FullTrackName in quicr.i specifically so callers don't
    // have to build a ByteVector by hand one byte at a time.
    private static FullTrackName NewFullTrackName(string[] namespaceTuple, string name)
    {
        var ftn = new FullTrackName { name_space = new TrackNamespace(new StringVector(namespaceTuple)) };
        ftn.SetNameBytes(System.Text.Encoding.UTF8.GetBytes(name));
        return ftn;
    }

    // Builds the minimal quicr.ObjectHeaders PublishObject() needs:
    // group/object id, payload length, and status. priority/ttl/track_mode
    // are left as their default (empty) std::optional, the same as main.go/
    // main.py's own newObjectHeaders()/new_object_headers().
    private static ObjectHeaders NewObjectHeaders(ulong groupId, ulong objectId, int payloadLen)
    {
        return new ObjectHeaders
        {
            group_id = groupId,
            object_id = objectId,
            subgroup_id = 0,
            payload_length = (ulong)payloadLen,
            status = ObjectStatus.kAvailable,
        };
    }

    private static void GracefulDisconnect(Transport transport, Session session)
    {
        session.Disconnect();
        transport.Shutdown();

        var timeout = TimeSpan.FromSeconds(3);
        var deadline = DateTime.UtcNow + timeout;
        while (DateTime.UtcNow < deadline)
        {
            var tStatus = transport.Status();
            var sStatus = session.GetStatus();
            if (SessionStopped(sStatus) && TransportStopped(tStatus))
            {
                Console.WriteLine($"disconnected: transport.Status(): {(int)tStatus,-2} session.GetStatus(): {(int)sStatus,-2}");
                return;
            }
            Thread.Sleep(50);
        }

        var finalT = transport.Status();
        var finalS = session.GetStatus();
        Console.WriteLine($"transport/session did not reach a terminal state within {timeout.TotalSeconds}s, exiting anyway");
        Console.WriteLine($"disconnected: transport.Status(): {(int)finalT,-2} session.GetStatus(): {(int)finalS,-2}");
    }

    private static int Main(string[] args)
    {
        var track = "";
        var trackName = "";
        var publish = false;
        for (var i = 0; i < args.Length; i++)
        {
            switch (args[i])
            {
                case "--track":
                    track = args[++i];
                    break;
                case "--track_name":
                    trackName = args[++i];
                    break;
                case "--publish":
                    publish = true;
                    break;
            }
        }

        Console.WriteLine("libquicr SWIG C# example");

        var namespaceTuple = track.Split('/');
        var @namespace = new TrackNamespace(new StringVector(namespaceTuple));
        Console.WriteLine($"track namespace:      {@namespace.Str()}");

        var duplicate = new TrackNamespace(new StringVector(namespaceTuple));
        var different = new TrackNamespace(new StringVector(new[] { "conference", "room2" }));
        // See the `using static quicr.quicr;` comment at the top of this
        // file for why these are unqualified.
        Console.WriteLine($"namespace == itself:   {TrackNamespaceEquals(@namespace, duplicate)}");
        Console.WriteLine($"namespace == other:    {TrackNamespaceEquals(@namespace, different)}");
        Console.WriteLine($"namespace < other:     {TrackNamespaceLess(@namespace, different)}");

        var config = new ClientConfig
        {
            endpoint_id = "qcsclient",
            connect_uri = "moq://relay.us-west-2.m10x.org:33437",
        };
        Console.WriteLine($"client config:         endpoint_id={config.endpoint_id} connect_uri={config.connect_uri}");

        var manager = new SessionManager();

        // Unlike Go (which needs a hand-written AddClientTransport() Go
        // companion purely to unpack this pair into a native multi-return),
        // C# calls the real, %extend'd AddClientTransport() directly and
        // gets back a TransportSessionPtrPair that SWIG's own std::pair
        // support already exposes with .first/.second properties (matching
        // the real C++ member names, the same as Python's own pair.first/
        // pair.second - see quicr.i's %template(TransportSessionPtrPair)
        // comment, near session_manager.h).
        var pair = manager.AddClientTransport(config);
        var transport = pair.first;
        var session = pair.second;
        if (transport == null || session == null)
        {
            Console.Error.WriteLine("AddClientTransport returned a null transport/session");
            return 1;
        }

        var interrupted = false;
        Console.CancelKeyPress += (_, e) =>
        {
            e.Cancel = true;
            interrupted = true;
        };

        TrackHandler? handler = null;
        var handlersAdded = false;
        ulong nextObjectId = 0;

        Console.WriteLine("Connecting... (Ctrl+C to exit)");
        while (true)
        {
            for (var i = 0; i < 10 && !interrupted; i++)
            {
                Thread.Sleep(100);
            }

            if (interrupted)
            {
                Console.WriteLine("\ninterrupted, disconnecting...");
                if (handlersAdded && handler != null)
                {
                    manager.RemoveTrackHandler(session, handler);
                }
                GracefulDisconnect(transport, session);
                return 0;
            }

            var tStatus = transport.Status();
            var sStatus = session.GetStatus();

            if (!handlersAdded && sStatus == Session.Status.kReady)
            {
                Console.WriteLine($"transport.Status(): {(int)tStatus,-2} session.GetStatus(): {(int)sStatus,-2}");
                var fullTrackName = NewFullTrackName(namespaceTuple, trackName);
                if (publish)
                {
                    var publisher = new Publisher(fullTrackName, TrackMode.kStream, 0, 500); // priority, ttl (ms)
                    manager.AddTrackHandler(session, publisher);
                    publisher.Disown(); // see the "double ownership" comment at the top of this file
                    handler = publisher;
                }
                else
                {
                    var subscriber = new Subscriber(fullTrackName, 0, new OptionalGroupOrder()); // priority
                    manager.AddTrackHandler(session, subscriber);
                    subscriber.Disown(); // see the "double ownership" comment at the top of this file
                    handler = subscriber;
                }
                handlersAdded = true;
                Console.WriteLine("session ready: subscribe+publish handlers added");
            }

            if (handlersAdded && sStatus == Session.Status.kReady && publish && handler is PublishTrackHandler publishHandler)
            {
                var payload = System.Text.Encoding.UTF8.GetBytes($"hello #{nextObjectId}");
                var status = publishHandler.PublishObject(NewObjectHeaders(0, nextObjectId, payload.Length), payload);
                Console.WriteLine($"publisher: PublishObject(#{nextObjectId}) -> status {status}");
                nextObjectId++;
            }

            if (TransportStopped(tStatus) || SessionStopped(sStatus))
            {
                Console.WriteLine("transport/session reached a terminal state, exiting");
                return 0;
            }
        }
    }
}
