libquicr
========

[![Build](https://github.com/Quicr/libquicr/actions/workflows/cmake.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/cmake.yml)

An API library that implements publish/subscribe protocol [draft-ietf-moq-transport-04](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-04).
The API supports both client and server. Server is intended to be implemented as a relay.

API documentation can be found under https://quicr.github.io/libquicr

## Build

### Dependencies

#### Linux

```
sudo apt-get update && sudo apt-get install -y gcc g++ golang pkgconf cmake make 
```

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

#### (5) Install GoLang
Golang is required for BoringSSL build.  Install via https://go.dev/doc/install instructions. 

---

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

### Create cert under cmd/examples

```
make cert
```

### OpenSSL/BorningSSL

```
cd build/cmd/examples
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

## Example
Please see cmd/examples for a sample client and server implementation showing chat and clock
applications

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

See [MOQ Implementation](docs/implementation)
