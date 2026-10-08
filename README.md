libquicr
========

[![Ubuntu](https://github.com/Quicr/libquicr/actions/workflows/ubuntu.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/ubuntu.yml)
[![macOS](https://github.com/Quicr/libquicr/actions/workflows/macos.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/macos.yml)
[![Windows](https://github.com/Quicr/libquicr/actions/workflows/windows.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/windows.yml)

An API library that implements publish/subscribe protocol [draft-ietf-moq-transport-16](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-16). The API supports both client and server. Server is intended to be implemented as a relay.

API documentation can be found under https://quicr.github.io/libquicr

## Requirements

### Tools

* GCC/G++ version 12 or higher
* Clang/llvm version 17 or higher
* AppleClang/llvm version 17 or higher
* Clang-tidy version 15 or higher
* Cmake 3.13 or higher

### Ubuntu 22.04 Jammy

```
sudo apt-get update
sudo apt-get install -y cmake make gcc-12 g++-12 clang-tidy-15 openssl golang wget git libssl-dev
```

> [!IMPORTANT]
> If default gcc/g++ is installed, then you will need to set the default
> compiler to gcc-12/g++-12.  You can do that using:
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

Both Apple Intel and Silicon are supported.


#### (1) Install Xcode

> [!NOTE]
> You **MUST** install xcode from Apple in order to get the base development programs.


Open the **App Store** and search for **Xcode** and install.

#### (2) Install Xcode Command Line Tools

You can install them via xcode UI or you can install them using `xcode-select --install` from a shell/terminal.


#### (3) Install Homebrew

Install via https://brew.sh instructions.

#### (4) Install packages via brew

```
brew install cmake clang-format
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

### TLS providers

[![Mbed TLS v3.6.7](https://img.shields.io/github/actions/workflow/status/Quicr/libquicr/mbedtls.yml?branch=main&label=Mbed%20TLS%20v3.6.7)](https://github.com/Quicr/libquicr/actions/workflows/mbedtls.yml)

> [!WARNING]
> For every provider, use a currently supported release, apply security updates promptly, and avoid
> mixing headers and libraries from different installations. Use a fresh build directory when
> switching providers so cached discovery results cannot select the previous TLS stack. Do not
> weaken the provider's TLS 1.3, certificate-validation, or secure-random configuration.

#### OpenSSL

The default build uses OpenSSL. Install a supported OpenSSL release through the platform package
manager and configure normally. For a non-standard installation, pass its prefix explicitly:

```
cmake -B build -DOPENSSL_ROOT_DIR=/path/to/openssl
```

#### Mbed TLS

To use Mbed TLS instead of OpenSSL, provide an installed or in-tree Mbed TLS build and enable the
provider:

```
cmake -B build -DWITH_MBEDTLS=ON
```

CMake searches standard installation paths automatically. The following variables must resolve to
valid Mbed TLS include directories and libraries; provide them explicitly for in-tree or
non-standard installations:

- `MBEDTLS_INCLUDE_DIR=<path>` — the primary include directory.
- `MBEDTLS_INCLUDE_DIRS=<paths>` — all required include directories. This is normally the same path
  as `MBEDTLS_INCLUDE_DIR`.
- `MBEDTLS_LIBRARY=<target-or-path>` — the `mbedtls` library.
- `MBEDTLS_X509=<target-or-path>` — the `mbedx509` library.
- `MBEDTLS_CRYPTO=<target-or-path>` — the `mbedcrypto` library.

For convenience, libquicr can fetch and build a pinned, tested Mbed TLS version:

```
cmake -B build -DWITH_MBEDTLS=ON -DQUICR_FETCH_MBEDTLS=ON
```

> [!IMPORTANT]
> The fetch option is intended for local development and CI, not packaged or production builds.
> Configuration requires network access, and the application cannot independently select and update
> Mbed TLS. Prefer a separately managed installation or in-tree target so security updates and build
> provenance remain under the application's control.

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
