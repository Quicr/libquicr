# Examples

Building with examples requires `-DQUICR_BUILD_EXAMPLES=ON` to be added to the
cmake configuration.

## Server

For an example on a simple server, look at the
[integration tests](https://github.com/Quicr/libquicr/tree/main/test/integration_test).
For a more complicated example, [LAPS](https://github.com/Quicr/laps) is a more
fleshed out server.

## Client

Included in this repo are four small interactive clients demonstrating the C++ and native C APIs:

- `simple_publish` is the C++ publisher. It links `quicr::quicr`, connects to a
  relay, registers a track, and publishes each line entered on standard input.
- `simple_subscribe` is the C++ subscriber. It links `quicr::quicr`, connects
  to a relay, subscribes to a track, and prints received objects.
- `simple_c_publish` provides the same publishing flow through the native C API
  and links `quicr::c`.
- `simple_c_subscribe` provides the same subscribing flow through the native C
  API and links `quicr::c`.

After configuring and building libquicr with examples enabled, run the clients
from the build directory:

```sh
./examples/simple_publish/simple_publish
./examples/simple_publish/simple_c_publish
./examples/simple_subscribe/simple_subscribe
./examples/simple_subscribe/simple_c_subscribe
```

Each client prompts for the relay URI, track namespace, and track name. Use
matching values in a publisher and subscriber to send and receive messages;
interrupt a subscriber with `Ctrl+C`.
