/* Python-specific %extend/%feature/%rename/%insert additions - counterpart
 * to python/typemaps.i's %typemap/%fragment half. Only ever %include'd
 * under #ifdef SWIGPYTHON in quicr.i.
 */

/* All five TrackHandler subclasses below are director-enabled classes
 * with a protected constructor and a %feature("nodirector")'d pure
 * virtual ancestor (see quicr.i's comment on TrackHandler::
 * RequestUpdateReceived()), which makes SWIG's director-completeness
 * check treat each as abstract (Warning 517, already filtered in
 * quicr.i), even though none actually is.
 *
 * Go's constructor generation never consults that signal, so it's
 * unaffected. Python's does, and without this override, constructing a
 * Python subclass fails at runtime with `AttributeError: No constructor
 * defined - class is abstract` (see swig/SWIG_WARNINGS.md).
 *
 * Must run before quicr.i's class declarations for these are parsed -
 * true regardless of where in this file it sits, since this whole file
 * is %include'd near the top of quicr.i. */
%feature("notabstract") quicr::SubscribeTrackHandler;
%feature("notabstract") quicr::PublishTrackHandler;
%feature("notabstract") quicr::FetchTrackHandler;
%feature("notabstract") quicr::PublishNamespaceHandler;
%feature("notabstract") quicr::SubscribeNamespaceHandler;

/* Global exception translation: without this, any C++ exception a
 * wrapped function throws (e.g. StreamHeaderProperties's constructor,
 * TrackNamespace's) propagates uncaught across the Python/C boundary and
 * aborts the whole process (SIGABRT) instead of raising a normal Python
 * exception - found via StreamHeaderProperties, see swig/SWIG_WARNINGS.md. */
%exception {
    try {
        $action
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}
