# Tests

libquicr has unit tests for core data structures and protocol encoding, plus integration tests for
client/server MOQT workflows over native QUIC and WebTransport.

## Build and run

Configure the test targets, build, and run them through CTest:

```bash
cmake -B build -DBUILD_TESTING=ON -DQUICR_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The repository Makefile provides the same common workflow:

```bash
make test
```

List discovered tests or run a subset by name:

```bash
ctest --test-dir build -N
ctest --test-dir build -R '<test-name-regex>' --output-on-failure
```

## Test suites

* `quicr_test` covers message encoding, containers, handlers, metrics, names, and transport support
  classes.
* `quicr_integration_test` exercises client/server sessions, streams, datagrams, fetches,
  namespaces, metrics, migration, and teardown over native QUIC and WebTransport.

Integration tests are configured only when the `openssl` command is available. The build generates
a one-day, self-signed P-256/SHA-256 certificate and private key in the integration-test build
directory. They are test credentials only: do not commit them, distribute them, or use them for
production. Production certificates must have their validity period, hostname/SAN, issuer,
signature algorithm, and public-key strength verified, and private keys must be stored securely.

## Sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer are available with Clang or GCC:

```bash
cmake -B build \
  -DBUILD_TESTING=ON \
  -DQUICR_BUILD_TESTS=ON \
  -DQUICR_ENABLE_SANITIZERS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Dependencies

Unit tests use [doctest](https://github.com/doctest/doctest). Integration tests additionally use
[spdlog](https://github.com/gabime/spdlog). Their pinned versions are declared in
[`package-lock.cmake`](../package-lock.cmake) and the CMake files beside the test targets.
