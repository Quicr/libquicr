# Build libquicr
#
# BUILD:
#   DOCKER_BUILDKIT=1
#   tar -c -C ../ ./quicrq ./libquicr  \
#           | docker buildx build --progress=plain \
#               --output type=docker \
#               -f libquicr/build.Dockerfile -t quicr/libquicr:latest -
#
# RUN and build code
#   docker run --rm \
#       -v $(pwd):/ws/src -v $(pwd)/../:/ws/libs \
#       quicr/libquicr:latest
#
FROM alpine:latest

VOLUME /ws/src
VOLUME /ws/libs

WORKDIR /ws/src

COPY libquicr/ /ws/src
COPY quicrq /ws/quicrq


RUN apk update \
    && apk add --no-cache cmake alpine-sdk openssl-dev doctest-dev bash \
    && apk add ca-certificates clang lld curl

# Build PicoTLS, PicoQuic, and quicrq
RUN cd /ws \
    && echo "Building PicoTLS" \
    && git clone https://github.com/h2o/picotls.git \
    && cd picotls \
    && git submodule init \
    && git submodule update \
    && cmake . \
    && make

RUN cd /ws \
    && echo "Building PicoQuic" \
    && git clone https://github.com/private-octopus/picoquic.git \
    && cd picoquic \
    && cmake . \
    && make

RUN echo "Building Quicrq" \
    && cd /ws/quicrq \
    && rm -rf CMakeCache.txt CMakeFiles build \
    && cmake . \
    && make

