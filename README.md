libquicr
========

[![CMake](https://github.com/Quicr/libquicr/actions/workflows/cmake.yml/badge.svg?branch=main)](https://github.com/Quicr/libquicr/actions/workflows/cmake.yml)

Implementation of transport layer api based on QuicR.
Please see for further details
[QuicR Protocol](https://www.ietf.org/id/draft-jennings-moq-quicr-proto-01.html)

The actual protocol implementation is covered [here](https://github.com/Quicr/quicrq).

Quickstart
----------

### Cmake FetchContent

All the dependecies are now fetched via cmake and below steps should
build libquicr.a

```
brew install openssl
export PKG_CONFIG_PATH="/opt/homebrew/opt/openssl@3/lib/pkgconfig"
git clone git@github.com:Quicr/libquicr.git
cd libquicr
git submodule update --init --recursive
mkdir build
cd build
cmake ..
make all
```

### Build Docker Dev Image
If building docker image is the preferred option, please follow the below steps

The developer image contains ```picotls```, ```picoquic``` and ```quicrq```, which are required
for many other builds.  The container works by running it with a volume mount ```/ws/src```
to the source to be compiled. The above libraries are in the ```/ws/``` directory, which is expected for builds.

While ```quicrq``` is included under ```/ws/quicrq```, the container can be used to develop and compile
it by mounting ```quicrq``` directory to ```/ws/src```.

The docker container has been tested to work for both ARM (M1/Apple Silicon) and x86.

```
git clone git@github.com:Quicr/libquicr.git
cd libquicr

tar -c -C ../ ./quicrq ./libquicr  \
    | docker buildx build --progress=plain \
        --output type=docker \
        -f libquicr/build.Dockerfile -t quicr/libquicr:latest -
```

```
cd <libquicr directory>

rm -rf $(pwd)/build
docker run --rm \
    -v $(pwd):/ws/src \
    quicr/libquicr:latest make all
```

For now, it's best to run the binaries in interactive shell.  The images have been built for linux.  Run
the image in interactive mode to access the shell prompt.  From there you can access the binaries.
```quicrq``` and others are located under ```/ws/```.   For example, you can run ```/ws/quircq/quicrq_app``` binary.

```
docker run --rm -it \
    -v $(pwd):/ws/src \
    quicr/libquicr:latest bash
```

Running Examples
--------------

### Running Relays/Etc.

In order to test QUIC, the server/```really``` needs to have a certificate. The program expects
the

Generate self-signed certificate for really server to test with.

```
cd build/cmd/really
openssl req -nodes -x509 -newkey rsa:2048 -days 365 \
    -subj "/C=US/ST=CA/L=San Jose/O=Cisco/CN=test.quicr.ctgpoc.com" \
    -keyout server-key.pem -out server-cert.pem
```

Run:

```
cd build/cmd/really
./really

./reallyTest

