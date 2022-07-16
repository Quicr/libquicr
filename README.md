libquicr
========

Implementation of transport layer api based on QuicR

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

### Docker Dev Image
The developer image contains ```picotls```, ```picoquic``` and ```quicrq```, which are required
for many other builds.  The container works by running it with a volume mount ```/ws/src```
to the source to be compiled. The above libraries are in the ```/ws/``` directory, which is expected for builds.

While ```quicrq``` is included under ```/ws/quicrq```, the container can be used to develop and compile
it by mounting ```quicrq``` directory to ```/ws/src```. 

#### Build Docker image
The docker container has been tested to work for both ARM (M1/Apple Silicon) and x86.  

```
git clone git@github.com:Quicr/libquicr.git
cd libquicr

tar -c -C ../ ./quicrq ./libquicr  \
    | docker buildx build --progress=plain \
        --output type=docker \
        -f libquicr/build.Dockerfile -t quicr/libquicr:latest -
```

#### Compile libquicr


```
cd <libquicr directory>

rm -rf $(pwd)/build
docker run --rm \
    -v $(pwd):/ws/src \
    quicr/libquicr:latest make all 
```

#### Run binaries

For now, it's best to run the binaries in interactive shell.  The images have been built for linux.  Run
the image in interactive mode to access the shell prompt.  From there you can access the binaries. 
```quicrq``` and others are located under ```/ws/```.   For example, you can run ```/ws/quircq/quicrq_app``` binary. 

```
docker run --rm -it \
    -v $(pwd):/ws/src \
    quicr/libquicr:latest bash
```

### Non-Docker Build

Using the convenient Makefile that wraps CMake:

```
> make all
```
This should build library `libquicr.a` and 
a command line program called `forty` under build/cmd.

`make cclean` will cleanup cmake build and can be use to 
build with a clean slate

`make format` will format the code based on 
clang-format.

If openssl isn't installed, `brew install openssl1.1` 
should fix it.
If there are still openssl errors, please ensure 
`OPENSSL_ROOT_DIR` is setup via

```
export PKG_CONFIG_PATH="/usr/local/opt/openssl@1.1/lib/pkgconfig"
```

---
## Run-time

### Running Relays/Etc.

> **NOTE:** The docker dev image has ```quicrq``` and other dependencies under ```/ws/```  

The `forty` test program can be run as below

```
Usage
> ./build/cmd/forty 
Usage: forty <server> <port> <mode> <name-for-self> <peer-name>
server: server ip for quicr origin/relay
port: server port for quicr origin/relay
mode: sendrecv/send/recv
name-for-self: some string
peer-name: some string that is not the same as self
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

Please reach out to Suhas for IP Address of the QuicR origin 
server deployed in AWS K8s Cluster.
The deployment details for the same can be found 
at https://sqbu-github.cisco.com/cto-media/quicr-deploy/


Local QuicR origin/relay setup
----------------------------------
For dev-testing, it would be beneficial to build and run 
the origin/relay locally.  Use the dev docker image for this. 

- Run Docker Image
    ```
    docker run --rm -it \
    -v $(pwd):/ws/src \
    quicr/libquicr:latest bash
    ```

- Run Origin as
    ```
    export QUICRQ=/ws/quicrq
    $QUICRQ/quicrq_app -q qlog_server -p 7777 -c ../picoquic/certs/cert.pem -k ../picoquic/certs/key.pem -a quicr-h00 server
    ```
- Run Relay as
    ```
    export QUICRQ=/ws/quicrq
    $QUICRQ/quicrq_app -q qlog_relay -p 8888 -c ../picoquic/certs/cert.pem -k ../picoquic/certs/key.pem -a quicr-h00 relay localhost d 7777
    ```

Above example, show Origin server running at 127.0.0.1:7777 and relay at 127.0.0.1:8888 and 
connecting to the Origin server.

Some Limitations in the prototype
---------------------------------

- Names cannot be reused after the publisher connection is closed 
[ This will be addressed in the coming changes to the stack]
- If there is inactivity for more than 30s, underlying QUIC connection 
 is terminated. This is due to the fact that the prototype was built
 with realtime media flow in mind. One can hack out of by sending
 1-byte message periodically
