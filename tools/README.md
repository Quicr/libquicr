# Tools

Building with tools requires `-DQUICR_BUILD_TOOLS=ON` to be added to the
cmake configuration.

## moq-probe

`moq-probe` is an interactive MOQT client for testing and debugging publishing,
subscribing, fetching, and protocol scenarios. See
[`moq-probe/README.md`](moq-probe/README.md) for usage.

## moq-capture

`moq-capture` records track objects and replays previously recorded data. See
[`moq-capture/README.md`](moq-capture/README.md) for usage.

## Dependencies

Building the tools fetches the following tool-only dependencies:

| Dependency | Purpose | Declared in |
| --- | --- | --- |
| [cxxopts](https://github.com/jarro2783/cxxopts) | Command-line parsing | [`tools/CMakeLists.txt`](CMakeLists.txt) |
| [spdlog](https://github.com/gabime/spdlog) | Logging | [`tools/CMakeLists.txt`](CMakeLists.txt) |
