libquicr
========

[![Build](https://github.com/Quicr/libquicr/actions/workflows/cmake.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/cmake.yml)

An API library that implements publish/subscribe protocol [draft-ietf-moq-transport-04](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-04).
The API supports both client and server. Server is intended to be implemented as a relay.

## Build

### CMake

Generate the cmake build directory:

```bash
cmake -B build
```

Available options to append to the above command for building tests, benchmarks, and with linting:

```
-DBUILD_TESTS=ON
-DBUILD_BENCHMARKS=ON
-DLINT=ON
```

To build the project, run:
```bash
cmake --build build -j
```

### Make

Use `make` to build libquicr.

Use `make test` to run tests.

Use `make cclean` to clean build files.

## Self-signed Certificate

Server requires a TLS certificate and key file. For development and testing, use a self-signed certificate. Below
are the steps to create a self-signed certificate and private ey.

### OpenSSL/BorningSSL

```
cd build/cmd/moq-example
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

```
    openssl ecparam -name prime256v1 -genkey -noout -out server-key-ec.pem
    openssl req -nodes -x509 -key server-key-ec.pem -days 365 \
        -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org" \
        -keyout server-key.pem -out server-cert.pem

```


## Generate documentation
API documentation can be generated using `make doc`

Below programs need to be installed.

Example on MacOS:

* `brew install doxygen`
* `brew install npm`
* `brew install pandoc`
* `npm install --global mermaid-filter`

> [!NOTE]
> https://github.com/raghur/mermaid-filter adds mermaid support to pandoc
>

### MOQ Implementation Documentation

See [MOQ Implementation](docs/moq-implementation.md)
