libquicr
========

Implementation of transport layer api based on QuicR

Quickstart
----------

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

If there are openssl errors, please ensure 
`OPENSSL_ROOT_DIR` is setup via

```
export PKG_CONFIG_PATH="/usr/local/opt/openssl@1.1/lib/pkgconfig"
```

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
./forty <server-ip> 7777 recv alice bob
```

```
Publisher
./forty <server-ip> 7777 send bob alice
```

Pub/Sub - 2 Way message exchange
```
Client 1
./forty <server-ip> 7777 sendrecv client1 client2

Client 2
./forty <server-ip> 7777 sendrecv client2 client1

```

Please reach out to Suhas for IP Address of the server

### Test against local origin
 Details wil be provided soon

### Test against local relay
Details will be provided soon

Some Limitations in the prototype
---------------------------------

- Names cannot be reused after the publisher connection is closed 
[ This will be addressed in the coming changes to the stack]