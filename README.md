libquicr
========

[![Ubuntu](https://github.com/Quicr/libquicr/actions/workflows/ubuntu.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/ubuntu.yml)
[![macOS](https://github.com/Quicr/libquicr/actions/workflows/macos.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/macos.yml)
[![Windows](https://github.com/Quicr/libquicr/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/windows.yml)

libquicr is a C++ implementation of Media over QUIC Transport (MOQT), currently targeting
[draft-ietf-moq-transport-18](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-18).
It provides APIs for client and server endpoints, including publishers, subscribers, and relays.

[API documentation](https://www.quicr.org/html/)

## Navigation

* [Requirements](#requirements)
  * [Tools](#tools)
  * [Ubuntu](#ubuntu-2204-jammy)
  * [Debian](#debian-bookworm)
  * [Apple/Mac](#applemac)
* [Dependencies](#dependencies)
  * [Caching dependency sources](#caching-dependency-sources)
* [CMake options](#cmake-options)
* [Make](#make)
* [C Bridge Target](#c-bridge-target)
* [Generate documentation](#generate-documentation)

## Requirements

### Tools

* CMake 3.16 or newer
* A C++20 compiler:
    * GCC 12 or newer
    * Clang 17 or newer
    * Apple Clang 17 or newer
    * or MSVC
* Clang-Tidy 15 or newer when configuring with `-DLINT=ON`

### Ubuntu 22.04 Jammy

```
sudo apt-get update
sudo apt-get install -y cmake make gcc-12 g++-12 clang-tidy-15 openssl golang wget git libssl-dev
```

> [!IMPORTANT]
> If default gcc/g++ is installed, then you will need to set the default
> compiler to at least gcc-12/g++-12.  You can do that using:
> `export CC=/usr/bin/gcc-12`
> and `export CXX=/usr/bin/g++-12`

### Debian Bookworm

```
sudo apt-get update
sudo apt-get install -y make wget git cmake openssl golang libssl-dev clang-tidy-15
```

> [!NOTE]
> GCC/G++ version 12 is default on bookworm and will install via
> dependency of the above packages.

### Apple/Mac

Framework builds support both Intel and Apple Silicon. Install
[Xcode](https://apps.apple.com/app/xcode/id497799835) and [Homebrew](https://brew.sh), then run:

```
xcode-select --install
brew install cmake
```

## Dependencies

libquicr's C/C++ dependencies are fetched at CMake configure time by
[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake). There is nothing to install by hand and no
submodules to initialise — configuring the project is enough. `cmake/CPM.cmake` first downloads a
pinned CPM release and checks it against a recorded SHA-256, and each dependency is then pinned to
its own tag or commit:

| Dependency | Declared in |
| --- | --- |
| picoquic, picotls, timeq, Mbed TLS | `cmake/QuicrDependencies.cmake` |
| fmt | `cmake/QuicrFormat.txt` |
| doctest | `test/CMakeLists.txt` |
| Google Benchmark | `benchmark/CMakeLists.txt` |
| nlohmann/json, sframe, spdlog | `examples/qclient/CMakeLists.txt` |

Mbed TLS is only fetched when configuring with `-DWITH_MBEDTLS=ON`. The default build links the
system OpenSSL instead, which is why OpenSSL is the one TLS dependency you still install yourself.

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

Entries are keyed by package name and version, so separate build directories, `WITH_MBEDTLS=ON` and
`OFF` builds, and branch switches all share the same clones and a clean rebuild costs no network.
The cache also holds the bootstrapped CPM release itself, so the download in `cmake/CPM.cmake` is
skipped as well.

## CMake options

libquicr provides CMake options for selecting build targets and optional features. For example,
configure a build with examples enabled:

```
cmake -B build -DQUICR_BUILD_EXAMPLES=ON
```

Build targets:

* `QUICR_BUILD_C_BRIDGE` — build the C API bridge.
* `QUICR_BUILD_EXAMPLES` — build examples.
* `QUICR_BUILD_TESTS` — build the test suite.
* `QUICR_BUILD_BENCHMARKS` — build benchmarks.
* `QUICR_BUILD_FUZZ` — build Clang fuzzing targets.

Tests, benchmarks, and the C Bridge default to `ON` for a standalone build and `OFF` when libquicr
is included by another CMake project. Examples and fuzzers default to `OFF`. When both the C Bridge
and examples are enabled, the C Bridge examples are built automatically. `BUILD_TESTING=ON` is
also required for the test suite, benchmarks, fuzzers, and C++ examples.

Library configuration:

* `QUICR_BUILD_SHARED` — build libquicr as a shared library; defaults to `OFF`.
* `WITH_MBEDTLS` — use Mbed TLS instead of OpenSSL; defaults to `OFF`.
* `QUICR_USE_BROTLI` — enable Brotli TLS certificate compression; defaults to `OFF`.
* `QUICR_ENABLE_SANITIZERS` — enable AddressSanitizer and UndefinedBehaviorSanitizer with Clang or
  GCC; defaults to `OFF`.
* `LINT` — run Clang-Tidy while compiling; defaults to `OFF`.

## Make

Use `make` to build libquicr.

Use `make test` to run tests.

Use `make cclean` to clean build files.

Use `make fuzz` to run the fuzzer tests.

## C Bridge Target

libquicr includes C Bridge interfaces for C applications:

### Building C Bridge
The C Bridge provides a C wrapper around the C++ libquicr library.

See [c-bridge/README.md](c-bridge/README.md) for details.

## Generate documentation

Run `make doc` from the repository root to generate the Doxygen API reference and
render the API guide. The output is written to `docs/html/`; open
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
