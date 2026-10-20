# Tools

Building with tools requires `-DQUICR_BUILD_TOOLS=ON` to be added to the
cmake configuration.

## qclient

`qclient` is an interactive MOQT client for testing and debugging publishing,
subscribing, fetching, and protocol scenarios. See
[`qclient/README.md`](qclient/README.md) for usage.

Building `qclient` fetches the following tool-only dependencies:

- [cxxopts](https://github.com/jarro2783/cxxopts) for command-line parsing
- [nlohmann/json](https://github.com/nlohmann/json) for recording metadata
- [spdlog](https://github.com/gabime/spdlog) for logging
