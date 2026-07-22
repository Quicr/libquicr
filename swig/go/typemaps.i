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

/* ---- std::optional<T> -> native Go pointer (*T), for scalar T ----------
   SWIG's Go module has no built-in notion of std::optional at all (see
   quicr.i's own "HasValue"/"Value" %rename + %template(OptionalXxx)
   pattern for the fallback used everywhere else in this file): left
   alone, every std::optional<T> instantiation that's never given one of
   those %template names falls back to an auto-mangled, unusable proxy
   class name (e.g. "Std_optional_Sl_bool_Sg_") - and even the ones that
   *do* get a friendly %template name still only expose has_value()/
   value() as ordinary method calls on an extra wrapper class/interface,
   never a real, idiomatic, nil-able Go pointer the way a plain C++
   pointer or reference parameter already does elsewhere in this file
   (see e.g. quicr::Transport* below).

   For a scalar T (an ordinary fixed-width integer or bool - the actual
   common case for every currently-unwrapped optional in this API:
   priority/ttl/request_id/track_alias/data_ctx_id/forward/...), this
   macro instead makes std::optional<T> cross as a genuine, freshly Go-
   allocated *GOTYPE that's nil exactly when the C++ optional is empty -
   nothing else in this file does this for a value (as opposed to
   pointer/class) C++ type, so unlike BytesSpan/[]byte above, there's no
   existing SWIG-shipped precedent to lean on; this was worked out and
   verified end-to-end (both directions, including the empty/nil case)
   against SWIG 4.4.1's actual Go code generator before being adopted
   here - see the "in"/"out"/"goout" bodies below for how each direction
   actually crosses the cgo boundary:

     - "in" (Go calling a function that takes std::optional<T>): Go
       passes the real address of its own *GOTYPE (or a nil, i.e. a null
       address) directly across cgo - exactly the same "pass a Go pointer
       into C for the duration of one call" pattern SWIG's own go.swg
       already uses for a plain (non-const) `int&` parameter, just
       applied to an optional instead of a mandatory reference.

     - "out"/"goout" (a function returning std::optional<T> back to Go):
       C++ can't hand Go a pointer into its own stack/heap the way "in"
       does in reverse (that memory doesn't outlive the call), so this
       instead malloc()s a tiny {size, val} box only when the optional
       actually has a value (mirroring go.swg's own SWIGTYPE (CLASS::*)
       out/goout pair almost exactly, just keyed on "has a value" rather
       than "is a non-null member pointer"), and the "goout" Go code
       copies that box's bytes into a real, freshly Go-allocated (and
       therefore GC-tracked) T before freeing it - never handing Go a
       pointer into non-Go-owned memory to keep around.

   GOTYPE must be one of the small set of Go scalar type names SWIG's own
   Go backend already recognizes by name when they appear after a
   leading "*" in a %typemap(gotype) string (this is *not* a naming
   convention this file invented - it's the same lexical recognition
   go.swg's own built-in `int&`/`long&`/... typemaps rely on for their own
   "*int"/"*int64"/... gotypes): bool, byte/uint8, int8, int16, uint16,
   int, uint, int64, uint64, float32, float64. CTYPE is the real C++
   scalar type (e.g. uint8_t) - it only has to be exactly as wide as
   GOTYPE; this macro never assumes anything about *how* SWIG's own
   internal bookkeeping represents that width, only about what its own
   "in"/"out" bodies read/write via an explicit pointer cast to CTYPE. */
%fragment("Swig_OptionalBox", "runtime") %{
struct Swig_optional_box { intgo size; void *val; };
%}

%define %go_optional_scalar(GOTYPE, CTYPE)
%typemap(gotype) std::optional< CTYPE > "*"#GOTYPE

%typemap(in) std::optional< CTYPE >
%{ $1 = $input ? std::optional< CTYPE >(*(CTYPE*)$input) : std::nullopt; %}

%typemap(out, fragment="Swig_OptionalBox") std::optional< CTYPE >
%{
    if ($1.has_value()) {
        struct Swig_optional_box *swig_box = (struct Swig_optional_box *)malloc(sizeof(*swig_box));
        swig_box->size = (intgo)sizeof(CTYPE);
        swig_box->val = malloc(sizeof(CTYPE));
        CTYPE swig_tmp = $1.value();
        memcpy(swig_box->val, &swig_tmp, sizeof(CTYPE));
        *(void**)&$result = (void*)swig_box;
    } else {
        *(void**)&$result = 0;
    }
%}

%typemap(goout) std::optional< CTYPE >
%{
    {
        type swig_optional_box struct { size int; val uintptr }
        p := (*swig_optional_box)(unsafe.Pointer($1))
        if p == nil || p.val == 0 {
            $result = nil
        } else {
            v := new(GOTYPE)
            *v = *(*GOTYPE)(unsafe.Pointer(p.val))
            Swig_free(p.val)
            Swig_free(uintptr(unsafe.Pointer(p)))
            $result = v
        }
    }
%}

/* ---- same thing again, but for std::optional<CTYPE>* -------------------
   Every "in"/"out"/"goout" triple above only ever fires for a *by-value*
   std::optional<CTYPE> - which covers every ordinary function parameter/
   return in this API, but not a plain public `std::optional<CTYPE> foo;`
   *struct field* (e.g. ObjectHeaders::priority/ttl below): lacking
   %naturalvar (see quicr.i's own %naturalvar comment for why turning
   that on for every class wholesale isn't the fix here either - it
   doesn't change any of this), SWIG's default for a struct field whose
   type is some class it doesn't otherwise recognize is to synthesize a
   getter/setter pair that hands Go a raw, direct pointer at the field's
   own storage (`&arg1->foo`) and typemaps *that pointer type* instead -
   i.e. std::optional<CTYPE>*, not std::optional<CTYPE> - so without this
   second block every scalar-optional field falls back to SWIG's own
   builtin generic "SWIGTYPE *" default instead of anything above: a
   gotype of "*" + whatever %typemap(gotype) is registered for
   std::optional<CTYPE> (i.e. "*" + "*"#GOTYPE), which is where the
   "**uint8 instead of *uint8" field-getter bug this was actually caught
   by came from. Same nil/box semantics as the by-value trio above,
   just with one extra pointer indirection on the C++ side to read
   through (or, for "in", a same-function-scope local to point *at*,
   since SWIG's synthesized field setter always assigns through `*arg2`
   - see $1's local-variable declaration below). */
%typemap(gotype) std::optional< CTYPE > * "*"#GOTYPE

%typemap(in) std::optional< CTYPE > * (std::optional< CTYPE > swig_tmp)
%{
    swig_tmp = $input ? std::optional< CTYPE >(*(CTYPE*)$input) : std::nullopt;
    $1 = &swig_tmp;
%}

%typemap(out, fragment="Swig_OptionalBox") std::optional< CTYPE > *
%{
    if ($1 && $1->has_value()) {
        struct Swig_optional_box *swig_box = (struct Swig_optional_box *)malloc(sizeof(*swig_box));
        swig_box->size = (intgo)sizeof(CTYPE);
        swig_box->val = malloc(sizeof(CTYPE));
        CTYPE swig_tmp = $1->value();
        memcpy(swig_box->val, &swig_tmp, sizeof(CTYPE));
        *(void**)&$result = (void*)swig_box;
    } else {
        *(void**)&$result = 0;
    }
%}

%typemap(goout) std::optional< CTYPE > *
%{
    {
        type swig_optional_box struct { size int; val uintptr }
        p := (*swig_optional_box)(unsafe.Pointer($1))
        if p == nil || p.val == 0 {
            $result = nil
        } else {
            v := new(GOTYPE)
            *v = *(*GOTYPE)(unsafe.Pointer(p.val))
            Swig_free(p.val)
            Swig_free(uintptr(unsafe.Pointer(p)))
            $result = v
        }
    }
%}
%enddef

%go_optional_scalar(uint8, uint8_t)
%go_optional_scalar(uint16, uint16_t)
%go_optional_scalar(uint32, uint32_t)
%go_optional_scalar(uint64, uint64_t)
%go_optional_scalar(bool, bool)

/* std::chrono::milliseconds isn't itself a scalar (it's a
   std::chrono::duration specialization), so it can't use the memcpy-
   based %go_optional_scalar macro above unmodified - std::chrono::
   milliseconds's in-memory representation isn't required by the standard
   to be a bare int64_t the way it practically always is, so this reads/
   writes it through its own .count()/constructor instead of reinterpreting
   raw bytes. There's no existing Go "milliseconds" type anywhere in this
   API to preserve, so a plain *int64 (a count of milliseconds, same as
   std::chrono::milliseconds::rep) is the natural, idiomatic Go shape. */
%typemap(gotype) std::optional< std::chrono::milliseconds > "*int64"

%typemap(in) std::optional< std::chrono::milliseconds >
%{ $1 = $input ? std::optional< std::chrono::milliseconds >(std::chrono::milliseconds(*(int64_t*)$input)) : std::nullopt; %}

%typemap(out, fragment="Swig_OptionalBox") std::optional< std::chrono::milliseconds >
%{
    if ($1.has_value()) {
        struct Swig_optional_box *swig_box = (struct Swig_optional_box *)malloc(sizeof(*swig_box));
        swig_box->size = (intgo)sizeof(int64_t);
        swig_box->val = malloc(sizeof(int64_t));
        int64_t swig_tmp = static_cast<int64_t>($1.value().count());
        memcpy(swig_box->val, &swig_tmp, sizeof(int64_t));
        *(void**)&$result = (void*)swig_box;
    } else {
        *(void**)&$result = 0;
    }
%}

%typemap(goout) std::optional< std::chrono::milliseconds >
%{
    {
        type swig_optional_box struct { size int; val uintptr }
        p := (*swig_optional_box)(unsafe.Pointer($1))
        if p == nil || p.val == 0 {
            $result = nil
        } else {
            v := new(int64)
            *v = *(*int64)(unsafe.Pointer(p.val))
            Swig_free(p.val)
            Swig_free(uintptr(unsafe.Pointer(p)))
            $result = v
        }
    }
%}

/* Same std::optional<CTYPE>* struct-field story as %go_optional_scalar's
   own pointer-variant block above (see its comment there for the full
   explanation) - PublishAttributes::max_cache_duration/delivery_timeout
   and PublishOkAttributes::subgroup_delivery_timeout/object_delivery_
   timeout below are all plain std::optional<std::chrono::milliseconds>
   fields, so they need this too. */
%typemap(gotype) std::optional< std::chrono::milliseconds > * "*int64"

%typemap(in) std::optional< std::chrono::milliseconds > * (std::optional< std::chrono::milliseconds > swig_tmp)
%{
    swig_tmp = $input ? std::optional< std::chrono::milliseconds >(std::chrono::milliseconds(*(int64_t*)$input)) : std::nullopt;
    $1 = &swig_tmp;
%}

%typemap(out, fragment="Swig_OptionalBox") std::optional< std::chrono::milliseconds > *
%{
    if ($1 && $1->has_value()) {
        struct Swig_optional_box *swig_box = (struct Swig_optional_box *)malloc(sizeof(*swig_box));
        swig_box->size = (intgo)sizeof(int64_t);
        swig_box->val = malloc(sizeof(int64_t));
        int64_t swig_tmp = static_cast<int64_t>($1->value().count());
        memcpy(swig_box->val, &swig_tmp, sizeof(int64_t));
        *(void**)&$result = (void*)swig_box;
    } else {
        *(void**)&$result = 0;
    }
%}

%typemap(goout) std::optional< std::chrono::milliseconds > *
%{
    {
        type swig_optional_box struct { size int; val uintptr }
        p := (*swig_optional_box)(unsafe.Pointer($1))
        if p == nil || p.val == 0 {
            $result = nil
        } else {
            v := new(int64)
            *v = *(*int64)(unsafe.Pointer(p.val))
            Swig_free(p.val)
            Swig_free(uintptr(unsafe.Pointer(p)))
            $result = v
        }
    }
%}
