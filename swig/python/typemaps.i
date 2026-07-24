/* Python-specific %typemap/%fragment definitions - counterpart to
 * python/type_extensions.i's %extend/%feature/%rename/%insert half. Only
 * ever %include'd under #ifdef SWIGPYTHON in quicr.i.
 */

/* ---- quicr::BytesSpan <-> Python bytes-like objects --------------------
 * BytesSpan (`using BytesSpan = std::span<const Byte>`) is a non-owning
 * view; SWIG is only taught enough here to know it's a real, wrappable
 * type for the typemaps below to attach to (same forward-declare
 * approach as go/typemaps.i). */
namespace quicr {
    class BytesSpan;
}

/* Python -> C++: accepts anything implementing the buffer protocol
 * (bytes, bytearray, memoryview, ...). PyBUF_CONTIG_RO requires a
 * contiguous buffer (all BytesSpan/std::span can represent) and allows a
 * read-only one (plain `bytes` objects are read-only). The Py_buffer can
 * be released immediately after building the span: BytesSpan never
 * outlives this call on the C++ side either, and releasing only drops
 * the buffer-protocol lock, not the caller's own reference to $input. */
%typemap(in) quicr::BytesSpan (Py_buffer view) {
    if (PyObject_GetBuffer($input, &view, PyBUF_CONTIG_RO) < 0) {
        SWIG_exception_fail(SWIG_TypeError, "in method '$symname', expected a bytes-like object for argument $argnum of type '$type'");
    }
    $1 = quicr::BytesSpan(static_cast<const uint8_t*>(view.buf), (size_t)view.len);
    PyBuffer_Release(&view);
}

/* C++ -> Python, director callbacks only (e.g. SubscribeTrackHandler's
 * ObjectReceived()): BytesSpan's backing storage is only valid for the
 * duration of the call that produced it, so this copies rather than
 * aliases it (same reasoning as go/typemaps.i's AllocateByteSlice
 * fragment) - PyBytes_FromStringAndSize() always allocates its own
 * buffer, giving the override an independent `bytes` object it can keep. */
%typemap(directorin) quicr::BytesSpan {
    $input = PyBytes_FromStringAndSize(reinterpret_cast<const char*>($1.data()), (Py_ssize_t)$1.size());
}

/* ---- std::optional<quicr::messages::StreamHeaderProperties> ------------
 * SWIG's default %typemap(in) for a plain by-value SWIGTYPE parameter -
 * the one Python would otherwise fall back to, from typemaps/swigtype.swg
 * - hits a deleted-copy-assignment error for this const-only type. This
 * mirrors that default line for line, but using placement-new instead of
 * copy-assignment, and the same portable %argument_fail/
 * %argument_nullref/%reinterpret_cast/%delete macros the real default
 * uses. go/typemaps.i needs its own, differently-shaped override for the
 * same underlying reason (Go's cgo ABI hands $input across as an
 * already-a-pointer raw value, so this file's SWIG_ConvertPtr-based
 * approach doesn't apply there, and vice versa).
 *
 * Deliberately plain { }, not %{ %}: a plain-brace typemap body is run
 * through SWIG's own preprocessor, which is what actually expands the
 * %argument_fail/etc. macros below into real code - %{ %} would emit them
 * unexpanded into the generated .cxx. */
%typemap(in,implicitconv=1) std::optional<quicr::messages::StreamHeaderProperties> (void *argp, int res = 0) {
    res = SWIG_ConvertPtr($input, &argp, $&descriptor, %convertptr_flags | %implicitconv_flag);
    if (!SWIG_IsOK(res)) {
        %argument_fail(res, "$type", $symname, $argnum);
    }
    if (!argp) {
        %argument_nullref("$type", $symname, $argnum);
    } else {
        $&ltype temp = %reinterpret_cast(argp, $&ltype);
        $1.~optional();
        new (&$1) std::optional<quicr::messages::StreamHeaderProperties>(*temp);
        if (SWIG_IsNewObj(res)) %delete(temp);
    }
}

/* ---- std::optional<CTYPE> <-> Python int/bool/None, for scalar CTYPE ---
 * SWIG's Python module has no built-in std::optional support either
 * (same gap go/typemaps.i's %go_optional_scalar documents for Go): a
 * scalar std::optional<CTYPE> falls back to an opaque, unusable
 * SwigPyObject with no has_value()/value() and no destructor (a real,
 * reproduced leak+garbage-object bug, found via
 * SubscribeTrackHandler::GetTrackAlias() - see swig/SWIG_WARNINGS.md).
 *
 * SWIG generates struct field getters/setters against a pointer
 * (std::optional<CTYPE> *), and ordinary by-value parameters/returns
 * (e.g. GetTrackAlias()) against the plain by-value type - both are
 * covered below, converting directly to/from a Python int/bool, using
 * None for an empty optional. The %typecheck entries let SWIG's overload
 * dispatcher (e.g. Connection::OnRecvStream(), overloaded on a trailing
 * defaulted bool) tell this type apart from others at runtime. */
%define %py_optional_scalar(CTYPE, PYFROM, PYAS, PYCHECK, TYPECHECK_PRECEDENCE)
%typemap(out) std::optional< CTYPE > {
    $result = $1.has_value() ? PYFROM($1.value()) : SWIG_Py_Void();
}
%typemap(in) std::optional< CTYPE > {
    if ($input == Py_None) {
        $1 = std::nullopt;
    } else {
        auto swig_val = PYAS($input);
        if (PyErr_Occurred()) SWIG_fail;
        $1 = std::optional< CTYPE >(static_cast< CTYPE >(swig_val));
    }
}
%typemap(typecheck, precedence=TYPECHECK_PRECEDENCE) std::optional< CTYPE > {
    $1 = ($input == Py_None) || PYCHECK($input);
}
%typemap(out) std::optional< CTYPE > * {
    $result = ($1 && $1->has_value()) ? PYFROM($1->value()) : SWIG_Py_Void();
}
%typemap(in) std::optional< CTYPE > * (std::optional< CTYPE > temp) {
    if ($input == Py_None) {
        temp = std::nullopt;
    } else {
        auto swig_val = PYAS($input);
        if (PyErr_Occurred()) SWIG_fail;
        temp = std::optional< CTYPE >(static_cast< CTYPE >(swig_val));
    }
    $1 = &temp;
}
%enddef

%py_optional_scalar(uint8_t, PyLong_FromLong, PyLong_AsLong, PyLong_Check, SWIG_TYPECHECK_INTEGER)
%py_optional_scalar(uint16_t, PyLong_FromLong, PyLong_AsLong, PyLong_Check, SWIG_TYPECHECK_INTEGER)
%py_optional_scalar(uint32_t, PyLong_FromUnsignedLong, PyLong_AsUnsignedLong, PyLong_Check, SWIG_TYPECHECK_INTEGER)
%py_optional_scalar(uint64_t, PyLong_FromUnsignedLongLong, PyLong_AsUnsignedLongLong, PyLong_Check, SWIG_TYPECHECK_INTEGER)
%py_optional_scalar(bool, PyBool_FromLong, PyObject_IsTrue, PyBool_Check, SWIG_TYPECHECK_BOOL)

