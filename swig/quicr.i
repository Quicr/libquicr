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

/* SWIG cannot wrap nested classes/structs directly for most target
   languages (Warning 325), which then breaks base-class resolution for
   types that inherit from a nested type (Warning 402). Flattening makes
   SWIG treat nested types as ordinary top-level types. */
%feature("flatnested");

/* <stdint.i> is SWIG's own official, language-agnostic library file
   declaring the real typedef chain behind the ISO C99 fixed-width integer
   types (uint8_t/uint16_t/uint32_t/uint64_t/...) - the same kind of ready-
   made building block <std_string.i>/<std_vector.i>/<std_pair.i> below
   are. It has to come before every other %include/%template in this
   section (in fact, before anything anywhere in this file that names one
   of these types, direcly or via a template argument): std::vector<uint8_t>
   right below, for instance, needs SWIG to already know what uint8_t
   *is* while it's being instantiated, not after - getting this backwards
   doesn't produce a warning or an error, it just silently leaves
   std::vector<uint8_t>::Get()/Set() (and every other already-templated
   use elsewhere below) resolved against SWIG's generic, otherwise-
   unusable placeholder handling instead of a real Go integer, exactly
   like every other ordering mistake this file's own top-of-file comment
   warns about. */
%include <stdint.i>

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

/* Go has no built-in typemap for std::vector<T> the way std_string.i gives
   std::string (that's what the StringVector proxy above is for), so
   without this, Go callers constructing a TrackNamespace from labels have
   to build a StringVector by hand and Add() each one. This typemap lets
   them instead pass a native []string directly wherever
   const std::vector<std::string>& is expected. It only needs to exist for
   Go: every other target language here already gets this for free from
   std_vector.i's own per-language typemaps (e.g. Python converts a plain
   list automatically), so guard it to avoid changing their behavior.

   It relies on two facts about SWIG's Go backend: a Go []string argument
   arrives in the wrapper as a _goslice_ (`{ void* array; intgo len; intgo
   cap; }`), and Go's native string header is `{ pointer; length }` - the
   same layout as SWIG's own _gostring_ (`{ char *p; intgo n; }`). That
   makes a []string's backing array exactly an array of _gostring_, so it
   can be cast and read directly here with no callback into Go needed. */
#ifdef SWIGGO
%typemap(gotype) const std::vector<std::string>& "[]string"

%typemap(in) const std::vector<std::string>& (std::vector<std::string> temp) {
    intgo i;
    const _gostring_* elems = (const _gostring_*)$input.array;
    temp.reserve((size_t)$input.len);
    for (i = 0; i < (intgo)$input.len; ++i) {
        temp.push_back(std::string(elems[i].p, (size_t)elems[i].n));
    }
    $1 = &temp;
}
#endif

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
#include "quicr/messages/object.h"
#include "quicr/handlers/track_handler.h"
#include "quicr/handlers/subscribe_track_handler.h"
#include "quicr/handlers/publish_track_handler.h"

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

/* Same requalification problem as `Bytes` just above, but for
   quicr::BytesSpan (quicr/utilities/bytes.h): SubscribeTrackHandler/
   PublishTrackHandler (hand-declared further below, inside
   `namespace quicr { ... }`) use the bare `BytesSpan` spelling, which
   SWIG re-emits verbatim at global scope in the generated wrapper. */
using BytesSpan = quicr::BytesSpan;

#include "quicr/connection.h"
#include "quicr/session_manager.h"
#include "quicr/session.h"
%}

%include "quicr/config.h"

/* quicr::BytesSpan (quicr/utilities/bytes.h: `using BytesSpan =
   std::span<const Byte>;`) is the payload type for every publish/receive
   object callback (PublishTrackHandler::PublishObject(),
   SubscribeTrackHandler::ObjectReceived()/PartialObjectReceived()). SWIG's
   Go module has no notion of std::span at all (it isn't declared to SWIG
   anywhere in this file - std::span's own template shape can't be taught
   to SWIG the way std::vector/std::string/std::optional above are,
   because unlike those, SWIG can't generate a constructor/no-arg-ctor
   wrapper for a *non-owning view* type). Both typemaps below key off this
   exact type spelling and marshal it by hand instead:
     - `gotype`/`in` (Go calling PublishObject(): a []byte the caller
       already owns) reads directly out of the incoming _goslice_ the same
       way the []string typemap above reads a _gostring_ array - no copy,
       no callback into Go, since the BytesSpan only needs to be valid for
       the duration of the call.
     - `directorin` (C++ calling ObjectReceived() on a Go-overridden
       director - see %feature("director") near SubscribeTrackHandler
       below) is the reverse direction and can't avoid a copy: the C++
       object backing the span is only guaranteed valid for the duration
       of this one call (see ObjectReceived's own "invalidated after
       return" warning), but the Go slice handed to the override can
       outlive it (e.g. if the Go code stores it for later), so the data
       has to be copied into its own allocation - std_string.i's
       string/AllocateString/CopyString pattern (near the []string
       typemap's own comment, above %include "quicr/config.h") is doing
       exactly this same "copy into a language-agnostic {ptr,len} struct
       now, let the other side turn it into a real, independently-owned
       value later" dance, just for a std::string's `_gostring_` instead
       of a byte slice's `_goslice_` - both structs share the same
       {pointer, length}-headed layout SWIG's Go runtime already relies
       on, so the fragments below mirror AllocateString/CopyString/
       swigCopyString almost verbatim.

   NOTE: unlike the []string typemap above, this isn't guarded with
   #ifdef SWIGGO/#endif - every %typemap key used below (gotype,
   directorin, godirectorin) is itself already Go-specific by name (a
   non-Go backend simply never looks at a "gotype"/"godirectorin"
   typemap), with the sole exception of the plain %typemap(in), which
   only ever fires for this exact quicr::BytesSpan type regardless of
   target language and simply wouldn't be reached by a non-Go backend
   generating its own (different) typemap for the same type first. Go is
   the only target language wired up in cmd/examples today regardless -
   see the shared_ptr comment further below for the same reasoning
   applied there.

   This has to live outside (after) the %{ %} raw preamble block just
   above: that block is pasted verbatim into the generated wrapper's C++
   preamble, so any %typemap/%fragment directive placed inside it (as
   this one originally was) is read as *literal text*, not a SWIG
   directive - its embedded %{ %} fragment bodies then get misread as the
   raw block's own closing %}, corrupting the rest of the file's #ifdef/
   %{/%} bookkeeping well past this point.

   The bare forward declaration below (never completed into a real class
   anywhere) matters just as much as the typemaps themselves: without it,
   SWIG has never seen *any* declaration - typedef, class, or otherwise -
   naming quicr::BytesSpan before ObjectReceived()/PublishObject() use it
   further down, and (same "opaque, otherwise-unusable placeholder"
   fallback documented throughout this file for every other not-yet-
   taught type) auto-invents its own unknown-type handling that never
   consults a same-named %typemap at all - the exact same reason
   std_string.i itself forward-declares `class string;` right next to its
   own %typemap(gotype) for the same reason, one directory up in SWIG's
   own library. */
namespace quicr {
    class BytesSpan;
}
%typemap(gotype) quicr::BytesSpan "[]byte"

%typemap(in) quicr::BytesSpan {
    $1 = quicr::BytesSpan(static_cast<const uint8_t*>($input.array), (size_t)$input.len);
}

%fragment("AllocateByteSlice", "runtime") %{
static _goslice_ Swig_AllocateByteSlice(const void *p, size_t l) {
  _goslice_ ret;
  ret.array = malloc(l);
  memcpy(ret.array, p, l);
  ret.len = (intgo)l;
  ret.cap = (intgo)l;
  return ret;
}
%}

%fragment("CopyByteSlice", "go_runtime") %{
type swig_goslice_hdr struct { array uintptr; len int; cap int }
func swigCopyByteSlice(s []byte) []byte {
  h := *(*swig_goslice_hdr)(unsafe.Pointer(&s))
  r := make([]byte, h.len)
  if h.len > 0 {
    copy(r, unsafe.Slice((*byte)(unsafe.Pointer(h.array)), h.len))
  }
  Swig_free(h.array)
  return r
}
%}

%typemap(directorin, fragment="AllocateByteSlice") quicr::BytesSpan {
    $input = Swig_AllocateByteSlice($1.data(), $1.size());
}

%typemap(godirectorin, fragment="CopyByteSlice") quicr::BytesSpan
%{ $result = swigCopyByteSlice($input) %}

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

/* quicr::messages::GroupOrder and quicr::messages::Location are real types
   declared in quicr/messages/ctrl_message_types.h - a large header full of
   MOQT wire-message types (Parameters, every control message struct, ...)
   we don't want to pull into the bindings wholesale, since it's an
   implementation detail of the control-plane protocol, not itself part of
   the public API surface this interface wraps. Both types are already
   used by value in several already-wrapped structs below (e.g.
   RequestResponse.publisher_default_group_order/largest_location in
   track_handler.h, PublishAttributes/SubscribeAttributes in attributes.h)
   - without a real declaration, SWIG has never actually known what either
   type *is*: every field/parameter mentioning one has silently fallen back
   to SWIG's generic "opaque, otherwise-unusable placeholder class" handling
   (no constructor, no accessors, no way to construct or compare a value) -
   the same class of problem %go_shared_ptr worked around for
   std::shared_ptr (see the comment near session_manager.h), just quieter,
   since SWIG doesn't warn about silently under-specifying an already-
   unknown type.

   These minimal, real declarations - mirroring the real header exactly,
   minus the defaulted C++20 `operator<=>` (SWIG can't wrap it any more than
   it could TrackNamespace::IsPrefixOf() above; ignored the same way) - give
   SWIG enough to generate genuine, usable value types from here on: an
   ordinary Go enum for GroupOrder (like TransportStatus/SessionStatus
   already are), and a real accessor-bearing struct for Location. This fixes
   every already-wrapped place either type appears, not just the new
   track-handler surface added later in this file. */
%ignore quicr::messages::Location::operator<=>;

/* Same "SWIG can't wrap C++ operator syntax" limitation as the free
   TrackNamespace comparison operators above (Warning 503), just for a
   member operator instead of a friend function this time - %rename works
   the same way for either. */
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

/* Bigger, previously-unnoticed gap than either the messages::GroupOrder/
   Location one just above, or the one this comment originally described:
   this file has never actually taught SWIG what uint8_t/uint16_t/uint32_t/
   uint64_t (bare *or* std::-qualified) really are. Every fixed-width
   integer typedef is used constantly throughout the already-wrapped
   surface (e.g. Transport::CreateDataContext()/Enqueue(), TrackHash's
   fields, %template(UInt64Vector) std::vector<uint64_t> above) - since
   SWIG was never told these names resolve to real primitive C types, *all*
   of them have silently been falling back to the exact same "opaque,
   otherwise-unusable placeholder class" handling documented above for
   messages::GroupOrder/Location: e.g. UInt64Vector's own Get()/Set()
   element type, or CreateDataContext()'s connection-ID return value, were
   never actually real Go integers. This is a much wider-reaching instance
   of the same root problem, just silent for even longer because it's
   spread across dozens of already-"working" (zero-SWIG-warning) methods
   rather than concentrated on a couple of named types.

   <stdint.i> (included near the top of this file, before anything that
   could need it - see the comment there) is SWIG's own official,
   language-agnostic library file for exactly this (the ISO C99 fixed-
   width integer typedefs) - the same kind of ready-made building block as
   <std_string.i>/<std_pair.i> above. It only declares the bare, global-
   namespace spelling though; std::uint8_t/std::uint16_t/std::uint32_t/
   std::uint64_t (the qualified spelling SubscribeTrackHandler::Create()'s
   `priority` parameter and quite a few already-wrapped fields, e.g.
   TrackHash's track_namespace_hash, actually use) still need an explicit
   alias to that real typedef chain, same reasoning as the
   messages::GroupOrder/Location fix above. This fixes every field/
   parameter using the std::-qualified spelling throughout this file, not
   just the new track-handler surface below. */
namespace std {
    typedef ::uint8_t uint8_t;
    typedef ::uint16_t uint16_t;
    typedef ::uint32_t uint32_t;
    typedef ::uint64_t uint64_t;
}
%ignore std::hash<quicr::TrackNamespace>;
%ignore std::hash<quicr::FullTrackName>;

/* std::optional<T> has never been taught to SWIG either - same "silently
   falls back to an opaque, otherwise-unusable placeholder class" problem
   as messages::GroupOrder/Location and the fixed-width integers above,
   just for a container instead of a plain value type. It's used *very*
   widely (TrackHandler::GetRequestId()/GetDataContextId()/
   GetRequestStreamId(), every ReasonCode struct's error_reason/
   largest_location, SubscribeTrackHandler::GetGroupOrder()/
   GetLatestLocation(), ObjectHeaders::priority/ttl/track_mode, ...), so
   fixing it generically here - once - is far less work than special-casing
   every call site, and fixes every one of them at once.

   Go has no built-in notion of "no value" for anything but a pointer/
   interface (both of which are nilable already), so a real
   std::optional<T> can't just become a Go `*T`/`T` the way std::string
   becomes a plain Go string - SWIG would need to either heap-allocate a T
   to point a `*T` at (extra allocation + a nil check that still has
   nothing to do with the real optional-ness) or silently return T's own
   zero value (indistinguishable from "present, but zero"). Both are worse
   than just giving Go the same two-step "check, then read" API
   std::optional<T> itself has in C++: HasValue() and Value(), mirroring
   has_value()/value() below via the global %rename further down. This
   needs a real (if partial) declaration of std::optional rather than an
   opaque stand-in precisely so SWIG generates working bodies for both -
   an opaque class with no declared members would give the same
   unusable-placeholder result this is fixing.

   Only instantiated below for the T's actually needed by the surface
   wrapped so far; add another %template line here (not a new typemap)
   the next time a not-yet-covered std::optional<T> shows up anywhere else
   in this file - the typemap/rename machinery is already generic across
   every T. */
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
%template(OptionalUInt8) std::optional<uint8_t>;
%template(OptionalUInt16) std::optional<uint16_t>;
%template(OptionalUInt32) std::optional<uint32_t>;
%template(OptionalUInt64) std::optional<uint64_t>;
%template(OptionalString) std::optional<std::string>;
%template(OptionalGroupOrder) std::optional<quicr::messages::GroupOrder>;
%template(OptionalLocation) std::optional<quicr::messages::Location>;

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

/* FullTrackName is a plain aggregate (see track_name.h) with only a
   compiler-generated default constructor - name_space/name are ordinary
   public fields, set via the SetName_space()/SetName() setters SWIG
   already generates for them. SetName() takes a ByteVector though (the
   %template'd std::vector<uint8_t> proxy), which has no bulk-load from a
   native byte string/slice in any target language - just the one-
   element-at-a-time Add()/Set() std::vector<T> proxies always get. Add a
   convenience setter that reuses the BytesSpan/[]byte typemap already
   built above (see the BytesSpan comment near the top of this file) so
   Go can just write `ftn.SetNameBytes([]byte("track1"))` directly instead
   of looping over a ByteVector by hand. */
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
