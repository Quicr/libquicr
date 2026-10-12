
# Examples

Building with examples requires `-DQUICR_BUILD_EXAMPLES=ON` to be added to the
cmake configuration.

## Server

For an example on a simple server, look at the [integration tests](https://github.com/Quicr/libquicr/tree/main/test/integration_test). For a more complicated example, [LAPS](https://github.com/Quicr/laps) is a more fleshed out server.

## qClient

[examples/qclient](https://github.com/Quicr/libquicr/tree/main/examples/qclient) is an example client. It implements the client API to make a connection to the relay and can act as subscriber, publisher, or both.
