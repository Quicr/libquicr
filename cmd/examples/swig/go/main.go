// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

// qgoclient is a minimal smoke test for the SWIG-generated Go bindings
// (see swig/quicr.i). It exercises plain structs/strings/enums, the
// %extend'd SessionManager helpers, and - once connected - a full
// subscribe+publish round trip through a SWIG *director*: a Go struct
// (subscriber below) whose ObjectReceived/StatusChanged methods are called
// directly by C++ whenever quicr::SubscribeTrackHandler's real virtual
// methods fire, with no polling involved.
//
// AddClientTransport() returns the Transport/Session it creates as plain,
// borrowed references (ordinary Go interface values, same as any other
// wrapped C++ object in this API) - not an owning smart pointer of any
// kind. SessionManager keeps its own owning reference internally (see the
// %go_shared_ptr comment in quicr.i, near session_manager.h) for as long
// as it manages that transport/session, so there is nothing to release,
// delete, or garbage-collect on the Go side here at all: once you have a
// Transport/Session, you can just call methods on it directly.
//
// A failed call can yield a nil Transport and/or Session; check with an
// ordinary `== nil`, exactly like any other Go pointer/interface value -
// never Swigcptr() == 0, which is a raw implementation-detail escape
// hatch, not a "this is nil" idiom (AddClientTransport/AddServerTransport
// specifically translate a null C++ pointer to a real Go nil for you; see
// their doc comments in quicr.i for why that's not the SWIG-generated
// default behavior for wrapped pointers in general).
//
// SubscribeTrackHandler/PublishTrackHandler themselves work the same way:
// AddTrackHandler()/RemoveTrackHandler() also take/hand back plain,
// borrowed TrackHandler pointers (see their own doc comments in quicr.i,
// near session_manager.h), so the *subscriber/*publisher Go values below
// never need an explicit delete/release either - the underlying C++
// object's lifetime is entirely SessionManager's problem once handed
// over, exactly as with Transport/Session.
package main

import (
	"fmt"
	"os"
	"os/signal"
	"sync/atomic"
	"syscall"
	"time"

	"quicr"
)

// subscriber's methods are called directly by C++ (via a SWIG director)
// whenever the real quicr::SubscribeTrackHandler virtual methods of the
// same name fire - there's no interface to implement or base type to
// embed; SWIG's generated director dispatch (see
// _swig_DirectorInterfaceSubscribeTrackHandlerObjectReceived et al. in
// the generated quicr.go) just checks whether the value passed to
// NewDirectorSubscribeTrackHandler() happens to have a matching method,
// and calls it if so.
type subscriber struct {
	received atomic.Uint64
}

func (s *subscriber) ObjectReceived(headers quicr.ObjectHeaders, data []byte, _ quicr.Std_optional_Sl_quicr_messages_StreamHeaderProperties_Sg_) {
	n := s.received.Add(1)
	fmt.Printf("subscriber: received object #%d group=%d object=%d payload=%q\n",
		n, headers.GetGroup_id(), headers.GetObject_id(), string(data))
}

func (s *subscriber) StatusChanged(status quicr.QuicrSubscribeTrackHandlerStatus) {
	fmt.Printf("subscriber: status changed to %d\n", status)
}

// publisher only overrides StatusChanged for a bit of visibility into the
// announce handshake; PublishObject() itself isn't virtual (see the
// PublishTrackHandler comment in quicr.i for why) - it's called directly
// on the handler returned by NewDirectorPublishTrackHandler(), same as
// any other wrapped method.
type publisher struct{}

func (p *publisher) StatusChanged(status quicr.QuicrPublishTrackHandlerStatus) {
	fmt.Printf("publisher: status changed to %d\n", status)
}

func transportStopped(status quicr.QuicrTransportStatus) bool {
	switch status {
	case quicr.TransportStatus_kShutdown:
		return true
	default:
		return false
	}
}

func sessionStopped(status quicr.QuicrSessionStatus) bool {
	switch status {
	case quicr.SessionStatus_kNotConnected:
		return true
	default:
		return false
	}
}

// newFullTrackName builds a quicr.FullTrackName from a plain namespace
// tuple and a name string. FullTrackName is an aggregate with no
// constructor of its own (see track_name.h) - name_space/name are
// ordinary public fields set via the usual setters, name via the
// SetNameBytes() convenience %extend'd onto FullTrackName in quicr.i
// specifically so Go doesn't have to build a ByteVector by hand one byte
// at a time.
func newFullTrackName(namespaceTuple []string, name string) quicr.FullTrackName {
	ftn := quicr.NewFullTrackName()
	ftn.SetName_space(quicr.NewTrackNamespace(namespaceTuple))
	ftn.SetNameBytes([]byte(name))
	return ftn
}

// newObjectHeaders builds the minimal quicr.ObjectHeaders PublishObject()
// needs: group/object id, payload length, and status. priority/ttl/
// track_mode are left as their default (empty) std::optional - see the
// OptionalUInt8/OptionalUInt16/OptionalTrackMode typemap in quicr.i.
func newObjectHeaders(groupID, objectID uint64, payloadLen int) quicr.ObjectHeaders {
	headers := quicr.NewObjectHeaders()
	headers.SetGroup_id(groupID)
	headers.SetObject_id(objectID)
	headers.SetSubgroup_id(0)
	headers.SetPayload_length(uint64(payloadLen))
	headers.SetStatus(quicr.ObjectStatus_kAvailable)
	return headers
}

func main() {
	fmt.Println("libquicr SWIG Go example")

	namespace := quicr.NewTrackNamespace([]string{"conference", "room1"})
	fmt.Printf("track namespace:      %s\n", namespace.Str())

	duplicate := quicr.NewTrackNamespace([]string{"conference", "room1"})
	different := quicr.NewTrackNamespace([]string{"conference", "room2"})
	fmt.Printf("namespace == itself:   %v\n", quicr.TrackNamespaceEquals(namespace, duplicate))
	fmt.Printf("namespace == other:    %v\n", quicr.TrackNamespaceEquals(namespace, different))
	fmt.Printf("namespace < other:     %v\n", quicr.TrackNamespaceLess(namespace, different))

	config := quicr.NewClientConfig()
	config.SetEndpoint_id("qgoclient")
	config.SetConnect_uri("moq://relay.us-west-2.m10x.org:33437")
	fmt.Printf("client config:         endpoint_id=%s connect_uri=%s\n", config.GetEndpoint_id(), config.GetConnect_uri())

	manager := quicr.NewSessionManager()
	transport, session := quicr.AddClientTransport(manager, config)
	if transport == nil || session == nil {
		fmt.Fprintln(os.Stderr, "AddClientTransport returned a nil transport/session")
		os.Exit(1)
	}

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)

	ticker := time.NewTicker(time.Second)
	defer ticker.Stop()

	var (
		handlersAdded bool
		sub           *subscriber
		subHandler    quicr.SubscribeTrackHandler
		pubHandler    quicr.PublishTrackHandler
		nextObjectID  uint64
	)

	fmt.Println("Connecting... (Ctrl+C to exit)")
	for {
		select {
		case <-sigCh:
			fmt.Println("\ninterrupted, disconnecting...")
			if handlersAdded {
				removeTrackHandlers(manager, session, subHandler, pubHandler)
			}
			gracefulDisconnect(transport, session)
			return

		case <-ticker.C:
			tStatus := transport.Status()
			sStatus := session.GetStatus()
			fmt.Printf("transport.Status(): %-2d  session.GetStatus(): %-2d\n", tStatus, sStatus)

			if !handlersAdded && sStatus == quicr.SessionStatus_kReady {
				sub = &subscriber{}
				subHandler = quicr.NewDirectorSubscribeTrackHandler(
					sub,
					newFullTrackName([]string{"conference", "room1"}, "video"),
					0, // priority
					quicr.NewOptionalGroupOrder())
				pubHandler = quicr.NewDirectorPublishTrackHandler(
					&publisher{},
					newFullTrackName([]string{"conference", "room1"}, "video"),
					quicr.TrackMode_kStream,
					0,   // default priority
					500) // default ttl (ms)

				manager.AddTrackHandler(session, subHandler)
				manager.AddTrackHandler(session, pubHandler)
				handlersAdded = true
				fmt.Println("session ready: subscribe+publish handlers added")
			}

			if handlersAdded && sStatus == quicr.SessionStatus_kReady {
				payload := []byte(fmt.Sprintf("hello #%d", nextObjectID))
				status := pubHandler.PublishObject(newObjectHeaders(0, nextObjectID, len(payload)), payload)
				fmt.Printf("publisher: PublishObject(#%d) -> status %d\n", nextObjectID, status)
				nextObjectID++
			}

			if transportStopped(tStatus) || sessionStopped(sStatus) {
				fmt.Println("transport/session reached a terminal state, exiting")
				return
			}
		}
	}
}

func removeTrackHandlers(manager quicr.SessionManager, session quicr.Session, subHandler quicr.SubscribeTrackHandler, pubHandler quicr.PublishTrackHandler) {
	manager.RemoveTrackHandler(session, subHandler)
	manager.RemoveTrackHandler(session, pubHandler)
}

func gracefulDisconnect(transport quicr.Transport, session quicr.Session) {
	session.Disconnect()
	transport.Shutdown()

	const timeout = 3 * time.Second
	deadline := time.After(timeout)
	tick := time.NewTicker(50 * time.Millisecond)
	defer tick.Stop()

	for {
		select {
		case <-tick.C:
			tStatus := transport.Status()
			sStatus := session.GetStatus()
			if sessionStopped(sStatus) && transportStopped(tStatus) {
				fmt.Printf("disconnected: transport.Status(): %-2d  session.GetStatus(): %-2d\n", tStatus, sStatus)
				return
			}

		case <-deadline:
			tStatus := transport.Status()
			sStatus := session.GetStatus()
			fmt.Printf("transport/session did not reach a terminal state within %s, exiting anyway\n", timeout)
			fmt.Printf("disconnected: transport.Status(): %-2d  session.GetStatus(): %-2d\n", tStatus, sStatus)
			return
		}
	}
}
