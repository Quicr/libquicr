<img width="100%" src=".github/logo.svg" alt="libquicr">

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B)](#requirements)
[![MOQT draft-18](https://img.shields.io/badge/MOQT-draft--18-blue)](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-18)

[![Ubuntu](https://github.com/Quicr/libquicr/actions/workflows/ubuntu.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/ubuntu.yml)
[![macOS](https://github.com/Quicr/libquicr/actions/workflows/macos.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/macos.yml)
[![Windows](https://github.com/Quicr/libquicr/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/windows.yml)

[![REUSE status](https://api.reuse.software/badge/github.com/Quicr/libquicr)](https://api.reuse.software/info/github.com/Quicr/libquicr)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue.svg)](LICENSE)

libquicr — Media over QUIC (MoQ) library
========================================

Media over QUIC (MoQ) is a protocol suite for low-latency delivery of live media and other
time-sensitive data over QUIC.

libquicr implements Media over QUIC Transport (MOQT) with a C++ API for client and server endpoints
that publish, subscribe, fetch, and relay. It currently targets
[draft-ietf-moq-transport-18](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-18).

## Key features

* ⚡ **Low-latency media** — prioritized streams, datagrams, and TTL-based expiry.
* 🚀 **Flexible transport options** — native QUIC and WebTransport over HTTP/3.
* 🧩 **Handler-driven architecture** — build publish, subscribe, fetch, and relay workflows.
* 🔌 **C/C++ API** — native interfaces for direct use and cross-language integration.
* 🌐 **Cross-platform** — desktop, mobile, and embedded targets.
* 🛡️ **Modern foundation** — C++20 with TLS 1.3 transport.
* 🛠️ **Practical tooling** — interactive MOQT client, capture and replay, examples, and metrics.

## Quick start

After installing the [build requirements](#requirements), configure and build the examples:

```bash
cmake -B build -DQUICR_BUILD_EXAMPLES=ON
cmake --build build
```

Run the subscriber and publisher in separate terminals:

```bash
./build/examples/simple_subscribe/simple_subscribe
./build/examples/simple_publish/simple_publish
```

The examples require an accessible MOQT relay. Both prompt for its URI, a track namespace, and a
track name; use the same values to publish and receive messages.

## CMake integration

Consume an installed libquicr package through its exported target:

```cmake
find_package(quicr CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE quicr::quicr)
```

For an installation outside standard search paths, add its prefix to `CMAKE_PREFIX_PATH` when
configuring the consuming project. When the C bridge is installed, its exported target is
`quicr::bridge`.

<details>
<summary>Installing or adding libquicr from source</summary>

**Build and install**

```bash
cmake -S . -B build -DQUICR_INSTALL=ON
cmake --build build
cmake --install build --prefix "/path/to/install"
```

The local prefix avoids administrator permissions. Omit `--prefix` to use the configured
`CMAKE_INSTALL_PREFIX`.

**FetchContent**

```cmake
include(FetchContent)
FetchContent_Declare(
    libquicr
    GIT_REPOSITORY https://github.com/Quicr/libquicr.git
    GIT_TAG 980a887a6ca64b5a422102a0507a0a93fa0df081
)
FetchContent_MakeAvailable(libquicr)
```

**Vendored source**

```cmake
add_subdirectory(path/to/libquicr)
```

**[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake)**

```cmake
CPMAddPackage("gh:Quicr/libquicr#980a887a6ca64b5a422102a0507a0a93fa0df081")
```

All source integration methods expose the `quicr::quicr` target shown above.

</details>

See the [API guide](docs/api-guide.md) for session and handler usage.

## Contents

* [Support](#support)
* [Requirements](#requirements)
* [Dependencies](#dependencies)
* [CMake options](#cmake-options)
* [Makefile Shortcuts](#makefile-shortcuts)
* [Command-line tools](#command-line-tools)
* [TLS provider configuration](#tls-provider-configuration)
* [C Bridge](#c-bridge)
* [Documentation](#documentation)

## Support

**Status:** ✅ Supported · 🧪 Manually tested (not in CI) · 🚧 Planned

### Transport options

| Option | Status | Selection | Notes |
| --- | --- | --- | --- |
| Native QUIC | ✅ Supported | `moq://` or `moqt://` | |
| WebTransport over HTTP/3 | ✅ Supported | `https://` | |
| QMUX | 🚧 Planned | To be determined | [Tracking issue #1044](https://github.com/Quicr/libquicr/issues/1044) |

### Platforms

| Platform | Build | Automated tests |
| --- | --- | --- |
| [Linux](https://github.com/Quicr/libquicr/actions/workflows/ubuntu.yml) | ✅ Supported | ✅ Tested in CI |
| [macOS](https://github.com/Quicr/libquicr/actions/workflows/macos.yml) | ✅ Supported | ✅ Tested in CI |
| [Windows](https://github.com/Quicr/libquicr/actions/workflows/windows.yml) | ✅ Supported | ✅ Tested in CI |
| [Android](https://github.com/Quicr/libquicr/actions/workflows/android.yml) | ✅ Supported | Not in CI |
| iOS | 🧪 Manually tested | Not in CI |
| ESP32 | 🧪 Manually tested | Not in CI |

### APIs

| API | Status | Notes |
| --- | --- | --- |
| C++ API | ✅ Supported | Requires C++20 |
| C bridge | ✅ Supported | Client-only API |
| C API | 🚧 Planned | Replacement tracked in [PR #1046](https://github.com/Quicr/libquicr/pull/1046) |

### TLS backends

[![TLS Backends](https://github.com/Quicr/libquicr/actions/workflows/tls-backends.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/tls-backends.yml)

TLS backend selection applies to both native QUIC and WebTransport.

| Backend | Status | Known versions | Notes |
| --- | --- | --- | --- |
| [picotls-minicrypto](docs/tls.md#no-external-tls-backend) | ✅ Supported | Bundled | Fallback when no external provider is selected |
| [OpenSSL-compatible providers](docs/tls.md#openssl-compatible-providers) | ✅ Supported | [![OpenSSL 3 (CI)](https://img.shields.io/badge/OpenSSL-3%20%28CI%29-blue)](https://www.openssl.org/) [![BoringSSL 0.20260929.0](https://img.shields.io/badge/BoringSSL-0.20260929.0-blue)](https://github.com/google/boringssl/tree/0.20260929.0) | OpenSSL 3 is tested through Ubuntu and Homebrew packages; see [BoringSSL limitations](docs/tls.md#boringssl) |
| [Mbed TLS](docs/tls.md#mbed-tls) | ✅ Supported | [![Mbed TLS 3.6.7](https://img.shields.io/badge/Mbed%20TLS-3.6.7-blue)](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7) | Enable with `WITH_MBEDTLS=ON` |

See the [TLS provider guide](docs/tls.md) for setup details and backend-specific limitations.

## Requirements

### Build tools

* CMake 3.16 or newer
* Git for dependency fetching
* Supported C++20 toolchains:
    * GCC 12 or newer
    * Clang 17 or newer
    * Apple Clang 17 or newer
    * or MSVC
* CMake 3.26 or newer and Clang-Tidy 15 or newer when configuring with `-DQUICR_LINT=ON`

> [!NOTE]
> Ubuntu 22.04 defaults to GCC 11. Install GCC 12 or newer and select it before configuring, for
> example with `CC=/usr/bin/gcc-12` and `CXX=/usr/bin/g++-12`.

## Dependencies

Most of libquicr's C/C++ dependencies are fetched at CMake configure time by
[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake). There are no submodules to initialise.
External TLS backends are optional and remain under the consuming application's control.
`cmake/CPM.cmake` first downloads a pinned CPM release and checks it against a recorded SHA-256,
and each fetched dependency is then pinned to its own tag or commit:

| Dependency | Purpose | Declared in |
| --- | --- | --- |
| [picoquic](https://github.com/private-octopus/picoquic/tree/v2026.10.25.1d81a73) | QUIC transport | [`cmake/QuicrDependencies.cmake`](cmake/QuicrDependencies.cmake) |
| [picotls](https://github.com/Quicr/picotls/tree/057ed54) | TLS 1.3 implementation | [`cmake/QuicrDependencies.cmake`](cmake/QuicrDependencies.cmake) |
| [timeq](https://github.com/Quicr/timeq/tree/v1.0.0) | Timed task scheduling | [`cmake/QuicrDependencies.cmake`](cmake/QuicrDependencies.cmake) |
| [fmt](https://github.com/fmtlib/fmt/tree/12.2.0) | Formatting fallback when `std::format` is unavailable | [`cmake/QuicrFormat.cmake`](cmake/QuicrFormat.cmake) |

Optional [tests](test/README.md), [benchmarks](benchmark/README.md), [tools](tools/README.md), and
[examples](examples/README.md) may fetch additional dependencies. See their documentation for
target-specific build and dependency notes.

See the [dependency guide](docs/dependencies.md) for source caching and third-party licence notices.

## CMake options

[![Sanitizers](https://github.com/Quicr/libquicr/actions/workflows/sanitizers.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/sanitizers.yml)
[![Fuzz](https://github.com/Quicr/libquicr/actions/workflows/fuzz.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/fuzz.yml)

libquicr provides CMake options for selecting build targets and optional features. For example,
configure a build with examples enabled:

```
cmake -B build -DQUICR_BUILD_EXAMPLES=ON
```

| Option | Default | Purpose |
| --- | --- | --- |
| `QUICR_INSTALL` | Standalone: `ON`; subproject: `OFF` | Enable install and CMake package rules |
| `QUICR_BUILD_C_BRIDGE` | Standalone: `ON`; subproject: `OFF` | Build the C bridge |
| `QUICR_BUILD_EXAMPLES` | `OFF` | Build examples |
| `QUICR_BUILD_TOOLS` | `OFF` | Build testing and debugging tools |
| `QUICR_BUILD_TESTS` | Standalone: `ON`; subproject: `OFF` | Build the test suite |
| `QUICR_BUILD_BENCHMARKS` | Standalone: `ON`; subproject: `OFF` | Build benchmarks |
| `QUICR_BUILD_FUZZ` | `OFF` | Build Clang fuzzing targets |
| `QUICR_BUILD_SHARED` | `OFF` | Build libquicr as a shared library |
| `QUICR_ENABLE_SANITIZERS` | `OFF` | Enable AddressSanitizer and UndefinedBehaviorSanitizer |
| `QUICR_GENERATE_THIRD_PARTY_LICENSES` | `OFF` | Add the `third-party-licenses` target |
| `QUICR_LINT` | `OFF` | Run Clang-Tidy while compiling a standalone build |
| `WITH_MBEDTLS` | `OFF` | Select the picotls Mbed TLS backend |
| `WITH_BROTLI` | `OFF` | Enable picotls Brotli TLS certificate compression |

`BUILD_TESTING` defaults to `ON` for standalone builds and gates tests, benchmarks, and fuzzers.
When both the C bridge and examples are enabled, the bridge examples are built automatically. The
`WITH_*` options configure picotls rather than libquicr itself.

## Makefile Shortcuts

| Command | Action |
| --- | --- |
| `make` | Build libquicr, tests, benchmarks, examples, tools, and the C bridge in Debug mode |
| `make test` | Build and run the test suite |
| `make cclean` | Remove the build directory |
| `make fuzz` | Build and run the control-message fuzzer |
| `make licenses` | Generate `build/THIRD_PARTY_LICENSES.txt` |
| `make doc` | Generate the Doxygen reference and API guide; requires Doxygen, Pandoc, Mermaid CLI, and `mermaid-filter` |
| `make package-lock` | Regenerate `package-lock.cmake` |

## Command-line tools

The [`tools`](tools/README.md) directory contains utilities for testing and debugging
MoQ networking. It is separate from [`examples`](examples/README.md), which contains small programs
focused on demonstrating individual APIs.

The available tools are:

- `moq-cli`, an interactive client for publishing, subscribing, fetching, and protocol test
  scenarios.
- `moq_capture`, a client for recording track objects and replaying recorded data.

Configure and build the tools with:

```bash
cmake -B build -DQUICR_BUILD_TOOLS=ON
cmake --build build
```

See the [`tools` documentation](tools/README.md) for usage.

## TLS provider configuration

An external TLS provider is optional: libquicr can use picotls-minicrypto on its own, or applications
can provide an OpenSSL-compatible provider or Mbed TLS when desired. OpenSSL-compatible providers
are used automatically when found; `WITH_MBEDTLS=ON` selects Mbed TLS instead. Use a fresh build
directory when switching providers so cached discovery results cannot select a previous
installation.

> [!WARNING]
> Use a currently supported provider release and apply security updates promptly. Do not mix
> headers and libraries from different installations or weaken TLS 1.3, certificate validation, or
> secure-random configuration.

> [!WARNING]
> The current picoquic minicrypto integration does not verify peer certificates, so clients do not
> authenticate servers in this mode. Use it only where peer authentication is not required.

See the [TLS provider guide](docs/tls.md) for provider selection and configuration.

## C Bridge

The optional C bridge provides a client-only C wrapper around the C++ library. Enable it with
`QUICR_BUILD_C_BRIDGE=ON`; also enable `QUICR_BUILD_EXAMPLES` to build its examples. See the
[C bridge documentation](c-bridge/README.md) for API details.

## Documentation

Browse the [pre-built documentation](https://quicr.github.io/libquicr/), or run
`make doc` from the repository root to generate the Doxygen API reference and
render the API guide locally. The output is written to `docs/html/`; open
`docs/html/index.html` in a browser to view it.

### Prerequisites on macOS

```bash
brew install doxygen node pandoc
npm install --global @mermaid-js/mermaid-cli
npm install --global mermaid-filter
```

> [!NOTE]
> [`mermaid-filter`](https://github.com/raghur/mermaid-filter) allows Pandoc to
> render the Mermaid diagrams used by the API guide.

### Additional documentation

* [API guide](docs/api-guide.md)
* [MOQT implementation notes](docs/implementation.md)
