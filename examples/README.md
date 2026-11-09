# Examples

Building with examples requires `-DQUICR_BUILD_EXAMPLES=ON` to be added to the
cmake configuration.

## Server

For an example on a simple server, look at the
[integration tests](https://github.com/Quicr/libquicr/tree/main/test/integration_test).
For a more complicated example, [LAPS](https://github.com/Quicr/laps) is a more
fleshed out server.

## Client

Included in this repo are a few small interactive clients to demonstrate basic API usage:

- `simple_publish` connects to a relay, registers a track, and publishes each
  line entered on standard input as an object.
- `simple_subscribe` connects to a relay, subscribes to a track, and prints
  received objects until interrupted with `Ctrl+C`.

After configuring and building libquicr with examples enabled, run the clients
from the build directory:

```sh
./examples/simple_subscribe/simple_subscribe
./examples/simple_publish/simple_publish
```

Each client prompts for the relay URI, track namespace, and track name. Use the
same values in both clients to publish and receive messages.
