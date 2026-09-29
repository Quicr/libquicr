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

Most of libquicr's C/C++ dependencies are fetched at CMake configure time by
[CPM.cmake](https://github.com/cpm-cmake/CPM.cmake). There are no submodules to initialise, but the
TLS provider is supplied by the user. `cmake/CPM.cmake` first downloads a pinned CPM release and
checks it against a recorded SHA-256, and each fetched dependency is then pinned to its own tag or
commit:

| Dependency | Declared in |
| --- | --- |
| picoquic, picotls, timeq | `cmake/QuicrDependencies.cmake` |
| fmt | `cmake/QuicrFormat.txt` |
| doctest | `test/CMakeLists.txt` |
| Google Benchmark | `benchmark/CMakeLists.txt` |
| nlohmann/json, sframe, spdlog | `examples/qclient/CMakeLists.txt` |

### TLS providers

The default build uses OpenSSL. Install a supported OpenSSL release through the platform package
manager and configure normally. For a non-standard installation, pass its prefix explicitly:

```
cmake -B build -DOPENSSL_ROOT_DIR=/path/to/openssl
```

BoringSSL can be used through its OpenSSL compatibility interface. Build and install a pinned
BoringSSL revision, keep it isolated from the system OpenSSL installation, and pass its installation
prefix in the same way:

```
cmake -B build -DOPENSSL_ROOT_DIR=/path/to/boringssl-install
```

BoringSSL does not provide stable API or ABI compatibility between revisions, so applications
should vendor or otherwise pin the exact tested revision. The BoringSSL job in
`.github/workflows/tls-backends.yml` is the reference build configuration.

To use Mbed TLS, provide an installed or in-tree Mbed TLS build and configure with
`-DWITH_MBEDTLS=ON`. Its location can be supplied through the `MBEDTLS_ROOT_DIR` and
`MBEDTLS_PREFIX` CMake variables when it is not in a standard search path. In-tree providers can
instead set `MBEDTLS_INCLUDE_DIR`, `MBEDTLS_INCLUDE_DIRS`, `MBEDTLS_LIBRARY`, `MBEDTLS_X509`, and
`MBEDTLS_CRYPTO`.

For every provider, use a currently supported release, apply security updates promptly, and avoid
mixing headers and libraries from different installations. Use a fresh build directory when
switching providers so cached discovery results cannot select the previous TLS stack. Do not weaken
the provider's TLS 1.3, certificate-validation, or secure-random configuration.

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
