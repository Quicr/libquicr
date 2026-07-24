/* Go-specific %extend/%feature/%rename/%insert additions - counterpart to
 * go/typemaps.i's %typemap/%fragment half. Only ever %include'd under
 * #ifdef SWIGGO in quicr.i.
 */

/* Go's code generator always capitalizes a %rename target's first letter,
 * so there's no way to make a renamed method unexported for Go
 * specifically. AddClientTransportRaw()/AddServerTransportRaw() (defined
 * as %extend methods in quicr.i) stay visible under these names on
 * purpose - AddClientTransport()/AddServerTransport() below are Go's own
 * friendlier wrappers around them.
 *
 * Must run before quicr.i's %extend quicr::SessionManager block defines
 * the methods being renamed - true regardless of where in this file it
 * sits, since this whole file is %include'd near the top of quicr.i. */
%rename(AddClientTransportRaw) quicr::SessionManager::AddClientTransport;
%rename(AddServerTransportRaw) quicr::SessionManager::AddServerTransport;

/* Global exception translation: without this, any C++ exception a
 * wrapped function throws (e.g. StreamHeaderProperties's constructor,
 * TrackNamespace's) propagates uncaught across the cgo boundary and
 * aborts the whole process instead of a recoverable Go panic - found via
 * StreamHeaderProperties, see swig/SWIG_WARNINGS.md. */
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        _swig_gopanic(e.what());
    }
}

/* Go's own idiomatic multi-return/nil-translating companions to the
 * Raw-renamed methods above. */
%insert(go_wrapper) %{
// AddClientTransport unpacks AddClientTransportRaw()'s TransportSessionPtrPair
// into Go's native (transport, session) multi-return, since Go's code
// generator can't return more than one value from an ordinary wrapped
// method. Transport/Session are plain, borrowed references, so there's
// nothing to release.
//
// A failed call can yield a nil Transport and/or Session; check with an
// ordinary `== nil`, not Swigcptr() == 0 - SWIG never returns a nil
// interface value on its own, even for a null C++ pointer, so this (and
// AddServerTransport below) does that translation for you.
func AddClientTransport(m SessionManager, config ClientConfig) (Transport, Session) {
	pair := m.AddClientTransportRaw(config)
	transport := pair.GetFirst()
	session := pair.GetSecond()
	if transport.Swigcptr() == 0 {
		transport = nil
	}
	if session.Swigcptr() == 0 {
		session = nil
	}
	return transport, session
}

// AddServerTransport is a thin wrapper over AddServerTransportRaw() that
// translates a null result to a real Go nil - see AddClientTransport's
// own doc comment above for why.
func AddServerTransport(m SessionManager, config ServerConfig) Transport {
	t := m.AddServerTransportRaw(config)
	if t.Swigcptr() == 0 {
		return nil
	}
	return t
}
%}
