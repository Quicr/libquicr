libquicr
========

Implementation of transport layer api based on QuicR.
Please see for further details
[QuicR Protocol](https://www.ietf.org/id/draft-jennings-moq-quicr-proto-01.html)

Quickstart
----------
### Cmake FetchContent

All the dependecies are now fetched via cmake and below steps should 
build libquicr.a

```
git clone git@github.com:Quicr/libquicr.git
cd libquicr
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

The `forty` test program can be run as below

```
Usage
> ./build/cmd/forty 
Usage: forty <server> <port> <mode> <name-for-self> <peer-name> <mask>
server: server ip for quicr origin/relay
port: server port for quicr origin/relay
mode: sendrecv/send/recv
name-for-self: some string
peer-name: some string that is not the same as self
mask: default to zero for full name match
```

Pub/Sub with forty
-------------------
### Test against cloud relay/origin server

```
Subcriber
build/cmd/forty <server-ip> 7777 recv alice bob
```

```
Publisher
build/cmd/forty <server-ip> 7777 send bob alice
```

Pub/Sub - 2 Way message exchange
```
Client 1
build/cmd/forty <aws-server-ip> 7777 sendrecv client1 client2

Client 2
build/cmd/forty <aws-server-ip> 7777 sendrecv client2 client1

```



Local QuicR origin/relay setup
----------------------------------
For dev-testing, it would be beneficial to build and run 
the origin/relay locally.  Use the dev docker image for this. 

- Run Origin as
    ```
    export QUICRQ=/ws/quicrq
    or
    export QUICRQ=./build/_deps/quicrq-build ( non docker )
    
    $QUICRQ/quicrq_app -q qlog_server -p 7777 -c ../picoquic/certs/cert.pem -k ../picoquic/certs/key.pem  server
    ```
- Run Relay as
    ```
    export QUICRQ=/ws/quicrq
    or 
    export QUICRQ=./build/_deps/quicrq-build ( non docker )
    $QUICRQ/quicrq_app -q qlog_relay -p 8888 -c ../picoquic/certs/cert.pem -k ../picoquic/certs/key.pem relay localhost d 7777
    ```

Above example, show Origin server running at 127.0.0.1:7777 and relay at 127.0.0.1:8888 and 
connecting to the Origin server.
