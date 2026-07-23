# SWIG bindings: known issues, gaps, and language-specific gotchas

This document tracks every SWIG-related surprise, bug, and out-of-scope gap
encountered while building and maintaining `swig/quicr.i` and its two
consumers, [`cmd/examples/swig/go`](../cmd/examples/swig/go) and
[`cmd/examples/swig/python`](../cmd/examples/swig/python). `quicr.i` itself
cross-references specific sections of this file inline (search either
codebase for `SWIG_WARNINGS.md`); this is the other half of those pointers.

Issues are grouped into three buckets:

- **Bugs found and fixed** - real crashes/compile failures/silently-broken
  types, with the fix actually applied in this repo.
- **Deliberate, tracked gaps** - C++ surface area this interface
  intentionally doesn't expose yet, listed here as a single follow-up
  backlog rather than fixed piecemeal.
- **Cross-language asymmetries** - not bugs at all, just places where SWIG's
  Go and Python backends genuinely generate different (and equally correct)
  shapes for the exact same C++ declaration, called out here purely so
  nobody mistakes one for a bug later.

As of the pass documented under "Resolved gaps" below, every previously
tracked gap in this file has been closed except `messages::Parameters`
(item 1) and the server-side `PublishFetchHandler`/`Session` receive-side
callbacks (new, see "Deliberate, tracked gaps" below) - both explained
there, with reasoning for why they're staying out of scope rather than
being fixed piecemeal.

## Bugs found and fixed

### 1. `quicr::BytesSpan` had no Python conversion at all

**Symptom:** `TypeError: in method 'FullTrackName_SetNameBytes', argument 2
of type 'quicr::BytesSpan'` the moment Python code tried to pass an ordinary
`bytes` object anywhere a `quicr::BytesSpan` parameter was expected (e.g.
`FullTrackName.SetNameBytes()`, `PublishTrackHandler.PublishObject()`).

**Root cause:** `BytesSpan` (`using BytesSpan = std::span<const Byte>` in
`quicr/utilities/bytes.h`) is only ever forward-declared to SWIG, never
`%include`-d - the real header isn't wrappable as-is. Go gets a working
`[]byte <-> BytesSpan` conversion via a hand-written pair of typemaps in
[`swig/go/typemaps.i`](go/typemaps.i). No Python equivalent existed; without
one, SWIG treats `BytesSpan` as an opaque, unusable `SWIGTYPE` with no way
to construct or read one at all from Python.

**Fix:** [`swig/python/typemaps.i`](python/typemaps.i) adds the Python
counterpart, following the same split Go's own file uses:

- `%typemap(in)` accepts anything implementing Python's buffer protocol
  (`bytes`, `bytearray`, `memoryview`, ...) via `PyObject_GetBuffer(...,
  PyBUF_CONTIG_RO)`, for the "Python passes bytes into C++" direction.
- `%typemap(directorin)` converts a `BytesSpan` back to a real, owned
  `bytes` object via `PyBytes_FromStringAndSize(...)` for the "C++ hands
  bytes back into a director callback" direction (e.g.
  `SubscribeTrackHandler::ObjectReceived()`'s `data` parameter) - copying
  rather than aliasing, since the span's backing storage is only guaranteed
  valid for the duration of the call that produced it.

### 2. Director classes with protected constructors were rejected as "abstract" in Python only

**Symptom:** `AttributeError: No constructor defined - class is abstract`
the moment a Python class tried to subclass `quicr.SubscribeTrackHandler` or
`quicr.PublishTrackHandler` - despite the exact same class, with the exact
same protected constructor and the exact same director setup, already
working correctly as a Go director base type (see the Go example's own
`NewDirectorSubscribeTrackHandler()`).

**Root cause:** `quicr.i` intentionally suppresses director generation for
`TrackHandler::RequestUpdateReceived()`/`RequestOkReceived()` via
`%feature("nodirector")` (see the comment above
`%warnfilter(517) quicr::SubscribeTrackHandler;` in `quicr.i`), because those
two are pure virtual in the base class but already have real, concrete
overrides in `SubscribeTrackHandler`/`PublishTrackHandler` themselves - the
`%nodirector`'d slots would otherwise emit a `_swig_gopanic("call to pure
virtual function ...")` stub that *shadows* those real overrides. That's a
correctness fix, but it has a side effect purely in SWIG's own director-
completeness bookkeeping: SWIG's model of the class still sees an
"unfulfilled" pure virtual (Warning 517, already filtered), and Python's
proxy-class generator additionally uses that same abstractness signal to
decide whether to emit a real `__init__`/constructor at all - unlike Go's
generator, which only cares about it for the (already-suppressed) director
vtable slot, so this never blocked Go's own constructor generation in the
first place.

**Fix:** `%feature("notabstract")` on every affected class (originally
`SubscribeTrackHandler`/`PublishTrackHandler`, now also
`FetchTrackHandler`/`PublishNamespaceHandler`/`SubscribeNamespaceHandler` -
see "Resolved gaps" below) in `python/type_extensions.i` explicitly
overrides SWIG's own abstract-class classification for constructor-
generation purposes, independent of the (correct, still-suppressed)
director behavior. This is required for Python; it happens to be a no-op
for Go, which never needed it.

### 3. `std::optional<StreamHeaderProperties>`'s custom typemap was Go-only and didn't even work for Python once ported

This one had two layered bugs, both already described in detail directly in
`quicr.i` (search for `OptionalStreamHeaderProperties`); summarized here
because both are exactly the kind of mistake this document exists to catch:

1. The existing custom `%typemap(in)` for this type called `_swig_gopanic()`
   unconditionally - a real symbol, but one that only exists inside Go's own
   cgo-exported runtime shim. `%typemap(in)`/`%typemap(out)` are
   *language-independent* category names, so Python's own code generator
   picked this typemap up for this exact type just as readily as Go's does,
   and the generated Python wrapper `.cxx` failed to *compile* with "use of
   undeclared identifier `_swig_gopanic`" - swig itself never warned about
   this at generation time.
2. Once that was fixed with a `#ifdef SWIGGO ... #else ... #endif` split,
   the naive `#else` branch (a plain copy-assignment, `$1 = *temp;`) hit the
   *exact same* deleted-copy-assignment-operator problem the Go branch's own
   placement-new trick already exists to work around (`std::optional<T>`'s
   copy assignment is only implicitly deleted when `T`'s own copy
   assignment is - true here since the real `StreamHeaderProperties`'
   members are all `const`). Every SWIG target language's own default
   by-value-`SWIGTYPE` typemap has this exact same shape, so this wasn't
   Python-specific at all - it's just latent in every *other* language this
   file might ever add, waiting to be hit the same way.

**Fix:** the `#else` branch was rewritten to use the same placement-new
construction (`$1.~optional(); new (&$1) std::optional<...>(*temp);`) as
Go's branch, expressed with SWIG's own portable `%argument_fail`/
`%argument_nullref`/`%reinterpret_cast`/`%delete` macros (rather than
hand-rolling Go's `SWIG_ConvertPtr` call site directly) so it stays correct
automatically for every future non-Go language this file adds, not just
Python. One more subtlety on top of that: this branch has to be written
with plain `{ }`, not the `%{ %}` used everywhere else in this file - `%{
%}` is copied into the generated wrapper completely verbatim, so those
`%argument_fail`/etc. macros never actually expand; written that way once by
mistake here, swig itself gave no error or warning, and it was only caught
by actually compiling the generated Python wrapper and hitting the literal,
unexpanded `%argument_fail` token as a C++ syntax error.

### 4. `std::pair` proxy objects look tuple-like in Python but silently break tuple *unpacking*

**Symptom:** `ValueError: too many values to unpack (expected 2)` from
`transport, session = manager.AddClientTransport(config)` - despite the
returned `TransportSessionPtrPair` object visibly supporting `len(pair) ==
2` and `pair[0]`/`pair[1]` indexing.

**Root cause:** SWIG's generic `std::pair<T1, T2>` Python support
(`std_pair.i`) gives the proxy class `.first`/`.second` attributes plus
`__len__`/`__getitem__` - but *not* `__iter__`. Without `__iter__`, Python's
old-style iteration-protocol fallback takes over for both `for` loops and
tuple-unpacking assignment: it calls `__getitem__(0)`, `__getitem__(1)`,
`__getitem__(2)`, ... and keeps going *until `__getitem__` raises
`IndexError`* - which is the only way that fallback ever learns a sequence
has ended. The generated `__getitem__` here is implemented as `index % 2`
(so `pair[2]`, `pair[3]`, ... just keep returning `.first`, `.second`,
`.first`, ... again, forever) - it never raises `IndexError` for any input,
so this fallback iterates infinitely from unpacking's point of view, which
is exactly what "too many values to unpack" is reporting once Python's own
unpacking bytecode gives up after collecting more values than it has
targets for.

**Fix:** access `.first`/`.second` directly instead of unpacking - see
`new_full_track_name()`'s sibling code in
[`cmd/examples/swig/python/main.py`](../cmd/examples/swig/python/main.py):

```python
pair = manager.AddClientTransport(config)
transport = pair.first
session = pair.second
```

### 5. Double free / segfault: `AddTrackHandler()`'s raw-pointer-into-`shared_ptr` pattern silently double-owns the object in Python

This is the most serious issue found while building the Python example - a
real, reproduced `Fatal Python error: Segmentation fault`, not just an
exception - and it is **still live in the Go bindings today**, just never
triggered there (see below for why).

**Symptom:** a hard crash (SIGSEGV, exit code 139) partway through Python
interpreter shutdown, well after the script's own code had already printed
its last line and returned cleanly:

```
Fatal Python error: Segmentation fault
...
_wrap_delete_SubscribeTrackHandler
SwigPyObject_dealloc
```

i.e. it happens when Python garbage-collects the `Subscriber`/`Publisher`
instance itself (in the example, this was the `handler` local going out of
scope at the end of `main()`) - not immediately when `AddTrackHandler()` is
called, which is what made this easy to miss initially.

**Root cause:** `SessionManager.AddTrackHandler()`'s `%extend` body in
`quicr.i` takes a plain, borrowed `TrackHandler*` and builds a brand new
`std::shared_ptr<TrackHandler>` directly around that raw pointer:

```cpp
void AddTrackHandler(quicr::Session* session, quicr::TrackHandler* handler)
{
    $self->AddHandler(session->shared_from_this(), std::shared_ptr<quicr::TrackHandler>(handler));
}
```

This is documented in `quicr.i` as intentionally safe *as long as it's the
only place a `shared_ptr` is ever constructed from that raw pointer* - which
is true on the C++ side. But it is not the only *owner*: `Subscriber(...)`/
`Publisher(...)` (real SWIG director subclasses, constructed from Python)
produce an ordinary SWIG Python proxy object, and every such proxy - director
or not - defaults to owning its underlying C++ object, meaning its own
deallocation calls `delete` on that same pointer once nothing in Python
references it anymore. After `AddTrackHandler()` runs, there are now two
independent owners of one pointer: the new C++ `shared_ptr`, and the
Python proxy's own default `thisown` bookkeeping. Whichever one lets go
last calls `delete` on an already-deleted object.

**Why this never surfaced in the Go example:** SWIG's Go module never
generates `runtime.SetFinalizer` calls for wrapped C++ objects at all
(confirmed against the generated `quicr.go`: zero occurrences of
`SetFinalizer` in the whole file), unlike its Python/Java/etc. modules,
which do so by default. Go's own generated proxy objects are therefore
*never* automatically deleted by the garbage collector - which means the
exact same "two owners of one raw pointer" setup exists there too, but the
second owner (the Go proxy) simply never calls `delete` on its own account,
so the bug is entirely latent rather than avoided. (It does mean the Go
proxy object itself just leaks for the life of the process instead - a much
smaller problem, and an accurate description of the *existing* "nothing to
release on the Go side" language already in the Go README, even though the
underlying justification for it turns out to be "Go never frees it," not
"C++ ownership was cleanly transferred.")

**Fix:** SWIG generates a `__disown__()` method on every director-enabled
proxy class specifically for this "a C++ container just took real ownership
of this director object out from under me" handoff - it flips the proxy's
own `thisown` flag off and returns a `weakref.proxy` standing in for the
same object (still fully usable for ordinary method calls, since the
underlying C++ object stays alive via the `shared_ptr` C++ now owns).
`cmd/examples/swig/python/main.py` calls it immediately after every
`AddTrackHandler()`:

```python
manager.AddTrackHandler(session, handler)
handler = handler.__disown__()
```

No `Go`-side fix was made for the underlying double-ownership pattern
itself (the Go proxy leaking, rather than double-freeing, means there's no
crash to fix there today) - but this is worth revisiting if Go bindings
ever need their objects to actually be freed deterministically rather than
leaked for the process lifetime.

### 6. Scalar `std::optional<T>` (e.g. `uint64_t`) was completely unusable and leaked in Python

**Symptom:** not an error - silently wrong, opaque output. E.g.
`handler.GetTrackAlias()` (a real, always-wrapped
`SubscribeTrackHandler`/`PublishTrackHandler` method returning
`std::optional<uint64_t>`) returned a bare, useless
`<Swig Object of type 'std::optional< unsigned long long > *' at 0x...>`
with no `HasValue()`/`Value()` and no way to get the number back out, plus
`swig/python detected a memory leak of type 'std::optional< unsigned long
long > *', no destructor found` printed to stderr on every call. Same story
for `ObjectHeaders.priority`/`.ttl` (`std::optional<uint8_t>`/`<uint16_t>`)
- both had silently never worked from Python, just never previously
exercised by anything.

**Root cause:** Go isn't the only SWIG backend with no built-in
`std::optional<T>` support - Python has none either. Go's own gap was
already worked around with `%go_optional_scalar` in
[`swig/go/typemaps.i`](go/typemaps.i) (giving scalar optionals a native,
nil-able `*T`); nothing equivalent existed for Python, so every scalar
optional fell back to `quicr.i`'s generic `std::optional<T>` stand-in class
- which only declares `has_value()`/`value()` on the *class itself*, not on
a pointer to it. SWIG generates struct field getters/setters (and some
by-value accessors, e.g. `GetTrackAlias()`) against a
`std::optional<CTYPE> *`, for which no method resolves at all - Python is
left with a bare, methodless `SwigPyObject`, and since no `%template` (and
therefore no known destructor) exists for that instantiation, it leaks
every time one crosses the boundary.

**Fix:** [`swig/python/typemaps.i`](python/typemaps.i) adds a
`%py_optional_scalar` macro, Python's counterpart to `%go_optional_scalar`
- converting directly to/from a Python `int`/`bool`/`None`, covering both
the field-pointer and plain by-value shapes, for `uint8_t`/`uint16_t`/
`uint32_t`/`uint64_t`/`bool`. Also needed a `%typecheck` typemap per type
(absent from Go's equivalent, which doesn't need one): without it, SWIG's
overload dispatcher for methods overloaded on a trailing defaulted
parameter (e.g. `Connection::OnRecvStream(uint64_t, optional<uint64_t>,
bool = false)`) can't tell this type apart from others at runtime (Warning
472, caught by actually rebuilding after adding the fix, not by anything
at first-pass generation time).

### 7. Uncaught C++ exceptions crash the whole process in *every* language, not just handle one throwing constructor

**Symptom:** `libc++abi: terminating due to uncaught exception of type
quicr::messages::ProtocolViolationException` (Python) /
a process abort with no Go panic or recoverable error at all (Go), the
moment any wrapped C++ function actually throws - first hit constructing
`messages::StreamHeaderProperties` with an invalid combination of
arguments (see "Resolved gaps" below), but not specific to that constructor
at all.

**Root cause:** neither `quicr.i` nor either language's own `type_extensions.i`
declared a `%exception` block anywhere. `%include <std_except.i>` (present
from the very start) only teaches SWIG about the *shapes* of a few
`std::exception` subclasses so other declarations can reference them - it
does not, by itself, wrap any generated call site in a `try`/`catch`. SWIG
does auto-generate a `try`/`catch` for the small number of `std_vector.i`
`%extend` methods explicitly declared `throw (std::out_of_range)` (visible
in the generated wrapper as e.g. `catch(std::out_of_range &_e) {
_swig_gopanic(...); }`), but that mechanism only fires for functions with an
explicit dynamic-exception-specification SWIG recognizes - every other
wrapped call, in both languages, had zero exception safety: a real,
reachable C++ exception (not just this one constructor - `TrackNamespace`'s
own constructor can throw too, along with plenty of other wrapped calls)
would abort the whole process instead of surfacing as an ordinary
Python exception or Go panic.

**Fix:** a global `%exception { try { $action } catch (const std::exception&
e) { ... } }` block in each language's own `type_extensions.i` -
`SWIG_exception(SWIG_RuntimeError, e.what())` for Python (raises a Python
`RuntimeError`), `_swig_gopanic(e.what())` for Go (a recoverable panic,
catchable with `recover()`). Catching by `const std::exception&`
polymorphically covers every real exception type this library throws
(e.g. `quicr::messages::ProtocolViolationException : std::runtime_error`)
without SWIG needing to know about each concrete exception type by name.

## Resolved gaps

These four items used to live in "Deliberate, tracked gaps" below,
each with a real fix landed in this pass. Kept as their own section
(rather than folded into "Bugs found and fixed" above) since none of these
were bugs to begin with - they were previously-undeclared C++ surface area,
now declared.

### `messages::StreamHeaderProperties` is now a real, usable type

Previously a bare, field-less opaque stand-in (enough for
`ObjectReceived()`/`PublishObject()`'s `std::optional<StreamHeaderProperties>`
parameter to parse and resolve against the real vtable slot, nothing more).
Now hand-declared in `quicr.i` with its real fields (`extensions`,
`subgroup_id_mode`, `end_of_group`, `default_priority`), both real
constructors (from a raw `uint64_t` type byte, or from the four fields
directly), `GetType()`, and the new `SubgroupIdType` enum - everything
`messages.h`'s real struct has, without needing to `%include` the rest of
`messages.h`. `%nodefaultctor` stays: every member is still `const` with no
default member initializer.

### `ObjectHeaders::extensions`/`immutable_extensions` are now real, usable maps

Previously `%ignore`'d - `quicr::Extensions` (`using Extensions =
std::map<uint64_t, std::vector<std::vector<uint8_t>>>`) had no `std::map`
typemap wired up in this interface at all. Now `%include <std_map.i>` (both
Go's and Python's own per-language `std_map.i` ship with this SWIG install;
neither had ever been pulled in here before) plus a `%template(Extensions)`
instantiation, using the same forward-declare-the-alias trick `quicr::Bytes`
already needed at the top of `quicr.i`. The map's value type
(`std::vector<std::vector<uint8_t>>`) was already wrapped as
`ByteVectorVector`, so no further typemap work was needed for it. Go's
`std_map.i` gives a `get`/`set`/`del`/`has_key` API; Python's gives a
richer, `dict`-like proxy (`__getitem__`/`__setitem__`/`.keys()`/`.items()`/
...) - both are SWIG's own stock per-language `std_map.i`, not hand-written
here.

### `messages::ErrorCode` is now declared, unlocking `TrackHandler::RequestError()`

`RequestUpdateReceived()`/`RequestOkReceived()` still take
`messages::Parameters` (see "Deliberate, tracked gaps" below - genuinely
out of scope, not just deferred) and stay `%ignore`'d/`%feature("nodirector")`'d
exactly as before. `RequestError(messages::ErrorCode, std::string)` only
ever needed `messages::ErrorCode` - a plain enum, now forward-declared in
`quicr.i` next to `GroupOrder`/`Location` - and has a real (non-pure)
default body in `TrackHandler`, so it was never `nodirector`'d and is now a
fully overridable director method, exactly like `ObjectReceived()`/
`StatusChanged()`.

### `Session::FetchTrack`/`PublishNamespace`/`SubscribeNamespace` are now reachable via `AddTrackHandler()`/`RemoveTrackHandler()`

The real blocker was never `Session`'s own methods (which take a
`std::shared_ptr<Handler>` directly - still opaque here, and still
`%ignore`'d, deliberately, for that reason) - it was that
`FetchTrackHandler`/`PublishNamespaceHandler`/`SubscribeNamespaceHandler`
had no wrapped type to construct in the first place. All three are now
hand-declared in `quicr.i`, director-enabled, the same way
`SubscribeTrackHandler`/`PublishTrackHandler` already were (protected
constructor with SWIG-invisible trailing parameters that keep their real
C++ defaults; `%warnfilter(517)` for the same false-abstract reason;
`%feature("notabstract")` added for Python in `python/type_extensions.i`).

The key discovery that made this tractable without touching `Session`'s own
SWIG declarations at all:
`SessionManager::AddHandler()`/`RemoveHandler()` (`src/session_manager.cpp`)
already `dynamic_pointer_cast` an incoming `shared_ptr<TrackHandler>`
against all five concrete handler types and dispatch to the matching
`Session::SubscribeTrack()`/`PublishTrack()`/`FetchTrack()`/
`PublishNamespace()`/`SubscribeNamespace()` on the C++ side. Since
`AddTrackHandler()`/`RemoveTrackHandler()` (the existing `%extend` wrappers
around `AddHandler()`/`RemoveHandler()`) already take a generic
`TrackHandler*`, they needed zero changes to start working for these three
new handler types the moment the types themselves existed - the exact same
call bindings already use for `SubscribeTrackHandler`/`PublishTrackHandler`
now also accepts a `FetchTrackHandler`/`PublishNamespaceHandler`/
`SubscribeNamespaceHandler` instance.

`PublishNamespaceHandler::PublishTrack()`/`UnPublishTrack()`/`PublishObject()`/
`ForwardPublishedData()` (passthrough helpers that forward to *child*
`PublishTrackHandler`s registered under the namespace handler) were
deliberately left undeclared - out of scope for this pass, same
shared_ptr-from-raw-pointer ownership question `AddTrackHandler()` already
had to solve once (see bug 5 above); registering/observing the namespace
handler itself doesn't need them.

## Deliberate, tracked gaps (not fixed, out of scope for this pass)

- **`messages::Parameters`** (used by
  `TrackHandler::RequestUpdateReceived()`/`RequestOkReceived()`) is a
  templated, wire-protocol-detail type (`ParameterList<ParameterType>`,
  backed by a `std::map<uint64_t, Bytes>` with per-value-type template
  member functions for `Add<T>()`/`Get<T>()`) never declared to SWIG. Both
  methods are `%ignore`'d; they're only ever called *by* the library in
  response to received MoQT control messages, never called *by* bindings
  code, and both real subclasses already provide concrete (non-pure)
  overrides in actual C++ regardless of what SWIG does or doesn't wrap.
  Unlike the other three gaps this document used to track, wrapping this
  type wouldn't actually unlock anything new even if done: both methods
  stay `%feature("nodirector")`'d for a *separate*, correctness reason (see
  `quicr.i`'s own comment on `RequestUpdateReceived()`) that has nothing to
  do with `Parameters` being unknown to SWIG - so even a fully-wrapped
  `Parameters` still couldn't be overridden from a director subclass here.
  `messages::ErrorCode` (the *other* type these methods' sibling
  `RequestError()` needed) has been wrapped - see "Resolved gaps" above.
- **`PublishFetchHandler`, and every server-side "peer sent us a
  request" receive callback on `Session`** (`PublishNamespaceReceived()`,
  `SubscribeNamespaceReceived()`, `PublishNamespaceDoneReceived()`,
  `PublishReceived()`, ...) are a distinct, *server-side* gap from the
  `FetchTrackHandler`/`PublishNamespaceHandler`/`SubscribeNamespaceHandler`
  one just resolved above - those three are *client-initiated* ("start a
  fetch/announce/subscribe"), constructed and owned by bindings code, and
  now fully wrapped. `PublishFetchHandler` (`Session::BindFetchTrack()`) is
  the *responder* side instead: the library constructs one internally,
  server-side, in response to a peer's incoming Fetch request - nothing in
  bindings code ever constructs one directly. Overriding the receive
  callbacks above would additionally require making `Session` itself
  director-enabled (it isn't today), a materially larger and riskier
  change with no existing test coverage exercising a server-side announce/
  subscribe-namespace flow to validate it against - deliberately left out
  of this pass rather than rushed.

## Cross-language asymmetries (not bugs - just worth knowing about)

- **Nested C++ enum naming.** `quicr::Session::Status` is nested *inside*
  `class Session`. Go's code generator flattens this into a single
  top-level package identifier, `quicr.SessionStatus_kReady` (concatenating
  the enclosing class name and the enum's own name with no separator).
  Python instead keeps the enum exactly where the real C++ declaration says
  it lives: as a `Session` class attribute, `quicr.Session.Status_kReady`.
  Plain namespace-scope enums (e.g. `quicr::TransportStatus`) aren't nested
  in any class to begin with, so both languages expose those the same way,
  as a flat module-level constant (`quicr.TransportStatus_kReady` in both).
- **Field access style.** Public C++ struct/class fields (e.g.
  `FullTrackName::name_space`, `ObjectHeaders::group_id`,
  `ClientConfig::endpoint_id`) come out as capitalized `GetX()`/`SetX()`
  method pairs in Go (Go has no property/attribute syntax of its own), but
  as ordinary Python properties in Python - `obj.name_space = ...` reads
  and writes the real underlying C++ field directly, with no `SetName_space`
  method generated or needed.
- **`AddClientTransport()`/`AddServerTransport()` multi-return.** Go has no
  way for a wrapped C++ function to return more than one Go value directly,
  so `quicr.i` gives these two a hand-written `%insert(go_wrapper)`
  companion (renaming the real, `%extend`'d methods to
  `AddClientTransportRaw()`/`AddServerTransportRaw()` for Go only) that
  unpacks the underlying `TransportSessionPtrPair`/raw pointer by hand into
  a native Go multi-return, plus a null-to-`nil` translation. Every other
  language, Python included, calls `AddClientTransport()`/
  `AddServerTransport()` directly under their real names and gets back the
  pair/pointer as-is - see item 4 above for why Python still can't just
  tuple-unpack that pair despite this.
- **`std::map` proxy richness.** Both Go's and Python's stock `std_map.i`
  (now used for `quicr::Extensions`, see "Resolved gaps" above) are SWIG's
  own per-language library files, not hand-written here - they just happen
  to differ a lot in what they expose. Go's gives four plain methods
  (`Get`/`Set`/`Del`/`Has_key`); Python's gives a much richer, `dict`-like
  proxy (`__getitem__`/`__setitem__`/`__len__`/`.keys()`/`.items()`/
  `.values()`/...). Neither is wrong; they're just SWIG's own idiomatic
  defaults for each language.

## History

- The original motivation for adding a custom `%typemap` for scalar
  `std::optional<T>` types at all (rather than relying on SWIG's own
  generic `std::optional<T>` wrapping, which produces a real but
  distractingly-named proxy class per instantiation, e.g.
  `Std_optional_Sl_quicr_messages_StreamHeaderProperties_Sg_`) was to give
  Go a nil-able native pointer (`*T`) for every *scalar* `std::optional<T>`
  in this interface, instead of that generated proxy class. That work
  predates this document; `messages::StreamHeaderProperties` was always the
  one exception left on the generic-proxy path (see "Resolved gaps" above
  for how that was eventually closed), which is what item 3 above is
  actually about. Python had no equivalent scalar-optional handling at all
  until bug 6 above - found by exercising the new
  `Fetch/PublishNamespace/SubscribeNamespaceHandler` types added in this
  same pass, not by anything already in either example.
- This document was created alongside `cmd/examples/swig/python`, the
  Python counterpart to the pre-existing `cmd/examples/swig/go` example,
  specifically to capture every issue that only became visible once a
  second SWIG target language actually exercised this interface end to
  end. Every issue in the "Bugs found and fixed" section above, except
  bugs 6 and 7, was found this way; none of them were visible, or even
  possible to hit, from the Go-only bindings that existed before it.
- Bugs 6 and 7, and every item in "Resolved gaps", came from a later pass
  specifically working through every item then listed under "Deliberate,
  tracked gaps" - verified end-to-end (both languages rebuilt from a clean
  SWIG regeneration, plus direct smoke tests exercising every newly-wrapped
  type/constructor/exception path in both) rather than left as a
  compiles-but-never-actually-run addition.
