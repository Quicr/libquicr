/* Go-specific %typemap/%fragment definitions - counterpart to
 * go/type_extensions.i's %extend/%feature/%rename/%insert half. Only
 * ever %include'd under #ifdef SWIGGO in quicr.i.
 */

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

/* directorin copies BytesSpan's backing storage into a fresh Go []byte
   (rather than aliasing it) since it's only valid for the duration of
   the call that produced it. */
%typemap(directorin, fragment="AllocateByteSlice") quicr::BytesSpan {
    $input = Swig_AllocateByteSlice($1.data(), $1.size());
}

%typemap(godirectorin, fragment="CopyByteSlice") quicr::BytesSpan
%{ $result = swigCopyByteSlice($input) %}

/* ---- std::optional<T> -> native Go pointer (*T), for scalar T ----------
 * SWIG's Go module has no built-in std::optional support: without this,
 * every std::optional<T> falls back to either an auto-mangled, unusable
 * proxy class name, or (with a %template name) a has_value()/value()
 * wrapper class - never a real, nil-able Go pointer.
 *
 * This macro instead makes std::optional<T> cross as a freshly
 * Go-allocated *GOTYPE, nil exactly when the C++ optional is empty:
 *   - "in": Go passes the address of its own *GOTYPE (or nil) across cgo
 *     for the duration of the call, the same way SWIG's go.swg handles a
 *     plain `int&` parameter.
 *   - "out"/"goout": C++ malloc()s a tiny {size, val} box when the
 *     optional has a value; "goout" copies it into a fresh,
 *     GC-tracked Go value and frees the box - Go never keeps a pointer
 *     into non-Go-owned memory.
 *
 * GOTYPE must be one of the Go scalar names SWIG's Go backend recognizes
 * after a leading "*" in a %typemap(gotype) string (bool, byte/uint8,
 * int8, int16, uint16, int, uint, int64, uint64, float32, float64).
 * CTYPE is the real C++ scalar type; it only has to be exactly as wide as
 * GOTYPE. */
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
 * The by-value "in"/"out"/"goout" trio above only fires for a by-value
 * std::optional<CTYPE> parameter/return, not a struct field (e.g.
 * ObjectHeaders::priority/ttl): SWIG synthesizes field getters/setters
 * against a raw std::optional<CTYPE>* instead, which needs its own
 * gotype/in/out/goout registered here or it falls back to SWIG's generic
 * "SWIGTYPE *" default. Same nil/box semantics, with one extra pointer
 * indirection. */
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

/* std::chrono::milliseconds isn't a scalar, so it can't reuse
   %go_optional_scalar's memcpy-based approach unmodified - this reads/
   writes it through .count()/its constructor instead. Crosses as a plain
   *int64 (a millisecond count). */
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

/* Same struct-field story as %go_optional_scalar's own pointer variant
   above - PublishAttributes::max_cache_duration/delivery_timeout and
   PublishOkAttributes::subgroup_delivery_timeout/object_delivery_timeout
   are all std::optional<std::chrono::milliseconds> fields needing this. */
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

/* ---- std::optional<quicr::messages::StreamHeaderProperties> ------------
 * SWIG's default %typemap(in) for a plain by-value SWIGTYPE parameter
 * hits a deleted-copy-assignment error for this const-only type. go.swg's
 * default has the same bug, via the same copy-assignment shape, so it
 * needs this placement-new-based override instead. python/typemaps.i
 * needs its own, differently-shaped override for the same underlying
 * reason (Go's cgo ABI hands $input across as an already-a-pointer raw
 * value, so Python's SWIG_ConvertPtr-based approach doesn't apply here,
 * and vice versa). */
%typemap(in) std::optional<quicr::messages::StreamHeaderProperties> ($&1_type argp)
%{
    argp = ($&1_ltype)$input;
    if (argp == NULL) {
        _swig_gopanic("Attempt to dereference null std::optional< quicr::messages::StreamHeaderProperties >");
    }
    $1.~optional();
    new (&$1) std::optional<quicr::messages::StreamHeaderProperties>(*argp);
%}
