/* quicr.i */
%module quicr

/* SWIG cannot wrap nested classes/structs directly for most target
   languages (Warning 325), which then breaks base-class resolution for
   types that inherit from a nested type (Warning 402). Flattening makes
   SWIG treat nested types as ordinary top-level types. */
%feature("flatnested");

/* ---- Generic STL type maps --------------------------------------------
   These %include directives are language-agnostic: SWIG automatically
   resolves <std_string.i>/<std_vector.i>/<std_pair.i> against the active
   target language's own subdirectory of its library path (e.g.
   .../go/std_vector.i vs .../python/std_vector.i) based on which backend
   (-go, -python, ...) is passed on the command line, so this file itself
   never needs to name a specific language. */
%include <std_string.i>
%include <std_vector.i>
%include <std_pair.i>

/* std::string converts to/from the target language's native string type
   automatically via std_string.i above; std::vector<T> instead needs an
   explicit %template per instantiation actually used in the wrapped API,
   since C++ templates only exist once instantiated. */
%template(ByteVector) std::vector<uint8_t>;
%template(ByteVectorVector) std::vector<std::vector<uint8_t>>;
%template(StringVector) std::vector<std::string>;
%template(UInt64Vector) std::vector<uint64_t>;

/* NOTE ON ORDER: throughout this file, both the %{ %} raw-code block below
   and the %include directives further down deliberately appear in
   dependency order (e.g. track_name.h before connection.h/session.h), and
   every %ignore/%rename/%nodefaultctor directive is placed *before* the
   %include of the header it targets. SWIG resolves unqualified type names
   (e.g. `MetricsTimeStamp`/`ConnectionMetrics` used inside
   `namespace quicr { ... }`) and feature directives based on what it has
   already parsed, so a header that *uses* a type must be processed after
   the header that *defines* it, and a directive that modifies a
   declaration must be processed before that declaration - even though a
   real C++ compiler is order-agnostic here thanks to #pragma once and
   normal header/translation-unit semantics. Getting either kind of order
   wrong surfaces as "unknown type name"/"use of undeclared identifier"
   errors when compiling the generated wrapper (not as SWIG warnings), or as
   directives silently not applying, so both are easy to miss. */
%{
#include "quicr/config.h"
#include "quicr/track_name.h"
#include "quicr/transport_metrics.h"
#include "quicr/metrics.h"
#include "quicr/transport.h"
#include "quicr/attributes.h"
#include "quicr/handlers/track_handler.h"

/* Generated wrapper code re-emits struct member types using whatever
   partial qualification appears in the header (e.g. `messages::Foo`,
   valid C++ when written inside `namespace quicr { ... }` since
   `quicr::messages` is a child namespace). SWIG doesn't requalify these
   to `quicr::messages::Foo` when copying them into the global-scope
   wrapper functions it generates, so the emitted code fails to compile.
   A namespace alias at global scope gives the literal spelling SWIG
   emits a valid meaning without touching any library headers. */
namespace messages = quicr::messages;

/* Same requalification problem as `messages::Foo` above, but for a plain
   `using` alias declared directly inside `namespace quicr { ... }`
   (quicr/utilities/bytes.h): generated code for TrackNamespace's
   begin()/end() (which return Bytes::iterator) emits the bare `Bytes`
   token at global scope. A global alias resolves it the same way. */
using Bytes = quicr::Bytes;

#include "quicr/connection.h"
#include "quicr/session_manager.h"
#include "quicr/session.h"
%}

%include "quicr/config.h"

/* ---- std:: type-system stand-ins ---------------------------------------
   We deliberately avoid %include-ing the real STL headers (they pull in far
   more than SWIG can reasonably handle here). These minimal forward
   declarations only teach SWIG's type system enough to stop misreading our
   headers; nothing here is exposed to bindings. */

/* Tell SWIG that std::hash is a template. Without this, SWIG treats the
   `template<> struct std::hash<T>` specializations in track_name.h as
   "specializing a non-template" (Warning 317). The specializations
   themselves are a pure C++ implementation detail for unordered_map/set;
   nothing in them is useful to expose to bindings, so they're %ignore'd
   below rather than wrapped. */
namespace std {
    template<class T> struct hash;
}
%ignore std::hash<quicr::TrackNamespace>;
%ignore std::hash<quicr::FullTrackName>;

/* Assignment operators don't map to a useful idiom in most target
   languages (their native `=` on the generated proxy object never calls
   these anyway), so ignore them explicitly rather than letting SWIG emit
   an "operator= ignored" warning for something we never intended to wrap. */
%ignore quicr::TrackNamespace::operator=;
%ignore quicr::TrackHash::operator=;

/* Move constructor: most SWIG target languages have no C++ move-semantics
   concept; the copy constructor (kept) is used instead. Ignoring it
   explicitly silences the "shadowed by copy constructor" warning. */
%ignore quicr::TrackNamespace::TrackNamespace(TrackNamespace&&);

/* IsPrefixOf() returns std::partial_ordering, a C++20 comparison-category
   type with no default constructor and no meaningful equivalent in any
   SWIG target language; SWIG would otherwise generate a wrapper that
   default-constructs one, which fails to compile. */
%ignore quicr::TrackNamespace::IsPrefixOf;

/* Comparison operators on TrackNamespace are declared as `friend`
   non-member functions, so they live in ::quicr, not
   ::quicr::TrackNamespace, and SWIG can't wrap C++ operator syntax for any
   target language (Warning 503) - give them plain, portable identifiers
   usable from any target language instead of ignoring them outright. */
%rename(TrackNamespaceEquals) quicr::operator==(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceNotEquals) quicr::operator!=(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceLess) quicr::operator<(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceGreater) quicr::operator>(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceLessOrEqual) quicr::operator<=(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceGreaterOrEqual) quicr::operator>=(const TrackNamespace&, const TrackNamespace&);

/* TrackNamespace(const std::vector<std::vector<uint8_t>>&) and
   TrackNamespace(const std::vector<std::string>&) are both wrapped through
   %template'd vector proxy classes above; some target languages'
   overload-dispatch precedence can't tell those two proxy types apart
   (Warning 509, one silently shadows the other). Give the byte-vector
   overload its own name so both remain callable everywhere. */
%rename(FromByteEntries) quicr::TrackNamespace::TrackNamespace(const std::vector<std::vector<uint8_t>>&);

/* begin()/end() are each overloaded on const-ness (returning
   Bytes::iterator vs Bytes::const_iterator). Most target languages have no
   notion of a const-qualified overload to dispatch on, so SWIG always
   keeps the non-const version and drops the const one (Warning 516).
   Ignoring the const overloads explicitly documents that this is
   intentional rather than leaving it to SWIG's default tie-breaking. */
%ignore quicr::TrackNamespace::begin() const;
%ignore quicr::TrackNamespace::end() const;

%include "quicr/track_name.h"

/* Defaulted C++20 three-way comparisons are a C++-only convenience with no
   equivalent in the languages we bind to. */
%ignore quicr::MinMaxAvg::operator<=>;
%ignore quicr::QuicConnectionMetrics::operator<=>;
%ignore quicr::QuicDataContextMetrics::operator<=>;
%ignore quicr::UdpDataContextMetrics::operator<=>;

%include "quicr/transport_metrics.h"
%include "quicr/metrics.h"

/* TransportException derives from std::runtime_error (Warning 401 without
   this stand-in); we only need enough of the interface for SWIG's base-
   class resolution to succeed, not a full wrapping of std::exception. */
namespace std {
    class exception {};
    class runtime_error : public exception {
      public:
        runtime_error(const std::string&);
    };
}

/* StreamRxContext::data_queue is an internal SafeQueue<> used by the
   transport implementation. We don't %include containers/safe_queue.h
   (it's an implementation detail, not part of the public API surface we
   want in bindings), so SWIG has no type information for SafeQueue.
   Ignoring just this field avoids pulling that header in while leaving the
   rest of StreamRxContext wrapped normally. */
%ignore quicr::StreamRxContext::data_queue;

%include "quicr/transport.h"

/* These aggregate structs have `const` members with no default initializer,
   so they have no default constructor in real C++ (the compiler implicitly
   deletes it). SWIG doesn't notice this and generates a default-constructor
   wrapper anyway, which fails to compile. The structs remain usable as
   parameter/return types; only the (nonexistent) default constructor
   wrapper is suppressed. */
%nodefaultctor quicr::ClientSetupAttributes;
%nodefaultctor quicr::ServerSetupAttributes;
%nodefaultctor quicr::PublishOkAttributes;
%nodefaultctor quicr::PublishAttributes;

%include "quicr/attributes.h"

/* TrackHandler (base class of the various track/namespace handler types)
   and its subclasses (SubscribeTrackHandler, PublishTrackHandler,
   FetchTrackHandler, PublishFetchHandler, PublishNamespaceHandler,
   SubscribeNamespaceHandler) are not wrapped here: they pull in a large
   additional surface (caches, timeq, per-handler protocol state) that is
   out of scope for this interface today. Ignoring the whole class still
   lets us %include the real header below for the plain-data
   Request/Publish/Fetch/SubscribeNamespace response structs it declares
   alongside TrackHandler - those are used directly by several Session
   methods that remain wrapped (see %ignore list near session.h below for
   the methods that instead take/return a handler type and stay ignored). */
%ignore quicr::TrackHandler;

/* PublishResponse has a `const` member with no default initializer, same
   reasoning as the attributes.h structs above. */
%nodefaultctor quicr::PublishResponse;

%include "quicr/handlers/track_handler.h"

%include "quicr/connection.h"

/* SessionManager::AddTransport() takes std::function<...>&& callback
   parameters. SWIG has no built-in typemap for std::function or for
   rvalue references in any target language, so these overloads can't be
   bound correctly today. Ignore the raw overload and expose default-
   session-only convenience wrappers instead (nullptr callback == default
   Session::Create path, which is fully supported by the C++
   implementation). Real per-call callback support from bindings would
   require a %feature("director") interface in place of std::function. */
%ignore quicr::SessionManager::AddTransport;

%include "quicr/session_manager.h"

%template(TransportSessionPair) std::pair<std::shared_ptr<quicr::Transport>, std::shared_ptr<quicr::Session>>;

%extend quicr::SessionManager {
    std::pair<std::shared_ptr<quicr::Transport>, std::shared_ptr<quicr::Session>> AddClientTransport(
      const quicr::ClientConfig& config)
    {
        return $self->AddTransport(config);
    }

    std::shared_ptr<quicr::Transport> AddServerTransport(const quicr::ServerConfig& config)
    {
        return $self->AddTransport(config, nullptr, nullptr);
    }
}

/* Constructor/destructor are protected on the real
   std::enable_shared_from_this, exactly as declared here: without this,
   SWIG assumes both are public (the usual default) and generates a
   constructor/destructor wrapper that calls them directly, which fails to
   compile against the real STL type. Registering the %template also
   resolves Warning 401 ("Nothing known about base class
   std::enable_shared_from_this<Session>"). */
namespace quicr { class Session; }
namespace std {
    template<class T> class enable_shared_from_this {
      protected:
        enable_shared_from_this() = default;
        ~enable_shared_from_this() = default;
      public:
        std::shared_ptr<T> shared_from_this();
    };
}
%template(SessionEnableSharedFromThis) std::enable_shared_from_this<quicr::Session>;

/* Session methods below all take or return one of the un-wrapped handler
   types (TrackHandler is %ignore'd near quicr/handlers/track_handler.h's
   %include above). Wrapping them
   would require also wrapping SubscribeTrackHandler, PublishTrackHandler,
   FetchTrackHandler, PublishFetchHandler, PublishNamespaceHandler, and
   SubscribeNamespaceHandler, which is out of scope for this interface.
   Everything else on Session (connect/disconnect, status, the Resolve*()
   responses, and the callbacks that only carry plain attribute/response
   structs) remains wrapped normally. */
%ignore quicr::Session::SubscribeTrack;
%ignore quicr::Session::UnsubscribeTrack;
%ignore quicr::Session::UpdateTrackSubscription;
%ignore quicr::Session::PublishTrack;
%ignore quicr::Session::UnpublishTrack;
%ignore quicr::Session::PublishNamespace;
%ignore quicr::Session::PublishNamespaceDone;
%ignore quicr::Session::SubscribeNamespace;
%ignore quicr::Session::UnsubscribeNamespace;
%ignore quicr::Session::ResolvePublish;
%ignore quicr::Session::FetchTrack;
%ignore quicr::Session::CancelFetchTrack;
%ignore quicr::Session::BindPublisherTrack;
%ignore quicr::Session::UnbindPublisherTrack;
%ignore quicr::Session::BindFetchTrack;
%ignore quicr::Session::UnbindFetchTrack;
%ignore quicr::Session::PublishReceived;
%ignore quicr::Session::GetPubTrackHandler;
%ignore quicr::Session::RemoveSubscribeTrack;
%ignore quicr::Session::RemoveSubscribeNamespace;
%ignore quicr::Session::ClosePublishTrackLocal;

/* Session::SetWebTransportMode() is declared in session.h but has no
   definition anywhere in the library (not a wrapping limitation - a normal
   C++ build only notices this if something actually calls or takes the
   address of the method, which nothing in the existing code does).
   Wrapping it produces an unresolved-symbol link error. Ignored here until
   the method is implemented upstream. */
%ignore quicr::Session::SetWebTransportMode;

/* Both have `const` members with no default initializer (see the
   %nodefaultctor comment above for the same reasoning). */
%nodefaultctor quicr::Session::RequestUpdateResponse;
%nodefaultctor quicr::Session::RequestUpdateResponse::Error;

%include "quicr/session.h"
