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

If openssl isn't installed, `brew install openssl1.1` 
should fix it.
If there are still openssl errors, please ensure 
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
./forty <aws-server-ip> 7777 sendrecv client1 client2

Client 2
./forty <aws-server-ip> 7777 sendrecv client2 client1

```

Please reach out to Suhas for IP Address of the QuicR origin 
server deployed in AWS K8s Cluster.
The deployment details for the same can be found 
at https://sqbu-github.cisco.com/cto-media/quicr-deploy/


Local QuicR origin/relay setup
----------------------------------
 For dev-testing, it would be beneficial to build and run 
the origin/relay locally

- Build [quicrq](https://github.com/Quicr/quicrq) 
  and its dependencies (picotols/picoquic)
- Run Origin as
```
./quicrq_app -q qlog_server -p 7777 -c ../picoquic/certs/cert.pem -k ../picoquic/certs/key.pem -a quicr-h00 server
```
- Run Relay as
```
/quicrq_app -q qlog_relay -p 8888 -c ../picoquic/certs/cert.pem -k ../picoquic/certs/key.pem -a quicr-h00 relay localhost d 7777
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