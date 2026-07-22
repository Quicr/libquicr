/* quicr.i */
/* Directors let a target-language override of a C++ virtual method be
   called back into from C++ - needed below so Go can subclass
   SubscribeTrackHandler/PublishTrackHandler and override ObjectReceived()/
   StatusChanged()/MetricsSampled() directly (see the director feature
   directives near those classes further down). It's harmless for every
   other target language wired up in the future that doesn't ask for it via
   %feature("director") on a specific class - this flag only makes the
   *mechanism* available module-wide, it doesn't turn every virtual method
   in every class into a director on its own. */
%module(directors="1") quicr

%feature("flatnested");

%include <stdint.i>
%include <std_string.i>
%include <std_vector.i>
%include <std_pair.i>
%include <std_except.i>

/* std::string converts to/from the target language's native string type
   automatically via std_string.i above; std::vector<T> instead needs an
   explicit %template per instantiation actually used in the wrapped API,
   since C++ templates only exist once instantiated. */
%template(ByteVector) std::vector<uint8_t>;
%template(ByteVectorVector) std::vector<std::vector<uint8_t>>;
%template(StringVector) std::vector<std::string>;
%template(UInt64Vector) std::vector<uint64_t>;

/* quicr::Bytes (quicr/utilities/bytes.h: `using Bytes = std::vector<Byte>;`)
   is a plain alias, invisible to SWIG's own parser unless told about it
   here too (the real header is only ever %{ #include %}'d, never
   %include'd - see that block's own comment below) - session.h's
   PublishNamespaceResponse::error_reason field below is declared as
   std::optional<Bytes>, and without this SWIG resolves that per-field
   "Bytes" the same way it resolves every other type this file never
   declares to it (e.g. messages::ErrorCode): as an distinct, unrelated,
   totally opaque type of its own, *not* as this exact vector<uint8_t>
   already wrapped as ByteVector above. */
namespace quicr {
    using Bytes = std::vector<uint8_t>;
}

%{
#include "quicr/utilities/bytes.h"

#include "quicr/config.h"
#include "quicr/track_name.h"
#include "quicr/transport_metrics.h"
#include "quicr/metrics.h"
#include "quicr/transport.h"
#include "quicr/attributes.h"

#include "quicr/messages/ctrl_message_types.h"
#include "quicr/messages/object.h"
namespace messages = quicr::messages;

using Bytes = quicr::Bytes;
using BytesSpan = quicr::BytesSpan;

#include "quicr/handlers/track_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/handlers/publish_track_handler.h"

#include "quicr/connection.h"
#include "quicr/session_manager.h"
#include "quicr/session.h"
%}

%include "quicr/config.h"

/* Every Go-specific typemap quicr.i needs (const std::vector<std::string>&
   <-> []string, quicr::BytesSpan <-> []byte, ...) lives in its own file -
   see go_typemaps.i's own top-of-file comment for why - so this interface
   file itself never has to name a specific target language anywhere else. */
#ifdef SWIGGO
%include "go/type_extensions.i"
%include "go/typemaps.i"
#endif

/* ---- std:: type-system stand-ins ---------------------------------------
   We deliberately avoid %include-ing the real STL headers (they pull in far
   more than SWIG can reasonably handle here). These minimal forward
   declarations only teach SWIG's type system enough to stop misreading our
   headers; nothing here is exposed to bindings.
 */

/* ---- std::shared_ptr / std::enable_shared_from_this support -------------
   SWIG's Go module has no smart-pointer support at all (unlike Python/
   Java/C#/Ruby/etc., which get a ready-made %shared_ptr macro from
   std_shared_ptr.i): %feature("smartptr") - the mechanism those other
   languages rely on - has no effect on Go's own code generation. An
   earlier version of this file worked around that with a hand-rolled
   %go_shared_ptr macro that heap-allocated a std::shared_ptr<T> as Go's
   opaque handle, with a manual DeleteXxxPtr() to release it. That turned
   out to be exactly the kind of ceremony Go code should never need for a
   library like this one: every shared_ptr<Transport>/shared_ptr<Session>/
   shared_ptr<TrackHandler> this API actually hands out is *also* kept
   alive independently - Transport/Session by SessionManager's own
   transports_/sessions_ maps (see session_manager.h) for as long as the
   manager itself keeps it around, TrackHandler by the Session it was
   registered with (see SessionManager::AddTrackHandler()'s %extend below) for
   as long as it stays subscribed/published - which covers this API's
   entire useful surface. Go doesn't need to hold, count, or release a
   reference of its own at all: it just needs a valid pointer to call
   methods through, exactly like calling a method through a borrowed
   reference in C++ - so that's what the AddClientTransport/
   AddServerTransport/AddTrackHandler extend methods hand back or take instead,
   and there's nothing to delete, release, or garbage-collect on the Go
   side for any of them.

   This forward declaration has to come before quicr/handlers/
   track_handler.h's %include below (rather than staying right next to the
   AddClientTransport/AddServerTransport %extend block that actually uses
   it, further down near session_manager.h): TrackHandler itself derives
   from std::enable_shared_from_this<TrackHandler>, so SWIG needs to
   already know what that base class is by the time it parses TrackHandler's
   own declaration, the same "process in dependency order" rule the top-of-
   file comment describes for headers applies here too.

   Go is the only target language wired up in cmd/examples today, so
   trading away real shared_ptr/refcounting semantics for other SWIG
   target languages here (should one ever get wired up too) is an
   intentional, scoped tradeoff, not an oversight.
 */
namespace std {
    template<class T> class shared_ptr {};

    /* Constructor/destructor are protected on the real
       std::enable_shared_from_this, exactly as declared here: without
       this, SWIG assumes both are public (the usual default) and
       generates a constructor/destructor wrapper that calls them
       directly, which fails to compile against the real STL type. */
    template<class T> class enable_shared_from_this {
      protected:
        enable_shared_from_this() = default;
        ~enable_shared_from_this() = default;
      public:
        std::shared_ptr<T> shared_from_this();
    };
}

%ignore quicr::messages::Location::operator<=>;

%rename(Equals) quicr::messages::Location::operator==;

namespace quicr {
    namespace messages {
        enum struct GroupOrder : uint8_t
        {
            kAscending = 0x1,
            kDescending = 0x2
        };

        struct Location
        {
            std::uint64_t group{ 0 };
            std::uint64_t object{ 0 };

            bool operator==(const Location& other) const;
        };
    }
}

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

%rename("HasValue") has_value;
%rename("Value") value;
namespace std {
    template<class T> class optional {
      public:
        optional();
        optional(const T& value);
        bool has_value() const;
        const T& value() const;
    };
}
/* uint8_t/uint16_t/uint32_t/uint64_t/bool/std::chrono::milliseconds are
   *not* %template'd here: go/typemaps.i's %go_optional_scalar (applied to
   each of those further up, right after the go/typemaps.i %include near
   the top of this file) already gives std::optional<T> a real, native
   Go *T for every one of them - a plain nil-able pointer instead of yet
   another has_value()/value() wrapper class/interface - so there's
   nothing left for %template to usefully add for those specific T's. */
%template(OptionalString) std::optional<std::string>;
%template(OptionalGroupOrder) std::optional<quicr::messages::GroupOrder>;
%template(OptionalLocation) std::optional<quicr::messages::Location>;

%ignore quicr::TrackNamespace::operator=;
%ignore quicr::TrackHash::operator=;

%ignore quicr::TrackNamespace::TrackNamespace(TrackNamespace&&);

%ignore quicr::TrackNamespace::IsPrefixOf;

%rename(TrackNamespaceEquals) quicr::operator==(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceNotEquals) quicr::operator!=(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceLess) quicr::operator<(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceGreater) quicr::operator>(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceLessOrEqual) quicr::operator<=(const TrackNamespace&, const TrackNamespace&);
%rename(TrackNamespaceGreaterOrEqual) quicr::operator>=(const TrackNamespace&, const TrackNamespace&);

%rename(FromByteEntries) quicr::TrackNamespace::TrackNamespace(const std::vector<std::vector<uint8_t>>&);

%ignore quicr::TrackNamespace::begin() const;
%ignore quicr::TrackNamespace::end() const;

%include "quicr/track_name.h"

%extend quicr::FullTrackName {
    void SetNameBytes(quicr::BytesSpan bytes)
    {
        $self->name.assign(bytes.begin(), bytes.end());
    }
}

/* Defaulted C++20 three-way comparisons are a C++-only convenience with no
   equivalent in the languages we bind to. */
%ignore quicr::MinMaxAvg::operator<=>;
%ignore quicr::QuicConnectionMetrics::operator<=>;
%ignore quicr::QuicDataContextMetrics::operator<=>;
%ignore quicr::UdpDataContextMetrics::operator<=>;

%include "quicr/transport_metrics.h"

/* UdpConnectionMetrics/QuicConnectionMetrics/UdpDataContextMetrics/
   QuicDataContextMetrics are real classes (not scalars), so - unlike
   uint8_t/uint16_t/uint32_t/uint64_t/bool/std::chrono::milliseconds just
   above - they stay on the has_value()/value() wrapper-class pattern
   rather than %go_optional_scalar's native-pointer one: without one of
   these %template names, each would otherwise fall back to the same
   unusable, auto-mangled "Std_optional_Sl_..._Sg_" proxy name every other
   never-%template'd std::optional<T> in this file gets (e.g. see
   OptionalBytes/OptionalRequestUpdateResponseError further below). */
%template(OptionalUdpConnectionMetrics) std::optional<quicr::UdpConnectionMetrics>;
%template(OptionalQuicConnectionMetrics) std::optional<quicr::QuicConnectionMetrics>;
%template(OptionalUdpDataContextMetrics) std::optional<quicr::UdpDataContextMetrics>;
%template(OptionalQuicDataContextMetrics) std::optional<quicr::QuicDataContextMetrics>;

%include "quicr/metrics.h"

// Suppress Warning 401
namespace std {
    class exception {};
    class runtime_error : public exception {
      public:
        runtime_error(const std::string&);
    };
}

%ignore quicr::StreamRxContext::data_queue;

%include "quicr/transport.h"

%nodefaultctor quicr::ClientSetupAttributes;
%nodefaultctor quicr::ServerSetupAttributes;
%nodefaultctor quicr::PublishOkAttributes;
%nodefaultctor quicr::PublishAttributes;

%include "quicr/attributes.h"

/* TrackHandler (base class of SubscribeTrackHandler/PublishTrackHandler,
   wrapped further below with directors so Go can subclass either one) is
   wrapped here too, but only partially: RequestUpdateReceived(),
   RequestOkReceived(), and RequestError() all take a
   quicr::messages::Parameters (a std::variant-shaped parameter-list type
   declared in quicr/messages/parameters.h) or messages::ErrorCode, neither
   of which is declared to SWIG anywhere in this file - like
   messages::Filter used by SubscribeTrackHandler's real constructor
   further below, both are wire-protocol-detail types that are out of
   scope for this interface today (tracked in swig/SWIG_WARNINGS.md rather
   than fixed here). All three are only ever *called* by the library
   itself (in response to received MoQT control messages) - nothing here
   calls them - and SubscribeTrackHandler/PublishTrackHandler already
   override RequestUpdateReceived()/RequestOkReceived() with real, concrete
   (non-pure) bodies in the actual C++ headers regardless of what SWIG
   does or doesn't wrap, so ignoring the base class declarations doesn't
   leave any subclass abstract.

   RequestUpdateReceived()/RequestOkReceived() need one more thing beyond
   the plain %ignore below, though: both are declared `= 0` (pure virtual)
   in the real track_handler.h that's %include'd further down, and
   SubscribeTrackHandler/PublishTrackHandler are both %feature("director")
   classes (see further below) - and SWIG's director code generator
   walks the *entire* ancestor chain for pure virtuals to override, not
   just the director-enabled class's own declared methods, entirely
   independent of %ignore (%ignore only suppresses the ordinary Go-callable
   proxy wrapper, not this). Left alone, that generates a director
   override on SwigDirector_SubscribeTrackHandler/SwigDirector_
   PublishTrackHandler themselves that unconditionally calls
   _swig_gopanic("call to pure virtual function ...") - because there's no
   Go-side method for it to route to, since it was ignored - which then
   *shadows* SubscribeTrackHandler's/PublishTrackHandler's own real,
   concrete overrides of the exact same methods, turning every ordinary,
   successful subscribe/publish request-acknowledgment into a runtime
   panic instead of quietly working. %feature("nodirector") tells SWIG to
   skip generating a director override for these two specific methods
   entirely, so the director subclass's vtable slot falls straight through
   to whichever real, concrete override SubscribeTrackHandler/
   PublishTrackHandler already provides, exactly as if this class weren't
   director-enabled at all for just these two methods. RequestError() only
   needs the plain %ignore below: it has a real (non-pure) default body in
   TrackHandler itself, so SWIG never considers it "must be overridden to
   stay concrete" the way the other two are, and no director stub for it
   ever gets generated in the first place. */
%feature("nodirector") quicr::TrackHandler::RequestUpdateReceived;
%feature("nodirector") quicr::TrackHandler::RequestOkReceived;
%ignore quicr::TrackHandler::RequestUpdateReceived;
%ignore quicr::TrackHandler::RequestOkReceived;
%ignore quicr::TrackHandler::RequestError;

/* Both protected, and both use a smart-pointer-to-Session shape
   (std::shared_ptr<Session> / std::weak_ptr<Session>) that's never
   otherwise needed by anything wrapped here - std::weak_ptr in particular
   isn't declared to SWIG anywhere in this file, unlike shared_ptr just
   above. Session-derived code (the only real caller of either) reaches
   TrackHandler through friend access instead, so neither is part of any
   binding's actual usable surface even when TrackHandler itself isn't
   ignored. */
%ignore quicr::TrackHandler::SetTransport;
%ignore quicr::TrackHandler::GetSession;

/* PublishResponse has a `const` member with no default initializer, same
   reasoning as the attributes.h structs above. */
%nodefaultctor quicr::PublishResponse;

/* Resolves Warning 401 ("Nothing known about base class
   std::enable_shared_from_this<TrackHandler>"), the same way
   SessionEnableSharedFromThis further below does for Session - see the
   enable_shared_from_this forward declaration near the top of this file
   for why the constructor/destructor being protected there matters, and
   why this %template instantiation has to come *before* the %include
   below rather than after: TrackHandler's own declaration (in the real
   header about to be parsed) already reads
   `: public std::enable_shared_from_this<TrackHandler>`, so the
   instantiation for this specific T has to exist by then, not just the
   generic template pattern. Needed so SessionManager::AddTrackHandler()'s
   %extend (near session_manager.h below) can call
   handler->shared_from_this(); TrackHandler is otherwise never itself
   instantiated directly - only its SubscribeTrackHandler/
   PublishTrackHandler subclasses are, further below. Unlike Session
   (already forward-declared for SWIG by the time its own
   SessionEnableSharedFromThis instantiation further below needs it -
   session_manager.h's own `class Session;` is parsed first), nothing
   else this early in the file forward-declares TrackHandler yet, so it
   needs one of its own here too. */
namespace quicr {
    class TrackHandler;
}
%template(TrackHandlerEnableSharedFromThis) std::enable_shared_from_this<quicr::TrackHandler>;

%include "quicr/handlers/track_handler.h"

/* quicr::TrackMode/ObjectStatus/ObjectHeaders (quicr/messages/object.h) are
   the plain-data types SubscribeTrackHandler::ObjectReceived() and
   PublishTrackHandler::PublishObject() below use for object metadata -
   small and self-contained enough (unlike ctrl_message_types.h/
   messages.h) to %include directly rather than hand-mirror.
   ObjectHeaders::extensions/immutable_extensions are
   std::optional<Extensions>, and Extensions itself is a
   std::map<uint64_t, std::vector<std::vector<uint8_t>>> - std::map has no
   %include-able Go typemap the way std::vector does via std_vector.i
   above, and adding one is out of scope for this pass (tracked in
   swig/SWIG_WARNINGS.md as a follow-up rather than fixed here); both
   fields are ignored below rather than left to silently fall back to the
   same opaque-placeholder handling every other gap in this file gets
   called out for. */
%ignore quicr::ObjectHeaders::extensions;
%ignore quicr::ObjectHeaders::immutable_extensions;
%include "quicr/messages/object.h"
%template(OptionalTrackMode) std::optional<quicr::TrackMode>;

/* messages::StreamHeaderProperties (quicr/messages/messages.h) describes
   how a QUIC stream's subgroup header is framed. Real accessors would need
   messages.h's own SubgroupIdType enum and its throwing
   uint64_t-decoding constructor, and messages.h itself is 450+ lines of
   otherwise-unrelated wire-format detail - out of scope here, same
   reasoning as ctrl_message_types.h above. It only ever appears as a
   std::optional<StreamHeaderProperties> defaulted to std::nullopt in the
   three method signatures below (ObjectReceived()/PublishObject()'s
   trailing stream_mode parameter); this bare, deliberately opaque stand-in
   (no fields, no accessors) exists only so those signatures parse and the
   ObjectReceived() director override resolves correctly against the real
   class's vtable slot - it is NOT a real, usable type the way
   messages::GroupOrder/Location are (tracked in swig/SWIG_WARNINGS.md as
   a follow-up, not fixed here).

   The real struct's members are all `const` with no default member
   initializer and no user-declared no-arg constructor (same reasoning as
   the attributes.h structs elsewhere in this file), so it has no real
   default constructor either - %nodefaultctor suppresses the one SWIG
   would otherwise assume this empty stand-in has. */
%nodefaultctor quicr::messages::StreamHeaderProperties;
namespace quicr {
    namespace messages {
        class StreamHeaderProperties {};
    }
}
%template(OptionalStreamHeaderProperties) std::optional<quicr::messages::StreamHeaderProperties>;

%typemap(in) std::optional<quicr::messages::StreamHeaderProperties> ($&1_type argp)
%{
    argp = ($&1_ltype)$input;
    if (argp == NULL) {
        _swig_gopanic("Attempt to dereference null std::optional< quicr::messages::StreamHeaderProperties >");
    }
    $1.~optional();
    new (&$1) std::optional<quicr::messages::StreamHeaderProperties>(*argp);
%}

/* SubscribeTrackHandler/PublishTrackHandler are hand-declared here rather
   than %include-d from their real headers (quicr/handlers/
   subscribe_track_handler.h / publish_track_handler.h), for two reasons
   at once:

   1. Both real constructors take a messages::Filter (a std::variant of
      several wire-format filter types) and/or a JoiningFetch struct
      (itself containing a messages::Parameters) - neither declared to
      SWIG anywhere in this file, same reasoning as the RequestUpdateReceived/
      RequestOkReceived/RequestError ignores near TrackHandler above.
      Hand-declaring each constructor with only its *leading*
      parameters (the trailing filter/joining_fetch/publisher_initiated
      ones all have real defaults in the actual header) sidesteps both
      types entirely: the shorter call SWIG generates is still a real,
      valid way to invoke the real constructor - C++ itself substitutes
      the real defaults for whatever's left - so nothing here is a lie
      about the real API, just an intentionally partial view of it.

   2. Both classes need to be constructible from Go as their own director
      subclass (see %feature("director") below and the
      NewDirectorSubscribeTrackHandler/NewDirectorPublishTrackHandler
      constructors SWIG generates from this), not via the real Create()
      factories: Create() always constructs a plain
      SubscribeTrackHandler/PublishTrackHandler, never a director
      subclass, so it can never be the thing whose virtual method
      overrides Go actually receives callbacks through. Directors work by
      Go constructing the *subclass* directly - exactly the same
      "protected base constructor, callable via NewDirectorXxx despite
      being inaccessible via NewXxx" mechanism already proven out for
      TrackHandler's own base constructor. Create() itself, and every
      other constructor overload/method not mentioned below, is simply
      never declared to SWIG at all - the real classes are unaffected,
      there is just nothing here for SWIG to generate a wrapper from.

   The %feature("nodirector")'d RequestUpdateReceived() near TrackHandler
   above has one side effect here worth calling out: SWIG's own
   (director-generation-only) model of the class still considers
   RequestUpdateReceived() a pure virtual method with nothing able to
   override it - it doesn't know SubscribeTrackHandler's real, concrete
   override in the actual C++ header satisfies that requirement, since
   %nodirector deliberately kept SWIG from generating anything for that
   slot at all - so it emits Warning 517 ("Director class ... is
   abstract"). That's a false positive specific to SWIG's own
   director-completeness bookkeeping: the class is not actually abstract
   in real C++ (SubscribeTrackHandler's own header overrides it with a
   real body, same as RequestOkReceived), and this has already been
   proven out end-to-end (see the director-based Go example under
   cmd/examples/swig/go) - subscribe/publish/unsubscribe all work
   correctly through this exact class. */
%warnfilter(517) quicr::SubscribeTrackHandler;
%feature("director") quicr::SubscribeTrackHandler;
namespace quicr {
    class SubscribeTrackHandler : public TrackHandler {
      public:
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotConnected,
            kError,
            kNotAuthorized,
            kNotSubscribed,
            kPendingResponse,
            kSendingUnsubscribe,
            kPaused,
            kNewGroupRequested,
            kCancelled,
            kDoneByFin,
            kDoneByReset,
        };

      protected:
        SubscribeTrackHandler(const FullTrackName& full_track_name,
                              std::uint8_t priority,
                              std::optional<messages::GroupOrder> group_order);

      public:
        Status GetStatus() const noexcept;
        void SetPriority(uint8_t priority) noexcept;
        std::uint8_t GetPriority() const noexcept;
        std::optional<messages::GroupOrder> GetGroupOrder() const noexcept;
        std::optional<messages::Location> GetLatestLocation() const noexcept;
        std::optional<uint64_t> GetTrackAlias() const noexcept;
        bool IsPublisherInitiated() const noexcept;

        /* Overridable from Go - see %feature("director") above. Each
           real virtual method's parameter *types* must match exactly for
           the SwigDirector subclass to genuinely override (rather than
           overload/hide) the real vtable slot, so unlike the constructor
           above, none of these drop a trailing defaulted parameter -
           ObjectReceived()'s stream_mode is kept, just typed against the
           deliberately-opaque StreamHeaderProperties stand-in above. */
        virtual void ObjectReceived(const ObjectHeaders& object_headers,
                                    BytesSpan data,
                                    std::optional<messages::StreamHeaderProperties> stream_mode);
        virtual void StatusChanged(Status status);
        virtual void MetricsSampled(const SubscribeTrackMetrics& metrics);
    };
}

/* Same Warning 517 false positive as SubscribeTrackHandler above, and for
   the same reason: SWIG's director-completeness bookkeeping doesn't know
   PublishTrackHandler's real header already overrides
   RequestUpdateReceived() with a concrete body. */
%warnfilter(517) quicr::PublishTrackHandler;
%feature("director") quicr::PublishTrackHandler;
namespace quicr {
    class PublishTrackHandler : public TrackHandler {
      public:
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotConnected,
            kNotAnnounced,
            kPendingAnnounceResponse,
            kAnnounceNotAuthorized,
            kNoSubscribers,
            kUnsubscribed,
            kDoneByFin,
            kSendingUnannounce,
            kSubscriptionUpdated,
            kNewGroupRequested,
            kPendingPublishOk,
            kPaused,
        };

        enum class PublishObjectStatus : uint8_t
        {
            kOk = 0,
            kInternalError,
            kNotAuthorized,
            kNotAnnounced,
            kNoSubscribers,
            kObjectPayloadLengthExceeded,
            kPreviousObjectTruncated,
            kNoPreviousObject,
            kObjectDataComplete,
            kObjectContinuationDataNeeded,
            kObjectDataIncomplete,
            kObjectDataTooLarge,
            kPreviousObjectNotCompleteMustStartNewGroup,
            kPreviousObjectNotCompleteMustStartNewTrack,
            kPaused,
            kPendingPublishOk,
        };

      protected:
        PublishTrackHandler(const FullTrackName& full_track_name,
                            TrackMode track_mode,
                            uint8_t default_priority,
                            uint32_t default_ttl);

      public:
        Status GetStatus() const noexcept;
        bool CanPublish() const noexcept;
        std::optional<uint64_t> GetTrackAlias() const noexcept;
        messages::Location GetLargestLocation() const noexcept;

        /* Not overridable from Go - PublishObject() is *called* to
           publish, never overridden, so unlike SubscribeTrackHandler's
           ObjectReceived() above, dropping its trailing defaulted
           stream_mode parameter here is safe: this isn't declared
           `virtual`, so SWIG generates an ordinary "call this method"
           wrapper (`$self->PublishObject(...)`) rather than a director
           override, and C++'s own real default fills in the parameter
           this stub never mentions - real virtual dispatch still applies
           at runtime regardless of what SWIG thinks the method's
           virtual-ness is, so nothing is lost by omitting `virtual` on a
           method Go only ever calls. */
        PublishObjectStatus PublishObject(const ObjectHeaders& object_headers, BytesSpan data);

        /* Overridable from Go - see the parameter-matching note on
           SubscribeTrackHandler's own StatusChanged()/MetricsSampled()
           above; the same reasoning applies here. */
        virtual void StatusChanged(Status status);
        virtual void MetricsSampled(const PublishTrackMetrics& metrics);
    };
}

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

/* SessionManager::AddHandler()/RemoveHandler() both take a
   std::shared_ptr<Session>/std::shared_ptr<TrackHandler> - like every
   other shared_ptr<T> parameter/return in this file, that's an opaque,
   unusable handle via the generic std::shared_ptr forward declaration
   near the top of this file. Ignore both real overloads and expose
   raw-pointer %extend replacements instead (below, once TrackHandler's
   shared_from_this() is available) that build the real shared_ptr
   internally - the same "Go passes/receives a plain, borrowed pointer;
   the real shared_ptr bookkeeping happens entirely on the C++ side"
   pattern as AddClientTransport/AddServerTransport already use.

   Unlike AddClientTransport/AddServerTransport though, the %extend
   replacements below can't reuse these same two names
   (AddHandler/RemoveHandler): %ignore matches by qualified name, not by
   declaration, so an %ignore'd name stays unavailable even to a same-
   named %extend method added afterwards - the exact same gotcha
   GetTransportPtr()'s own comment further below calls out for
   GetTransport(). They're named AddTrackHandler()/RemoveTrackHandler()
   instead. */
%ignore quicr::SessionManager::AddHandler;
%ignore quicr::SessionManager::RemoveHandler;

%include "quicr/session_manager.h"

/* Go's code generator capitalizes the first letter of every %rename target
   it emits regardless of what case is actually given here (e.g. renaming
   to "addClientTransportRaw" still comes out as "AddClientTransportRaw"
   in the generated Go), so there is no way to make a %rename'd method
   unexported for Go specifically the way the free functions in the
   go_wrapper block below can be. That would only have mattered for
   hiding an unsafe path anyway, and there isn't one here - AddClientTransportRaw()/
   AddServerTransportRaw() below return the exact same plain, borrowed
   pointers their Go-friendly companions do, just without the pair-
   unpacking (AddClientTransportRaw) or nil-translation
   (AddServerTransportRaw) convenience - so both remain visible under
   their Raw names on purpose rather than being an implementation detail
   to hide. */
#ifdef SWIGGO
%rename(AddClientTransportRaw) quicr::SessionManager::AddClientTransport;
%rename(AddServerTransportRaw) quicr::SessionManager::AddServerTransport;
#endif
%extend quicr::SessionManager {
    /* Returns plain, borrowed pointers - see the %go_shared_ptr comment
       above for why that's safe here. For Go, renamed away to
       AddClientTransportRaw() and given a hand-written multi-return
       companion below (Go's code generator can't return more than the one
       TransportSessionPtrPair value an ordinary wrapped method would
       produce here - every other target language wired up to this file in
       the future can use TransportSessionPtrPair.GetFirst()/GetSecond()
       directly, e.g. via native tuple unpacking). */
    std::pair<quicr::Transport*, quicr::Session*> AddClientTransport(const quicr::ClientConfig& config)
    {
        auto transport_and_session = $self->AddTransport(config);
        return { transport_and_session.first.get(), transport_and_session.second.get() };
    }

    /* Returns a plain, borrowed pointer for the same reason. Renamed away
       to AddServerTransportRaw() for Go purely so the go_wrapper companion
       below can translate a null result to a real Go nil instead of
       making callers check Swigcptr() == 0 themselves - Go's code
       generator has no trouble with the single return value here on its
       own. */
    quicr::Transport* AddServerTransport(const quicr::ServerConfig& config)
    {
        return $self->AddTransport(config, nullptr, nullptr).get();
    }

    /* Takes plain, borrowed pointers for the same reason as
       AddClientTransport/AddServerTransport above - see the shared_ptr
       comment near the top of this file. Builds the real
       std::shared_ptr<TrackHandler> exactly once here, the same moment
       the real (C++-side) AddHandler() itself takes ownership of it (by
       storing a copy internally, transitively, via
       Session::SubscribeTrack()/PublishTrack()/etc.) - this %extend
       method is the *only* place a shared_ptr is ever constructed from a
       raw TrackHandler* anywhere in these bindings, which is exactly what
       enable_shared_from_this requires: constructing a second, unrelated
       shared_ptr from the same raw pointer later would be undefined
       behavior (a double free once both eventually reach zero), so
       RemoveTrackHandler() below deliberately reuses this same ownership
       via handler->shared_from_this() instead of repeating this
       constructor.

       `handler` is any of the SubscribeTrackHandler/PublishTrackHandler
       director subclasses Go constructs further below
       (NewDirectorSubscribeTrackHandler()/NewDirectorPublishTrackHandler())
       - passed here as their common TrackHandler* base, exactly like
       passing a derived class pointer to a base-class parameter in C++
       itself. */
    void AddTrackHandler(quicr::Session* session, quicr::TrackHandler* handler)
    {
        $self->AddHandler(session->shared_from_this(), std::shared_ptr<quicr::TrackHandler>(handler));
    }

    /* Safe only once AddTrackHandler() above has already run for this
       exact handler: handler->shared_from_this() here requires *some*
       shared_ptr to already own `handler`, which AddTrackHandler() is
       the one and only place that establishes (see its own comment
       above). Calling RemoveTrackHandler() for a handler that was never
       added, or calling it twice for the same one, is a caller error -
       the same category of mistake as double-closing a file handle -
       and will throw std::bad_weak_ptr (uncaught, so it terminates the
       process; there is no typemap(throws) for it here) rather than
       fail gracefully. */
    void RemoveTrackHandler(quicr::Session* session, quicr::TrackHandler* handler)
    {
        $self->RemoveHandler(session->shared_from_this(), handler->shared_from_this());
    }
}
%template(TransportSessionPtrPair) std::pair<quicr::Transport*, quicr::Session*>;

#ifdef SWIGGO
%insert(go_wrapper) %{
// AddClientTransport is the Go counterpart to every other language
// binding's SessionManager.AddClientTransport(): because Go's code
// generator can only return the single TransportSessionPtrPair value an
// ordinary SWIG-wrapped method would produce, this unpacks that pair by
// hand into Go's native (transport, session) multi-return. Transport and
// Session here are plain, borrowed references - see the %go_shared_ptr
// comment above for why that's safe - so there is nothing to release.
//
// A failed call (e.g. before a connection is established) can yield a nil
// Transport and/or Session; check with an ordinary `== nil`, not
// Swigcptr() == 0. SWIG's own generated code never produces a nil
// interface value on its own account - even for a null C++ pointer, it
// always returns a non-nil wrapper around that null pointer - so this
// function (and AddServerTransport below) does the translation to a real,
// idiomatic Go nil for you.
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

// AddServerTransport is the Go counterpart to every other language
// binding's SessionManager.AddServerTransport(): a thin wrapper that only
// exists to translate a null result to a real Go nil - see
// AddClientTransport's own doc comment above for why.
func AddServerTransport(m SessionManager, config ServerConfig) Transport {
	t := m.AddServerTransportRaw(config)
	if t.Swigcptr() == 0 {
		return nil
	}
	return t
}
%}
#endif

/* Registering this %template resolves Warning 401 ("Nothing known about
   base class std::enable_shared_from_this<Session>") - see the
   enable_shared_from_this forward declaration near the top of this file
   (right before quicr/handlers/track_handler.h's %include) for why both
   the constructor/destructor being protected there and this instantiation
   matter. */
%template(SessionEnableSharedFromThis) std::enable_shared_from_this<quicr::Session>;

/* Session methods below all take or return a std::shared_ptr<T> of one of
   TrackHandler's subclasses. TrackHandler itself is wrapped now (see its
   %include further above, and SubscribeTrackHandler/PublishTrackHandler
   just below it) - but only two of its subclasses are: SubscribeTrack()/
   PublishTrack() (and their Unsubscribe/Unpublish/Update counterparts)
   are exactly the ones SessionManager.AddTrackHandler()/
   RemoveTrackHandler() already cover generically (see their %extend near
   session_manager.h below, which calls these same two methods internally
   after a dynamic_pointer_cast) - through the same plain, borrowed
   TrackHandler* pointer that FetchTrackHandler/PublishNamespaceHandler/
   SubscribeNamespaceHandler don't have a wrapped equivalent for yet, so
   there is no working, Go-callable way to build one of those in the
   first place. Wrapping this set of Session methods directly, on top of
   AddTrackHandler()/RemoveTrackHandler() already covering
   Subscribe/PublishTrack, would just be a second, redundant way to do the
   same thing for the two handler kinds that already work, while still
   leaving Fetch/Namespace handlers unimplemented - so all of them stay
   ignored here, and Fetch/PublishNamespace/SubscribeNamespace handler
   support remains tracked as a follow-up in swig/SWIG_WARNINGS.md rather
   than fixed here. Everything else on Session (connect/disconnect,
   status, the Resolve*() responses, and the callbacks that only carry
   plain attribute/response structs) remains wrapped normally. */
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

/* Both PublishNamespaceResponse::error_reason (std::optional<Bytes>) and
   RequestUpdateResponse::error (std::optional<RequestUpdateResponse::
   Error>) are only fully known to SWIG once quicr/session.h above has
   actually been parsed (RequestUpdateResponse::Error is a type nested
   inside a class %include'd above, so unlike OptionalBytes there's no
   way to instantiate this one any earlier) - same has_value()/value()
   wrapper-class reasoning as OptionalUdpConnectionMetrics/etc. near
   transport_metrics.h above applies to both. */
%template(OptionalBytes) std::optional<quicr::Bytes>;
%template(OptionalRequestUpdateResponseError) std::optional<quicr::Session::RequestUpdateResponse::Error>;

/* GetTransport() itself (from the real header, returning
   `const std::shared_ptr<Transport>&`) stays wrapped as-is - like every
   other not-specifically-unwrapped shared_ptr<T> accessor in this API
   (e.g. GetConnection() above), it's just an opaque, largely unusable
   handle via the generic std::shared_ptr forward declaration near
   session_manager.h. GetTransportPtr() below is the one actually meant to
   be used: a plain, borrowed quicr::Transport* (see the %go_shared_ptr
   comment near session_manager.h for why that's safe) - it needs its own
   name rather than overriding GetTransport() directly because %ignore-ing
   the original to free up its name would also suppress this same-named
   %extend replacement (SWIG's %ignore matches by qualified name, not by
   declaration, so it doesn't distinguish which declaration to drop). */
%extend quicr::Session {
    quicr::Transport* GetTransportPtr() const { return $self->GetTransport().get(); }
}
