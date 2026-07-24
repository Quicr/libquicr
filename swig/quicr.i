/* quicr.i */
/* Enables SWIG directors module-wide so target languages can subclass
   SubscribeTrackHandler/PublishTrackHandler and override their virtual
   callbacks (see %feature("director") further below). Harmless for any
   other class that doesn't opt in via its own %feature("director"). */
%module(directors="1") quicr

%feature("flatnested");

%include <stdint.i>
%include <std_string.i>
%include <std_vector.i>
%include <std_pair.i>
%include <std_except.i>

/* std::vector<T> needs an explicit %template per instantiation actually
   used below, since C++ templates only exist once instantiated. */
%template(ByteVector) std::vector<uint8_t>;
%template(ByteVectorVector) std::vector<std::vector<uint8_t>>;
%template(StringVector) std::vector<std::string>;
%template(UInt64Vector) std::vector<uint64_t>;

/* quicr::Bytes (std::vector<uint8_t>) is a plain alias, invisible to SWIG
   unless declared here too - without it, std::optional<Bytes> fields
   (e.g. PublishNamespaceResponse::error_reason) would resolve to an
   unrelated opaque type instead of this exact vector<uint8_t>
   (ByteVector, above). */
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

/* Language-specific %typemap/%fragment/%extend/%feature/%rename/%insert
   code lives under swig/<lang>/type_extensions.i and swig/<lang>/
   typemaps.i instead of here (see either file's own top-of-file comment,
   per language, for the split's own reasoning). */
#ifdef SWIGGO
%include "go/type_extensions.i"
%include "go/typemaps.i"
#endif

#ifdef SWIGPYTHON
%include "python/type_extensions.i"
%include "python/typemaps.i"
#endif

#ifdef SWIGCSHARP
%include "csharp/type_extensions.i"
%include "csharp/typemaps.i"
#endif

/* Minimal forward declarations teaching SWIG's type system just enough
   about a few std:: types to stop it misreading our headers, without
   %include-ing the real (much larger) STL headers. Nothing here is
   exposed to bindings. */

/* SWIG's Go module has no smart-pointer support (%feature("smartptr") is
   a no-op for Go's code generation). Every shared_ptr<Transport>/
   <Session>/<TrackHandler> this API hands out is also kept alive
   elsewhere (SessionManager's own maps, or the Session a TrackHandler is
   registered with), so callers never need to hold, count, or release a
   reference of their own - just a plain, borrowed pointer, exactly like
   AddClientTransport/AddServerTransport/AddTrackHandler hand back or take
   below. This forward declaration has to precede quicr/handlers/
   track_handler.h's %include: TrackHandler derives from
   std::enable_shared_from_this<TrackHandler>, so SWIG needs to already
   know that base class by the time it parses TrackHandler itself. */
namespace std {
    template<class T> class shared_ptr {};

    /* Constructor/destructor are protected on the real
       std::enable_shared_from_this; without declaring them protected
       here too, SWIG assumes both are public and generates a wrapper
       that fails to compile against the real STL type. */
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

        /* ctrl_message_types.h's real enum, forward-declared here (rather
           than %include-ing that ~700-line header) purely so
           TrackHandler::RequestError() below can be un-%ignore'd -
           messages::Parameters (used by RequestUpdateReceived()/
           RequestOkReceived() instead) is a much larger templated type
           and stays out of scope (tracked in swig/SWIG_WARNINGS.md). */
        enum class ErrorCode : uint64_t
        {
            kInternalError = 0x0,
            kUnauthorized = 0x1,
            kTimeout = 0x2,
            kNotSupported = 0x3,
            kMalformedAuthToken = 0x4,
            kExpiredAuthToken = 0x5,
            kDoesNotExist = 0x10,
            kInvalidRange = 0x11,
            kMalformedTrack = 0x12,
            kDuplicateSubscription = 0x19,
            kUninterested = 0x20,
            kPrefixOverlap = 0x30,
            kInvalidJoiningRequestId = 0x32,
        };
    }
}

/* Tell SWIG that std::hash is a template, so it doesn't treat the
   `template<> struct std::hash<T>` specializations in track_name.h as
   "specializing a non-template" (Warning 317). Those specializations are
   a pure C++ implementation detail for unordered_map/set, so they're
   %ignore'd below rather than wrapped. */
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
   *not* %template'd here: go/typemaps.i's %go_optional_scalar already
   gives std::optional<T> a native, nil-able Go *T for each of those,
   so there's nothing left for %template to usefully add. */
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

%ignore quicr::MinMaxAvg::operator<=>;
%ignore quicr::QuicConnectionMetrics::operator<=>;
%ignore quicr::QuicDataContextMetrics::operator<=>;
%ignore quicr::UdpDataContextMetrics::operator<=>;

%include "quicr/transport_metrics.h"

/* These are real classes, not scalars, so they stay on the
   has_value()/value() wrapper-class pattern rather than
   %go_optional_scalar's native-pointer one. Without a %template name
   each would fall back to an unusable, auto-mangled proxy class name
   (e.g. "Std_optional_Sl_..._Sg_"). */
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

/* TrackHandler is wrapped here, but only partially: RequestUpdateReceived()
   and RequestOkReceived() take messages::Parameters, a templated,
   wire-format-detail type not declared to SWIG (tracked in
   swig/SWIG_WARNINGS.md) - and are only ever called by the library
   itself, never by bindings - so they stay %ignore'd below.

   They're also pure virtual in the real header, and
   SubscribeTrackHandler/PublishTrackHandler (below) are directors - so
   without more, SWIG's director generator would still emit an override
   for them that panics on call, since there's no binding-side method to
   route to, shadowing the classes' own real, concrete overrides.
   %feature("nodirector") skips generating that override, letting real
   virtual dispatch use the concrete override instead. */
%feature("nodirector") quicr::TrackHandler::RequestUpdateReceived;
%feature("nodirector") quicr::TrackHandler::RequestOkReceived;
%ignore quicr::TrackHandler::RequestUpdateReceived;
%ignore quicr::TrackHandler::RequestOkReceived;

/* RequestError() only needs messages::ErrorCode (declared above), has a
   real default body (so it's not pure virtual, and never needed
   nodirector), and is called by the library on protocol-level request
   errors - overridable like ObjectReceived()/StatusChanged() below. */

/* Both protected, and both use a shared_ptr<Session>/weak_ptr<Session>
   shape not needed anywhere else in this file. Only Session (via friend
   access) needs them. */
%ignore quicr::TrackHandler::SetTransport;
%ignore quicr::TrackHandler::GetSession;

/* const member with no default initializer, same as the attributes.h
   structs above. */
%nodefaultctor quicr::PublishResponse;

/* Resolves Warning 401 for TrackHandler's own enable_shared_from_this
   base (same reasoning as the forward declaration near the top of this
   file). Must be instantiated before track_handler.h's %include parses
   TrackHandler's own `: public std::enable_shared_from_this<TrackHandler>`
   declaration. Needed so SessionManager::AddTrackHandler() below can call
   handler->shared_from_this(). */
namespace quicr {
    class TrackHandler;
}
%template(TrackHandlerEnableSharedFromThis) std::enable_shared_from_this<quicr::TrackHandler>;

%include "quicr/handlers/track_handler.h"

/* quicr::Extensions is a plain alias (`using Extensions =
   std::map<uint64_t, std::vector<std::vector<uint8_t>>>`), same
   forward-declare-then-%template story as quicr::Bytes above - needed so
   ObjectHeaders::extensions/immutable_extensions (std::optional<Extensions>
   fields, below) resolve to this exact wrapped map instead of an
   unrelated opaque type. The value type is already wrapped as
   ByteVectorVector. */
%include <std_map.i>
namespace quicr {
    using Extensions = std::map<uint64_t, std::vector<std::vector<uint8_t>>>;
}
%template(Extensions) std::map<uint64_t, std::vector<std::vector<uint8_t>>>;
%template(OptionalExtensions) std::optional<quicr::Extensions>;

%include "quicr/messages/object.h"
%template(OptionalTrackMode) std::optional<quicr::TrackMode>;

/* messages::StreamHeaderProperties (messages.h) describes how a stream
   header type byte decodes; hand-declared here (rather than %include'd)
   because messages.h as a whole pulls in far more wire-format detail
   than this interface wants to take on. All members are `const` with no
   default member initializer, so - like the real struct - this has no
   default constructor either. */
%nodefaultctor quicr::messages::StreamHeaderProperties;
namespace quicr {
    namespace messages {
        enum class SubgroupIdType : std::uint8_t
        {
            kIsZero = 0b00,
            kSetFromFirstObject = 0b01,
            kExplicit = 0b10,
            kReserved = 0b11
        };

        struct StreamHeaderProperties
        {
            const bool extensions;
            const SubgroupIdType subgroup_id_mode;
            const bool end_of_group;
            const bool default_priority;

            explicit StreamHeaderProperties(std::uint64_t type);
            StreamHeaderProperties(bool extensions,
                                   SubgroupIdType subgroup_id_mode,
                                   bool end_of_group,
                                   bool default_priority);

            std::uint64_t GetType() const;

            static bool IsValid(std::uint64_t type) noexcept;
        };
    }
}
%template(OptionalStreamHeaderProperties) std::optional<quicr::messages::StreamHeaderProperties>;

/* SWIG's default %typemap(in) for a plain by-value SWIGTYPE parameter
   hits a deleted-copy-assignment-operator error for this const-only
   type; fixed per language in go/typemaps.i and python/typemaps.i (see
   swig/SWIG_WARNINGS.md for the history). */

/* SubscribeTrackHandler/PublishTrackHandler are hand-declared here rather
   than %include'd from their real headers, for two reasons:

   1. Their real constructors also take a messages::Filter/JoiningFetch,
      neither declared to SWIG. The trailing parameters all have real
      defaults in the actual header, so omitting them here still invokes
      the real constructor correctly.

   2. Director subclasses must be constructed directly via
      NewDirectorSubscribeTrackHandler()/NewDirectorPublishTrackHandler(),
      not via the real Create() factories, which always construct a plain
      (non-director) instance. Create() and every other method not
      mentioned below is simply never declared to SWIG. */

/* The %feature("nodirector")'d RequestUpdateReceived() above leaves
   SWIG's director-completeness check thinking this class is still
   abstract (it doesn't know the real header's concrete override
   satisfies it), producing a false-positive Warning 517 ("Director class
   ... is abstract") - suppressed below. Already verified end-to-end via
   the Go example under cmd/examples/swig/go.

   Python's constructor generation additionally treats that same false
   "abstract" signal as real and needs an explicit
   %feature("notabstract") override (see python/type_extensions.i); Go's
   does not. */
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

        /* Overridable - see %feature("director") above. Parameter types
           must match the real virtual signature exactly for a director
           subclass to override (rather than overload/hide) it. */
        virtual void ObjectReceived(const ObjectHeaders& object_headers,
                                    BytesSpan data,
                                    std::optional<messages::StreamHeaderProperties> stream_mode);
        virtual void StatusChanged(Status status);
        virtual void MetricsSampled(const SubscribeTrackMetrics& metrics);
    };
}

/* Same Warning 517 false positive as SubscribeTrackHandler above, and
   the same Python-only %feature("notabstract") consequence - see
   python/type_extensions.i. */
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

        /* Not overridable - PublishObject() is only ever called, never
           overridden, so dropping its trailing defaulted stream_mode
           parameter here is safe; real virtual dispatch still applies. */
        PublishObjectStatus PublishObject(const ObjectHeaders& object_headers, BytesSpan data);

        /* Overridable - see ObjectReceived()'s note above. */
        virtual void StatusChanged(Status status);
        virtual void MetricsSampled(const PublishTrackMetrics& metrics);
    };
}

/* messages::FetchEndLocation (ctrl_message_types.h) - a plain aggregate,
   needed by FetchTrackHandler below. Unlike messages::Location, this one
   is never renamed/%ignore'd, so it's declared directly rather than
   alongside Location/GroupOrder/ErrorCode near the top of this file. */
namespace quicr {
    namespace messages {
        struct FetchEndLocation
        {
            std::uint64_t group{ 0 };
            std::optional<std::uint64_t> object;
        };
    }
}

/* Error result shape shared by PublishNamespaceHandler/
   SubscribeNamespaceHandler below (each real header spells this out
   itself as a nested `using Error = std::pair<messages::ErrorCode,
   Bytes>;`; declared once here instead since both instantiate the exact
   same pair). */
%template(NamespaceHandlerError) std::pair<quicr::messages::ErrorCode, quicr::Bytes>;
%template(OptionalNamespaceHandlerError) std::optional<std::pair<quicr::messages::ErrorCode, quicr::Bytes> >;

/* FetchTrackHandler/PublishNamespaceHandler/SubscribeNamespaceHandler
   fill the last "deliberate, tracked gap" in swig/SWIG_WARNINGS.md:
   Session::FetchTrack()/PublishNamespace()/SubscribeNamespace() (and
   their Cancel/Done/Un- counterparts) stay %ignore'd below regardless -
   they take a std::shared_ptr<Handler> directly, an opaque type here -
   but SessionManager::AddHandler()/RemoveHandler() (wrapped generically
   as AddTrackHandler()/RemoveTrackHandler() near session_manager.h
   below) already dynamic_pointer_cast-dispatch to all three of these
   from a plain TrackHandler*, exactly like Subscribe/PublishTrackHandler
   - so wrapping the handler classes themselves is all that's needed to
   reach Session's own methods indirectly.

   Each hand-declared for the same reasons as Subscribe/PublishTrackHandler
   above: a protected constructor with trailing parameters that aren't
   declared to SWIG (messages::Filter for SubscribeNamespaceHandler; both
   real defaults still apply since they're only ever omitted, never
   overridden), and director subclasses needing their own
   NewDirectorXxx() rather than the real Create() factories.

   PublishTrack()/UnPublishTrack()/PublishObject()/ForwardPublishedData()
   on PublishNamespaceHandler (passthrough helpers to child
   PublishTrackHandlers) are deliberately not declared below - out of
   scope for this pass, same shared_ptr-from-raw-pointer ownership
   question AddTrackHandler() already had to solve once (see
   swig/SWIG_WARNINGS.md); registering/observing the namespace handler
   itself via AddTrackHandler()/RemoveTrackHandler() below doesn't need
   them. */
%warnfilter(517) quicr::FetchTrackHandler;
%feature("director") quicr::FetchTrackHandler;
namespace quicr {
    class FetchTrackHandler : public SubscribeTrackHandler {
      protected:
        FetchTrackHandler(const FullTrackName& full_track_name,
                          std::uint8_t priority,
                          std::optional<messages::GroupOrder> group_order,
                          const messages::Location& start_location,
                          const messages::FetchEndLocation& end_location);

      public:
        const messages::Location& GetStartLocation() const noexcept;
        const messages::FetchEndLocation& GetEndLocation() const noexcept;
    };
}

%warnfilter(517) quicr::PublishNamespaceHandler;
%feature("director") quicr::PublishNamespaceHandler;
namespace quicr {
    class PublishNamespaceHandler : public TrackHandler {
      public:
        enum class Status : uint8_t
        {
            kOk = 0,
            kNotConnected,
            kNotPublished,
            kPendingResponse,
            kPublishNotAuthorized,
            kSendingDone, ///< In this state, callbacks will not be called
            kError,
        };

      protected:
        PublishNamespaceHandler(const TrackNamespace& prefix);

        /* Protected in the real header too - overridable via director,
           not directly callable (same StatusChanged()-is-protected shape
           SWIG's director support handles fine for a virtual method). */
        virtual void StatusChanged(Status status);

      public:
        const TrackNamespace& GetPrefix() const noexcept;
        Status GetStatus() const noexcept;
        std::optional<std::pair<messages::ErrorCode, Bytes> > GetError() const noexcept;
    };
}

%warnfilter(517) quicr::SubscribeNamespaceHandler;
%feature("director") quicr::SubscribeNamespaceHandler;
namespace quicr {
    class SubscribeNamespaceHandler : public TrackHandler {
      public:
        enum class Mode
        {
            kNamespaces,
            kTracks
        };

        enum class Status : uint8_t
        {
            kOk = 0,
            kNotSubscribed,
            kError,
        };

      protected:
        SubscribeNamespaceHandler(const TrackNamespace& prefix, Mode mode);

      public:
        virtual void StatusChanged(Status status);
        const TrackNamespace& GetPrefix() const noexcept;
        Status GetStatus() const noexcept;
        std::optional<std::pair<messages::ErrorCode, Bytes> > GetError() const noexcept;
        Mode GetMode() const noexcept;
    };
}

%include "quicr/connection.h"

/* AddTransport() takes std::function<...>&& callback parameters, which
   SWIG can't bind in any target language. Ignored in favor of the
   default-Session %extend wrappers below (nullptr callback == default
   Session::Create path). */
%ignore quicr::SessionManager::AddTransport;

/* AddHandler()/RemoveHandler() take a std::shared_ptr<Session>/
   <TrackHandler> - an opaque handle here (see the shared_ptr forward
   declaration near the top of this file). Ignored in favor of
   raw-pointer %extend replacements below, named AddTrackHandler()/
   RemoveTrackHandler() since %ignore matches by qualified name and would
   also hide a same-named %extend replacement. */
%ignore quicr::SessionManager::AddHandler;
%ignore quicr::SessionManager::RemoveHandler;

%include "quicr/session_manager.h"

/* Go renames the two %extend methods below to AddClientTransportRaw()/
   AddServerTransportRaw() and adds its own idiomatic multi-return/
   nil-translating wrapper (see go/type_extensions.i). Every other
   language calls AddClientTransport()/AddServerTransport() directly. */
%extend quicr::SessionManager {
    /* Returns plain, borrowed pointers - see the shared_ptr comment near
       the top of this file. Other languages can unpack the pair via
       GetFirst()/GetSecond(). */
    std::pair<quicr::Transport*, quicr::Session*> AddClientTransport(const quicr::ClientConfig& config)
    {
        auto transport_and_session = $self->AddTransport(config);
        return { transport_and_session.first.get(), transport_and_session.second.get() };
    }

    /* Returns a plain, borrowed pointer, same reasoning. */
    quicr::Transport* AddServerTransport(const quicr::ServerConfig& config)
    {
        return $self->AddTransport(config, nullptr, nullptr).get();
    }

    /* Builds the real shared_ptr<TrackHandler> exactly once, at the same
       point the real AddHandler() itself takes ownership of it -
       constructing a second, unrelated shared_ptr from the same raw
       pointer later would double-free once both reach zero, so
       RemoveTrackHandler() below reuses this same ownership via
       shared_from_this() instead. `handler` is a Subscribe/PublishTrackHandler,
       Fetch/PublishNamespace/SubscribeNamespaceHandler director subclass,
       passed via its common TrackHandler* base - the real AddHandler()
       dynamic_pointer_casts to the concrete type itself and routes to
       the matching Session::SubscribeTrack()/PublishTrack()/FetchTrack()/
       PublishNamespace()/SubscribeNamespace(), so this one wrapper covers
       all five. */
    void AddTrackHandler(quicr::Session* session, quicr::TrackHandler* handler)
    {
        $self->AddHandler(session->shared_from_this(), std::shared_ptr<quicr::TrackHandler>(handler));
    }

    /* Only safe once AddTrackHandler() above has already run for this
       exact handler - shared_from_this() requires an existing shared_ptr
       to already own it. Removing a handler that was never added, or
       removing it twice, throws std::bad_weak_ptr (uncaught). */
    void RemoveTrackHandler(quicr::Session* session, quicr::TrackHandler* handler)
    {
        $self->RemoveHandler(session->shared_from_this(), handler->shared_from_this());
    }
}
%template(TransportSessionPtrPair) std::pair<quicr::Transport*, quicr::Session*>;

/* Go's own AddClientTransport()/AddServerTransport() wrapper functions
   are inserted via go/type_extensions.i's %insert(go_wrapper), not here. */

/* Resolves Warning 401 for Session's own enable_shared_from_this base -
   same reasoning as TrackHandlerEnableSharedFromThis above. */
%template(SessionEnableSharedFromThis) std::enable_shared_from_this<quicr::Session>;

/* SubscribeTrack()/PublishTrack()/FetchTrack()/PublishNamespace()/
   SubscribeNamespace() (and their Unsubscribe/Unpublish/Update/Cancel/
   Done counterparts) all take a std::shared_ptr<Handler> directly - an
   opaque type here (see the shared_ptr forward declaration near the top
   of this file) - so all of these stay ignored in favor of
   AddTrackHandler()/RemoveTrackHandler() above, which already covers
   all five handler kinds generically via a raw TrackHandler* and the
   real AddHandler()'s own dynamic_pointer_cast dispatch. Everything else
   on Session (connect/disconnect, status, Resolve*() responses, plain
   attribute/response callbacks) remains wrapped normally. */
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

/* Declared in session.h but never defined anywhere in the library;
   wrapping it produces an unresolved-symbol link error. */
%ignore quicr::Session::SetWebTransportMode;

/* const members, no default initializer - same as the attributes.h
   structs above. */
%nodefaultctor quicr::Session::RequestUpdateResponse;
%nodefaultctor quicr::Session::RequestUpdateResponse::Error;

%include "quicr/session.h"

/* Both only fully known to SWIG once session.h above has been parsed
   (RequestUpdateResponse::Error is nested inside a class %include'd
   above). Same has_value()/value() wrapper-class reasoning as
   OptionalUdpConnectionMetrics/etc. near transport_metrics.h. */
%template(OptionalBytes) std::optional<quicr::Bytes>;
%template(OptionalRequestUpdateResponseError) std::optional<quicr::Session::RequestUpdateResponse::Error>;

/* GetTransport() stays wrapped as-is - an opaque shared_ptr<Transport>
   handle, same as every other not-specifically-unwrapped shared_ptr<T>
   accessor. GetTransportPtr() below is the actually-usable plain,
   borrowed pointer; it needs its own name since %ignore-ing
   GetTransport() would also hide this same-named %extend replacement. */
%extend quicr::Session {
    quicr::Transport* GetTransportPtr() const { return $self->GetTransport().get(); }
}
