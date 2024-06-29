libquicr
========

[![Build](https://github.com/Quicr/libquicr/actions/workflows/cmake.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/cmake.yml)

An API library that implements publish/subscribe protocol [draft-ietf-moq-transport-04](https://datatracker.ietf.org/doc/html/draft-ietf-moq-transport-04). 
The API supports both client and server. Server is intended to be implemented as a relay.

### Build
Use `make` to build libquicr.

Use `make test` to run tests. 

Use `make cclean` to clean build files.


### Build without InfluxDB

To build without InfluxDB, run `cmake` with `-DLIBQUICR_WITHOUT_INFLUXDB=ON`

### Self-signed Certificate

Server requires a TLS certificate and key file. For development and testing, use a self-signed certificate. Below
are the steps to create a self-signed certificate and private ey. 

#### OpenSSL/BorningSSL

```
cd build/cmd/moq-example
openssl req -nodes -x509 -newkey rsa:2048 -days 365 \
    -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org" \
    -keyout server-key.pem -out server-cert.pem
```

#### MbedTLS

```
openssl req -nodes -x509 -newkey ec:<(openssl ecparam -name secp256r1) -days 365 \
    -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.m10x.org" \
    -keyout server-key.pem -out server-cert.pem
```


### MOQ Implementation Documentation

See [MOQ Implementation](docs/moq-implementation.md)
