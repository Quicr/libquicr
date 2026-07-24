/* C#-specific %extend/%feature/%rename/%insert additions - counterpart
 * to csharp/typemaps.i's %typemap/%fragment half. Only ever %include'd
 * under #ifdef SWIGCSHARP in quicr.i.
 */

/* All five TrackHandler subclasses below are director-enabled classes
 * with a protected constructor and a %feature("nodirector")'d pure
 * virtual ancestor (see quicr.i's comment on TrackHandler::
 * RequestUpdateReceived()), which makes SWIG's director-completeness
 * check treat each as abstract (Warning 517, already filtered in
 * quicr.i), even though none actually is.
 *
 * Go's constructor generation never consults that signal, so it's
 * unaffected. Python's does (see python/type_extensions.i's own copy of
 * this comment) and needs this same override - so does C#'s: without it,
 * constructing a C# subclass fails at compile time with "cannot create
 * an instance of the abstract type or interface 'SubscribeTrackHandler'". */
%feature("notabstract") quicr::SubscribeTrackHandler;
%feature("notabstract") quicr::PublishTrackHandler;
%feature("notabstract") quicr::FetchTrackHandler;
%feature("notabstract") quicr::PublishNamespaceHandler;
%feature("notabstract") quicr::SubscribeNamespaceHandler;

/* Global exception translation: without this, any C++ exception a
 * wrapped function throws (e.g. StreamHeaderProperties's constructor,
 * TrackNamespace's) propagates uncaught across the P/Invoke boundary and
 * aborts the whole process instead of a recoverable C# exception - same
 * bug, found the same way, as go/type_extensions.i's and python/
 * type_extensions.i's own copies of this block.
 *
 * Deliberately no explicit `return $null;`/similar here, unlike csharp.
 * swg's own built-in std::exception %typemap(throws) - letting execution
 * just fall through to whatever normal return path follows works for
 * every return type actually used in this API, including the
 * SwigBytesSpanCType/SwigOptionalScalarBox custom ctypes in typemaps.i
 * that `$null` itself doesn't compile against (see that file's own
 * comment on this same subject, next to %csharp_optional_scalar). */
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_CSharpSetPendingException(SWIG_CSharpApplicationException, e.what());
    }
}

/* Director-enabled proxy classes default to owning their underlying C++
 * object in C# exactly like they do in Python (delete-on-Dispose/
 * finalize) - AddTrackHandler()'s %extend body in quicr.i builds a
 * second, independent std::shared_ptr<TrackHandler> directly around the
 * same raw pointer, and letting both C# and that shared_ptr believe they
 * solely own it double-frees the moment either one lets go first (same
 * root cause, and the same fix, as Python's __disown__()).
 *
 * Python gets __disown__() for free from SWIG's own director code
 * generation; C#'s director code generation does not generate an
 * equivalent, so this adds one by hand. swigCMemOwn is `protected` on
 * TrackHandlerEnableSharedFromThis (the actual root of this hierarchy,
 * not TrackHandler itself) - %typemap(cscode) splices this method
 * directly into TrackHandler's own proxy class body, not a subclass of
 * it, so it can still reach that protected field normally, and every
 * subclass below (SubscribeTrackHandler, ...) inherits it unmodified. */
%typemap(cscode) quicr::TrackHandler %{
  /* Must be called exactly once, immediately after handing this instance
     to SessionManager.AddTrackHandler() - see cmd/examples/swig/csharp/
     Program.cs's own usage for why. */
  public void Disown() {
    swigCMemOwn = false;
  }
%}
