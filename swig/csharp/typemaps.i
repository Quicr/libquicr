/* C#-specific %typemap/%fragment definitions - counterpart to
 * csharp/type_extensions.i's %extend/%feature/%rename/%insert half. Only
 * ever %include'd under #ifdef SWIGCSHARP in quicr.i.
 */

/* ---- quicr::BytesSpan <-> C# byte[] -------------------------------------
 * BytesSpan (`using BytesSpan = std::span<const Byte>`) is a non-owning
 * view; SWIG is only taught enough here to know it's a real, wrappable
 * type for the typemaps below to attach to (same forward-declare
 * approach as go/typemaps.i and python/typemaps.i).
 *
 * Unlike Go (a slice header already carries {ptr,len} as one value
 * across cgo) or Python (PyObject* already carries its own length via
 * the buffer protocol), a bare C# byte[] does not carry its length
 * across P/Invoke on its own - marshaling one as a plain pointer would
 * lose it. SwigBytesSpanCType below is a small POD struct standing in
 * for BytesSpan itself, marshaled by value across P/Invoke exactly like
 * Go's own built-in _goslice_ header struct; declared as a plain
 * aggregate with *no* user-provided constructors on purpose - adding one
 * silently changes this struct's C++ ABI classification (register- vs
 * memory-passed) independently of C#'s own blittable-struct marshaling
 * rules for the exact same layout, and the two can disagree (found by
 * trying exactly this). */
%{
struct SwigBytesSpanCType { const void* data; unsigned long long len; };
%}

namespace quicr {
    class BytesSpan;
}

/* modulecode places this directly inside the generated module proxy
 * class (`public class quicr` in quicr.cs, matching `%module quicr`) -
 * every other generated C# file already references that class by name
 * (e.g. quicr.TrackNamespaceEquals()), so its nested struct types below
 * are reachable the same way: `quicr.SwigBytesSpanCType`, not
 * `quicrPINVOKE.SwigBytesSpanCType` (caught by actually compiling the
 * generated output, not by anything at swig-generation time - the wrong
 * qualifier only fails with a C# CS0426 once dotnet build runs). Same
 * cross-file visibility reasoning applies to the optional-scalar box
 * below. */
%pragma(csharp) modulecode=%{
  [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Sequential)]
  public struct SwigBytesSpanCType {
    public global::System.IntPtr data;
    public ulong len;
  }

  /* Director callbacks only (e.g. SubscribeTrackHandler's
     ObjectReceived()): BytesSpan's backing storage is only valid for the
     duration of the call that produced it, so this copies rather than
     aliases it (same reasoning as go/typemaps.i's AllocateByteSlice
     fragment/python/typemaps.i's PyBytes_FromStringAndSize) - a fresh
     managed byte[] is always its own, independently-owned copy. */
  internal static byte[] SwigUnpackBytesSpan(SwigBytesSpanCType value) {
    if (value.data == global::System.IntPtr.Zero || value.len == 0) return new byte[0];
    byte[] result = new byte[(int)value.len];
    global::System.Runtime.InteropServices.Marshal.Copy(value.data, result, 0, result.Length);
    return result;
  }
%}

%typemap(ctype) quicr::BytesSpan "SwigBytesSpanCType"
%typemap(imtype) quicr::BytesSpan "quicr.SwigBytesSpanCType"
%typemap(cstype) quicr::BytesSpan "byte[]"

/* C# -> C++: pins the caller's own array for exactly the duration of the
 * P/Invoke call (freed in `post`, guaranteed via SWIG's own generated
 * try/finally - verified in the generated code, not just assumed) rather
 * than copying it - safe because BytesSpan never outlives the call it
 * was passed into on the C++ side either (same contract python/
 * typemaps.i's own %typemap(in) comment describes). Null/empty arrays
 * skip GCHandle.Alloc entirely (it throws on a null target). */
%typemap(csin,
    pre="    global::System.Runtime.InteropServices.GCHandle swigPinned$csinput = ($csinput != null && $csinput.Length > 0) ? global::System.Runtime.InteropServices.GCHandle.Alloc($csinput, global::System.Runtime.InteropServices.GCHandleType.Pinned) : default(global::System.Runtime.InteropServices.GCHandle);",
    post="    if ($csinput != null && $csinput.Length > 0) swigPinned$csinput.Free();"
) quicr::BytesSpan
    "new quicr.SwigBytesSpanCType {
      data = ($csinput != null && $csinput.Length > 0) ? swigPinned$csinput.AddrOfPinnedObject() : global::System.IntPtr.Zero,
      len = (ulong)($csinput != null ? $csinput.Length : 0)
    }"

%typemap(in) quicr::BytesSpan %{
    $1 = quicr::BytesSpan(static_cast<const uint8_t*>($input.data), (size_t)$input.len);
%}

/* C++ -> C#, director callbacks only - see SwigUnpackBytesSpan's own
 * comment above for why this copies. */
%typemap(directorin) quicr::BytesSpan %{
    $input.data = $1.data();
    $input.len = (unsigned long long)$1.size();
%}
%typemap(csdirectorin) quicr::BytesSpan "quicr.SwigUnpackBytesSpan($iminput)"

/* ---- std::optional<CTYPE> <-> C# nullable value types, for scalar CTYPE
 * SWIG's C# module has no built-in std::optional support either (same
 * gap go/typemaps.i's %go_optional_scalar and python/typemaps.i's
 * %py_optional_scalar document for Go/Python): a scalar
 * std::optional<CTYPE> falls back to an opaque, unusable
 * SWIGTYPE_p_std__optionalT..._t with no HasValue()/Value() and no
 * destructor (found via SubscribeTrackHandler::GetTrackAlias(), same
 * root cause as Go/Python's own versions of this bug).
 *
 * SwigOptionalScalarBox (same by-value-POD-with-no-constructors caveat
 * as SwigBytesSpanCType above) is reused unmodified across every CTYPE
 * this is instantiated for below - `value` is always the widest scalar
 * (unsigned long long) regardless of CTYPE's actual width, narrowed back
 * down on whichever side actually needs the narrower type. This crosses
 * as a real, idiomatic C# nullable (CSTYPE?), matching CTYPE's own
 * bool-or-integer-ness. */
%{
struct SwigOptionalScalarBox { bool has_value; unsigned long long value; };
%}
%pragma(csharp) modulecode=%{
  [global::System.Runtime.InteropServices.StructLayout(global::System.Runtime.InteropServices.LayoutKind.Sequential)]
  public struct SwigOptionalScalarBox {
    public bool has_value;
    public ulong value;
  }

  /* bool doesn't have a plain numeric cast to/from ulong in C# (unlike
     every other %csharp_optional_scalar instantiation below, which just
     casts directly) - these two stand in for that one CTYPE. */
  internal static ulong SwigBoolToULong(bool value) { return value ? 1UL : 0UL; }
  internal static bool SwigULongToBool(ulong value) { return value != 0; }
%}

/* TO_ULONG/FROM_ULONG let bool reuse this same macro despite needing a
 * true/false <-> 0/1 conversion instead of every other CTYPE's plain
 * numeric cast - see %csharp_optional_scalar(bool, ...)'s own invocation
 * below. */
%define %csharp_optional_scalar(CSTYPE, CTYPE, TO_ULONG, FROM_ULONG)
%typemap(ctype) std::optional< CTYPE > "SwigOptionalScalarBox"
%typemap(imtype) std::optional< CTYPE > "quicr.SwigOptionalScalarBox"
%typemap(cstype) std::optional< CTYPE > #CSTYPE "?"
%typemap(csin) std::optional< CTYPE >
    "($csinput.HasValue)
      ? new quicr.SwigOptionalScalarBox { has_value = true, value = TO_ULONG($csinput.Value) }
      : new quicr.SwigOptionalScalarBox { has_value = false, value = 0 }"
/* Deliberately no `return $null;` in the exception path below (unlike
   csharp.swg's own built-in std::exception %typemap(throws)) - it does
   not compile here, `$null` isn't aware of this file's own custom ctype
   above. Falling through instead leaves `result` at whatever %exception
   wrapping this call already default-constructed it to; never observed
   by the caller anyway, since $excode's own pending-exception check
   below throws before this value is ever read. */
%typemap(csout, excode=SWIGEXCODE) std::optional< CTYPE > {
    quicr.SwigOptionalScalarBox box = $imcall;$excode
    return box.has_value ? new CSTYPE?(FROM_ULONG(box.value)) : null;
}
%typemap(in) std::optional< CTYPE > %{
    $1 = $input.has_value ? std::optional< CTYPE >(static_cast< CTYPE >($input.value)) : std::nullopt;
%}
%typemap(out) std::optional< CTYPE > %{
    $result.has_value = $1.has_value();
    $result.value = $1.has_value() ? static_cast<unsigned long long>($1.value()) : 0ULL;
%}

/* Struct-field getters/setters (e.g. ObjectHeaders::priority/ttl) are
 * synthesized by SWIG against a raw std::optional<CTYPE>*, not the plain
 * by-value type above - same split go/typemaps.i's own pointer-variant
 * comment documents, for the same underlying reason.
 *
 * The setter's native function takes a pointer parameter (so its
 * %typemap(in) below can write into a local, then have the field
 * assignment run against it); the getter's native function *returns* a
 * pointer aliasing the field's own storage instead - one C++ signature
 * shape, two different C# marshaling needs, so `ref` (parameter-only;
 * a ref-returning `extern` P/Invoke declaration does not compile) can't
 * describe both. `out=` below is SWIGTYPE*'s own built-in mechanism for
 * exactly this asymmetry (HandleRef in, IntPtr out, see csharp.swg) -
 * reused here with this file's own box type instead of HandleRef/IntPtr. */
%typemap(ctype, out="SwigOptionalScalarBox") std::optional< CTYPE > * "SwigOptionalScalarBox*"
%typemap(imtype, out="quicr.SwigOptionalScalarBox") std::optional< CTYPE > * "ref quicr.SwigOptionalScalarBox"
%typemap(cstype) std::optional< CTYPE > * #CSTYPE "?"
/* $imcall (used by csvarin/csvarout below) is itself built from this
   csin - only reached via csvarin's "set" block below; csvarout's "get"
   block takes no parameter, so csin doesn't apply there at all - its
   $imcall is a plain value-returning call instead, per the `out=` above. */
%typemap(csin) std::optional< CTYPE > * "ref swigbox"
%typemap(csvarin, excode=SWIGEXCODE2) std::optional< CTYPE > * %{
    set {
      quicr.SwigOptionalScalarBox swigbox = (value.HasValue)
          ? new quicr.SwigOptionalScalarBox { has_value = true, value = TO_ULONG(value.Value) }
          : new quicr.SwigOptionalScalarBox { has_value = false, value = 0 };
      $imcall;$excode
    }
%}
%typemap(csvarout, excode=SWIGEXCODE2) std::optional< CTYPE > * %{
    get {
      quicr.SwigOptionalScalarBox swigbox = $imcall;$excode
      return swigbox.has_value ? new CSTYPE?(FROM_ULONG(swigbox.value)) : null;
    }
%}
%typemap(in) std::optional< CTYPE > * (std::optional< CTYPE > swig_tmp) %{
    swig_tmp = $input->has_value ? std::optional< CTYPE >(static_cast< CTYPE >($input->value)) : std::nullopt;
    $1 = &swig_tmp;
%}
/* Return direction only (ctype/imtype `out=` above) - builds the box
   fresh from the field's current value. No argout: unlike the setter,
   nothing needs to flow back into a caller-owned pointer here. */
%typemap(out) std::optional< CTYPE > * %{
    $result.has_value = ($1 && $1->has_value());
    $result.value = $result.has_value ? static_cast<unsigned long long>($1->value()) : 0ULL;
%}
%enddef

%csharp_optional_scalar(byte, uint8_t, (ulong), (byte))
%csharp_optional_scalar(ushort, uint16_t, (ulong), (ushort))
%csharp_optional_scalar(uint, uint32_t, (ulong), (uint))
%csharp_optional_scalar(ulong, uint64_t, (ulong), (ulong))
%csharp_optional_scalar(bool, bool, quicr.SwigBoolToULong, quicr.SwigULongToBool)

/* ---- std::optional<StreamHeaderProperties>, by-value parameter only ----
 * SubscribeTrackHandler::ObjectReceived()'s third parameter. Unrelated to
 * the scalar boxing above (StreamHeaderProperties is a real class, wrapped
 * as an opaque pointer like any other %template'd std::optional<T> - see
 * %template(OptionalStreamHeaderProperties) in quicr.i - ctype/imtype/
 * cstype are all already correct without any override here).
 *
 * StreamHeaderProperties's fields are all `const` (see messages.h), making
 * it - and std::optional<StreamHeaderProperties> along with it - copy-
 * constructible but not copy-assignable. SWIG's own default %typemap(in)
 * for a plain by-value class-type C# parameter uses assignment
 * (`$1 = *argp;`), which doesn't compile for a type like this (a real,
 * reproduced build failure - "copy assignment operator is implicitly
 * deleted" - not a warning). go/typemaps.i's own generated code for this
 * exact parameter (a language-generic director/upcall code path, found by
 * comparing its generated wrap .cxx against this one's) sidesteps the same
 * problem with a placement-new + copy-construct instead of assignment;
 * reused here for the same reason, since C#'s own default doesn't. */
%typemap(in) std::optional< quicr::messages::StreamHeaderProperties > %{
    {
      std::optional< quicr::messages::StreamHeaderProperties > *argp = (std::optional< quicr::messages::StreamHeaderProperties > *)$input;
      if (!argp) {
        /* 0, not $argnum: SWIG_CSharpSetPendingExceptionArgument's 3rd
           parameter is a `const char *` (the argument's *name*, absent
           here), not an index - matching the literal `0` SWIG's own
           default-generated null checks elsewhere in this same file use
           for the identical "no name available" case. */
        SWIG_CSharpSetPendingExceptionArgument(SWIG_CSharpArgumentNullException, "Attempt to dereference null std::optional< quicr::messages::StreamHeaderProperties >", 0);
        return $null;
      }
      (&$1)->~optional();
      new (&$1) std::optional< quicr::messages::StreamHeaderProperties >(*argp);
    }
%}
