# TLS provider configuration

An external TLS provider is optional: libquicr can use picotls-minicrypto on its own, or applications
can provide an OpenSSL-compatible provider or Mbed TLS when desired. OpenSSL-compatible providers
are used automatically when found; `WITH_MBEDTLS=ON` selects Mbed TLS instead. Use a fresh build
directory when switching providers so cached discovery results cannot select a previous
installation.

> [!WARNING]
> Use a currently supported provider release and apply security updates promptly. Do not mix
> headers and libraries from different installations or weaken TLS 1.3, certificate validation, or
> secure-random configuration.

## No external TLS backend

When no external provider is available, the build falls back to `picotls-minicrypto`, so minimal
systems do not need OpenSSL, BoringSSL, or Mbed TLS:

```bash
cmake -B build
```

QUIC still uses TLS 1.3 through picotls; this configuration only removes the external provider.
To test this path on a system where OpenSSL is installed, disable its package discovery explicitly:

```bash
cmake -B build -DCMAKE_DISABLE_FIND_PACKAGE_OpenSSL=TRUE
```

> [!WARNING]
> The current picoquic minicrypto integration does not verify peer certificates, so clients do not
> authenticate servers in this mode. Use it only where peer authentication is not required.

## OpenSSL-compatible providers

Providers exposing an OpenSSL-compatible API, including OpenSSL and BoringSSL, are detected and
enabled automatically through CMake's OpenSSL discovery interface:

```bash
cmake -B build
```

For a non-standard OpenSSL-compatible provider, pass its installation prefix:

```bash
cmake -B build -DOPENSSL_ROOT_DIR=/path/to/provider
```

### BoringSSL

For a static BoringSSL installation, picotls also expects `libdecrepit.a` beside the installed
`libcrypto.a`. BoringSSL's install step may omit it, so copy it from the build directory before
configuring libquicr:

```bash
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

## Mbed TLS

Applications can provide an installed or in-tree Mbed TLS build:

```bash
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
