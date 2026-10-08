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
* [Make](#make)
* [C Bridge Target](#c-bridge-target)
* [Self-signed Certificate](#self-signed-certificate)
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


## Self-signed Certificate

Server requires a TLS certificate and key file. For development and testing, use a self-signed certificate. Below
are the steps to create a self-signed certificate and private ey.

### OpenSSL/BorningSSL

```
cd build/examples/qclient
openssl req -nodes -x509 -newkey rsa:2048 -days 365 \
    -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org" \
    -keyout server-key.pem -out server-cert.pem
```

### MbedTLS

```
openssl req -nodes -x509 -newkey ec:<(openssl ecparam -name prime256v1) -days 365 \
    -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org" \
    -keyout server-key.pem -out server-cert.pem
```

OR

```
    openssl ecparam -name prime256v1 -genkey -noout -out server-key-ec.pem
    openssl req -nodes -x509 -key server-key-ec.pem -days 365 \
        -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org" \
        -keyout server-key.pem -out server-cert.pem

```

---

## Generate documentation
API documentation can be generated using `make doc`

Below programs need to be installed.

### Example on MacOS:

```bash
brew install doxygen
brew install npm
brew install pandoc
npm install --global @mermaid-js/mermaid-cli
npm install --global mermaid-filter
```

> [!NOTE]
> https://github.com/raghur/mermaid-filter adds mermaid support to pandoc
>

### MOQ Implementation Documentation

See [MOQ Implementation](docs/implementation)
