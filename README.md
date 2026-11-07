libquicr — Media over QUIC (MoQ) library
========================================

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=c%2B%2B)](#requirements)
[![MOQT draft-18](https://img.shields.io/badge/MOQT-draft--18-blue)](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-18)

[![Ubuntu](https://github.com/Quicr/libquicr/actions/workflows/ubuntu.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/ubuntu.yml)
[![macOS](https://github.com/Quicr/libquicr/actions/workflows/macos.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/macos.yml)
[![Windows](https://github.com/Quicr/libquicr/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/windows.yml)
[![Android](https://github.com/Quicr/libquicr/actions/workflows/android.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/android.yml)

[![REUSE status](https://api.reuse.software/badge/github.com/Quicr/libquicr)](https://api.reuse.software/info/github.com/Quicr/libquicr)
[![License: BSD-2-Clause](https://img.shields.io/badge/License-BSD--2--Clause-blue.svg)](LICENSE)

Media over QUIC (MoQ) is a publish/subscribe architecture for low-latency delivery of live media
and other time-sensitive data.

libquicr implements Media over QUIC Transport (MOQT) with a C++ API for client and server endpoints
that publish, subscribe, fetch, and relay. It currently targets
[draft-ietf-moq-transport-18](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-18).

## Key features

* ⚡ **Low-latency media** — prioritized streams, datagrams, and TTL-based expiry.
* 🚀 **Flexible transport options** — native QUIC and WebTransport over HTTP/3.
* 🧩 **Handler-driven architecture** — build publish, subscribe, fetch, and relay workflows.
* 🔌 **C++ API** — integrate directly with static or shared-library builds.
* 🌐 **Cross-platform** — desktop, mobile, and embedded targets.
* 🛡️ **Modern foundation** — C++20 with TLS 1.3 transport.
* 🛠️ **Practical tooling** — interactive MOQT client, capture and replay, examples, and metrics.

## Quick start

After installing the [build requirements](#requirements), configure and build the examples:

```bash
cmake -B build -DBUILD_TESTING=ON -DQUICR_BUILD_EXAMPLES=ON
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

Fetch a pinned libquicr commit from another CMake project:

```cmake
include(FetchContent)
FetchContent_Declare(
    libquicr
    GIT_REPOSITORY https://github.com/Quicr/libquicr.git
    GIT_TAG d010d77f3cf31af2668aa6dd37e29832f8d616a1
)
FetchContent_MakeAvailable(libquicr)
```

<details>
<summary>Vendored source and CPM alternatives</summary>

For vendored source, use:

```cmake
add_subdirectory(path/to/libquicr)
```

Projects already using [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) can use:

```cmake
CPMAddPackage("gh:Quicr/libquicr#d010d77f3cf31af2668aa6dd37e29832f8d616a1")
```

</details>

Link the library target after adding it:

```cmake
target_link_libraries(your_target PRIVATE quicr)
```

Installed-package support using `find_package` is planned in
[PR #1033](https://github.com/Quicr/libquicr/pull/1033). Until versioned releases are available,
pin remote integrations to a commit as shown above.

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
| Linux | ✅ Supported | ✅ Tested in CI |
| macOS | ✅ Supported | ✅ Tested in CI |
| Windows | ✅ Supported | ✅ Tested in CI |
| Android | ✅ Supported | Not in CI |
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
| [picotls-minicrypto](#no-external-tls-backend) | ✅ Supported | Bundled | Fallback when no external provider is selected |
| [OpenSSL-compatible providers](#openssl-compatible-providers) | ✅ Supported | [![OpenSSL 3 (CI)](https://img.shields.io/badge/OpenSSL-3%20%28CI%29-blue)](https://www.openssl.org/) [![BoringSSL 0.20260929.0](https://img.shields.io/badge/BoringSSL-0.20260929.0-blue)](https://github.com/google/boringssl/tree/0.20260929.0) | OpenSSL 3 is tested through Ubuntu and Homebrew packages; see [BoringSSL limitations](#boringssl) |
| [Mbed TLS](#mbed-tls) | ✅ Supported | [![Mbed TLS 3.6.7](https://img.shields.io/badge/Mbed%20TLS-3.6.7-blue)](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7) | Enable with `WITH_MBEDTLS=ON` |

See [TLS provider configuration](#tls-provider-configuration) for setup details and security
caveats.

## Requirements

### Build tools

* CMake 3.16 or newer
* Git for dependency fetching
* Supported C++20 toolchains:
    * GCC 12 or newer
    * Clang 17 or newer
    * Apple Clang 17 or newer
    * or MSVC
* Clang-Tidy 15 or newer when configuring with `-DLINT=ON`

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
| [picoquic](https://github.com/private-octopus/picoquic/commit/1d81a730ebbc0f4598f8127a7f43260a71a9ac7d) | QUIC transport | [`cmake/QuicrDependencies.cmake`](cmake/QuicrDependencies.cmake) |
| [picotls](https://github.com/Quicr/picotls/tree/057ed54) | TLS 1.3 implementation | [`cmake/QuicrDependencies.cmake`](cmake/QuicrDependencies.cmake) |
| [timeq](https://github.com/Quicr/timeq/tree/898d45c) | Timed task scheduling | [`cmake/QuicrDependencies.cmake`](cmake/QuicrDependencies.cmake) |
| [fmt](https://github.com/fmtlib/fmt/tree/12.2.0) | Formatting fallback when `std::format` is unavailable | [`cmake/QuicrFormat.cmake`](cmake/QuicrFormat.cmake) |

Optional [tests](test/README.md), [benchmarks](benchmark/README.md), [tools](tools/README.md), and
[examples](examples/README.md) may fetch additional dependencies. See their documentation for
target-specific build and dependency notes.

### Caching dependency sources

By default CPM clones each dependency into the build tree under `build/_deps`, so removing the
build directory with `make cclean` (or `rm -rf build`) discards the sources and the next configure
re-downloads all of them.

Setting `CPM_SOURCE_CACHE` to a directory outside the build tree keeps a single shared copy
instead. It is read from the environment:

```
export CPM_SOURCE_CACHE=$HOME/.cache/CPM
```

or passed per configure:

```
cmake -B build -DCPM_SOURCE_CACHE=$HOME/.cache/CPM
```

Entries are keyed by package name and version, so separate build directories, crypto backend
selections, and branch switches all share the same clones and a clean rebuild costs no network.
The cache also holds the bootstrapped CPM release itself, so the download in `cmake/CPM.cmake` is
skipped as well.

### Third-party license notices

[CPMLicenses.cmake](https://github.com/cpm-cmake/CPMLicenses.cmake) can collect license and
copyright files from CPM dependencies distributed with libquicr. Enable its target with
`QUICR_GENERATE_THIRD_PARTY_LICENSES`:

```bash
cmake -B build -DQUICR_GENERATE_THIRD_PARTY_LICENSES=ON
cmake --build build --target third-party-licenses
```

Alternatively, run:

```bash
make licenses
```

Both commands write `build/THIRD_PARTY_LICENSES.txt`. The target intentionally includes only
`timeq`, `picotls`, `picoquic`, and conditional `fmt`; test, benchmark, example, and build-tool
dependencies are excluded.

CPMLicenses searches dependency roots for files beginning with `LICENSE`, `LICENCE`, `COPYING`, or
`Copyright` and uses only the first match. Review its warnings and generated output: dependencies
without a matching root file, or repositories containing multiple license files, require manual
review. This notice file complements rather than replaces the product SBOM and license-policy
checks.

## CMake options

[![Sanitizers](https://github.com/Quicr/libquicr/actions/workflows/sanitizers.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/sanitizers.yml)
[![Fuzz](https://github.com/Quicr/libquicr/actions/workflows/fuzz.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/fuzz.yml)

libquicr provides CMake options for selecting build targets and optional features. For example,
configure a build with examples enabled:

```
cmake -B build -DQUICR_BUILD_EXAMPLES=ON
```

Build targets:

* `QUICR_BUILD_C_BRIDGE` — build the C API bridge.
* `QUICR_BUILD_EXAMPLES` — build examples.
* `QUICR_BUILD_TOOLS` — build testing and debugging tools.
* `QUICR_BUILD_TESTS` — build the test suite.
* `QUICR_BUILD_BENCHMARKS` — build benchmarks.
* `QUICR_BUILD_FUZZ` — build Clang fuzzing targets.

Tests, benchmarks, and the C Bridge default to `ON` for a standalone build and `OFF` when libquicr
is included by another CMake project. Tools, examples, and fuzzers default to `OFF`. When both the
C Bridge and examples are enabled, the C Bridge examples are built automatically.
`BUILD_TESTING=ON` is also required for the test suite, benchmarks, fuzzers, tools, and C++
examples.

Library configuration:

* `QUICR_BUILD_SHARED` — build libquicr as a shared library; defaults to `OFF`.
* `QUICR_ENABLE_SANITIZERS` — enable AddressSanitizer and UndefinedBehaviorSanitizer with Clang or
  GCC; defaults to `OFF`.
* `QUICR_GENERATE_THIRD_PARTY_LICENSES` — add the `third-party-licenses` target for distributed CPM
  dependencies; defaults to `OFF`.
* `LINT` — run Clang-Tidy while compiling; defaults to `OFF`.

Picotls configuration used by the build:

* `WITH_MBEDTLS` — use Mbed TLS instead of OpenSSL; defaults to `OFF`.
* `WITH_BROTLI` — enable Brotli TLS certificate compression; defaults to `OFF`.

## Makefile Shortcuts

| Command | Action |
| --- | --- |
| `make` | Build libquicr, tests, benchmarks, examples, tools, and the C bridge in Debug mode |
| `make test` | Build and run the test suite |
| `make cclean` | Remove the build directory |
| `make fuzz` | Build and run the control-message fuzzer |
| `make licenses` | Generate `build/THIRD_PARTY_LICENSES.txt` |
| `make doc` | Generate the Doxygen reference and API guide; requires Doxygen, Pandoc, Mermaid CLI, and `mermaid-filter` |

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
cmake -B build -DBUILD_TESTING=ON -DQUICR_BUILD_TOOLS=ON
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

### No external TLS backend

When no external provider is available, the build falls back to `picotls-minicrypto`, so minimal
systems do not need OpenSSL, BoringSSL, or Mbed TLS:

```
cmake -B build
```

QUIC still uses TLS 1.3 through picotls; this configuration only removes the external provider.
To test this path on a system where OpenSSL is installed, disable its package discovery explicitly:

```
cmake -B build -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE
```

> [!WARNING]
> The current picoquic minicrypto integration does not verify peer certificates, so clients do not
> authenticate servers in this mode. Use it only where peer authentication is not required.

### OpenSSL-compatible providers

Providers exposing an OpenSSL-compatible API, including OpenSSL and BoringSSL, are detected and
enabled automatically through CMake's OpenSSL discovery interface:

```
cmake -B build
```

For a non-standard OpenSSL-compatible provider, pass its installation prefix:

```
cmake -B build -DOPENSSL_ROOT_DIR=/path/to/provider
```

### BoringSSL

For a static BoringSSL installation, picotls also expects `libdecrepit.a` beside the installed
`libcrypto.a`. BoringSSL's install step may omit it, so copy it from the build directory before
configuring libquicr:

```
cmake -E copy /path/to/boringssl-build/libdecrepit.a \
    /path/to/boringssl-install/lib/libdecrepit.a
cmake -B build -DOPENSSL_ROOT_DIR=/path/to/boringssl-install
```

This library is required for picotls compatibility; applications should not directly use the
deprecated algorithms it exposes.

> [!NOTE]
> BoringSSL is currently tested and supported through this interface on Linux. Static BoringSSL
> installations do not configure with picotls on macOS because picotls' BoringSSL adjustment logic
> does not recognize CMake's `AppleClang` compiler ID when selecting the required C++ standard
> library. Until that upstream limitation is fixed, use OpenSSL, Mbed TLS, or picotls-minicrypto on
> macOS.

### Mbed TLS

Applications can provide an installed or in-tree Mbed TLS build:

```
cmake -B build -DWITH_MBEDTLS=ON
```

CMake searches standard installation paths. For an in-tree or non-standard build, provide:

- `MBEDTLS_INCLUDE_DIR=<path>` — primary include directory.
- `MBEDTLS_INCLUDE_DIRS=<paths>` — all required include directories.
- `MBEDTLS_LIBRARY=<target-or-path>` — `mbedtls`.
- `MBEDTLS_X509=<target-or-path>` — `mbedx509`.
- `MBEDTLS_CRYPTO=<target-or-path>` — `mbedcrypto`.

libquicr never fetches Mbed TLS. The consuming application is responsible for selecting, updating,
and supplying it.

## C Bridge

The optional C bridge provides a client-only C wrapper around the C++ library. See the
[C bridge documentation](c-bridge/README.md) for build instructions and API details.

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
