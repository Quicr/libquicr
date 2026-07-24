# SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
# SPDX-License-Identifier: BSD-2-Clause

cmake_minimum_required(VERSION 3.13)

# Builds the SWIG Python bindings for libquicr (from ../../../../swig/quicr.i)
# and a tiny Python script that exercises them - the Python counterpart to
# ../go, driven by CMake the same way so that `cmake --build` alone
# reproduces what running swig/python by hand does.
#
# Both SWIG and Python (interpreter + development headers/library) are
# optional dependencies of the rest of the project, so this whole example
# is skipped (with a message, not an error) if either is missing.

find_package(SWIG 4.0 QUIET)
find_package(Python3 COMPONENTS Interpreter Development QUIET)

if(NOT SWIG_FOUND OR NOT Python3_FOUND)
    message(STATUS "Skipping SWIG Python example (qpyclient): SWIG and/or Python3 development files were not found")
    return()
endif()

cmake_policy(SET CMP0086 NEW)
cmake_policy(SET CMP0078 NEW)
include(UseSWIG)

# Directory layout under the build tree:
#   pypkg/   the generated `quicr` Python module (quicr.py + the compiled
#            _quicr extension module, e.g. _quicr.cpython-3xx-darwin.so)
#   app/     a throwaway copy of main.py, run directly out of pypkg/ via
#            PYTHONPATH (Python has no per-directory module-resolution
#            story equivalent to Go's go.mod/replace, so there is nothing
#            here analogous to the Go example's generated go.mod files)
set(QUICR_PY_PKG_DIR ${CMAKE_CURRENT_BINARY_DIR}/pypkg)
set(QUICR_PY_APP_DIR ${CMAKE_CURRENT_BINARY_DIR}/app)
file(MAKE_DIRECTORY ${QUICR_PY_PKG_DIR})
file(MAKE_DIRECTORY ${QUICR_PY_APP_DIR})

set_property(SOURCE ${PROJECT_SOURCE_DIR}/swig/quicr.i PROPERTY CPLUSPLUS ON)

# -threads: without it, SWIG's generated wrapper code for every *ordinary*
# wrapped call holds the GIL for the call's entire duration (blocking every
# other Python thread, including whichever one is meant to be servicing
# director callbacks arriving from libquicr's own background packet-I/O
# threads - see the director comment in main.py for why that specific
# combination matters here, not just for general responsiveness). It has
# no effect on the *director* call path itself either way - that part of
# SWIG's Python support always acquires the GIL itself, regardless of this
# flag; see main.py's own director comment for that half of the story.
set(CMAKE_SWIG_FLAGS -threads)
set(CMAKE_SWIG_OUTDIR ${QUICR_PY_PKG_DIR})
set(SWIG_OUTFILE_DIR ${CMAKE_CURRENT_BINARY_DIR}/wrap)

# UseSWIG derives the `-I...` flags it passes to the swig executable itself
# (as opposed to the flags used to *compile* the generated wrapper) from
# this directory-scoped property, not from target_include_directories().
include_directories(${CMAKE_BINARY_DIR}/include ${PROJECT_SOURCE_DIR}/include)

swig_add_library(quicr_python
    LANGUAGE python
    TYPE MODULE
    SOURCES ${PROJECT_SOURCE_DIR}/swig/quicr.i
)

set_target_properties(quicr_python PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS OFF
    LIBRARY_OUTPUT_DIRECTORY ${QUICR_PY_PKG_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${QUICR_PY_PKG_DIR}
    # UseSWIG's own LANGUAGE python handling always passes swig its own
    # `-interface _<OUTPUT_NAME>` (one leading underscore prepended
    # automatically - the standard "compiled half of a pure-Python module
    # proxy is named _modulename" Python C-extension convention; SWIG's
    # generated quicr.py always does `from . import _quicr` /
    # `import _quicr` to match, unconditionally), deriving that OUTPUT_NAME
    # from the target's own name (quicr_python) unless told otherwise - so
    # this has to be plain "quicr" (matching %module quicr, no underscore
    # of our own) for the two to actually agree: adding one here too would
    # double up with UseSWIG's own (this was caught by running the actual
    # built qpyclient_build - `import quicr` failed with "No module named
    # '__quicr'" - not by anything at either swig or C++-compile time).
    OUTPUT_NAME "quicr"
)

# quicr_python only needs to link quicr itself and the Python runtime:
# because it's a real CMake target (unlike a manual swig/g++ invocation),
# CMake resolves every one of quicr's transitive PUBLIC dependencies
# (spdlog, timeq, picoquic, picotls, sframe, OpenSSL/mbedTLS, fmt, ...)
# automatically, the same as the Go example's own quicr_go target.
target_link_libraries(quicr_python PRIVATE quicr Python3::Module)

if(LINT)
    include(Lint)
    lint(quicr_python)
endif()

configure_file(main.py ${QUICR_PY_APP_DIR}/main.py COPYONLY)

set(QUICR_PY_STAMP ${CMAKE_CURRENT_BINARY_DIR}/qpyclient.stamp)

# There is no Python build step analogous to Go's `go build` - main.py
# runs directly off the interpreter - so this "build" target only has to
# make sure quicr_python (the compiled _quicr extension module) and its
# companion quicr.py proxy both exist before anything tries to `import
# quicr` against ${QUICR_PY_PKG_DIR}; touching a stamp file gives it a
# real OUTPUT to depend on/be depended on, the same role qgoclient (an
# actual binary) plays for the Go example.
add_custom_command(
    OUTPUT ${QUICR_PY_STAMP}
    COMMAND ${CMAKE_COMMAND} -E touch ${QUICR_PY_STAMP}
    DEPENDS
        quicr_python
        $<TARGET_FILE:quicr_python>
        ${QUICR_PY_PKG_DIR}/quicr.py
        ${QUICR_PY_APP_DIR}/main.py
    COMMENT "Staged Python SWIG example (qpyclient)"
    VERBATIM
)

add_custom_target(qpyclient_build ALL DEPENDS ${QUICR_PY_STAMP})
