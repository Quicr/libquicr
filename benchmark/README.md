# Benchmarks

libquicr's benchmarks measure individual data structures and protocol-processing components. They
are intended for development and regression analysis, not as end-to-end network throughput or
latency claims.

## Build and run

Configure and build the benchmark target:

```bash
cmake -B build -DBUILD_TESTING=ON -DQUICR_BUILD_BENCHMARKS=ON
cmake --build build --target quicr_benchmark
```

Run it from a single-configuration build:

```bash
./build/benchmark/quicr_benchmark
```

Google Benchmark options can list or filter cases:

```bash
./build/benchmark/quicr_benchmark --benchmark_list_tests=true
./build/benchmark/quicr_benchmark --benchmark_filter='<benchmark-name-regex>'
```

Multi-configuration generators may place the executable in a configuration-specific subdirectory,
such as `build/benchmark/Release/`.

## Coverage

The suite contains component benchmarks for:

* data storage;
* extension serialization and deserialization;
* track-name hashing;
* priority queues;
* stream buffers;
* variable-length integer encoding.

Compare results using the same machine, compiler, build type, and benchmark arguments. Network,
relay, and application performance require separate end-to-end measurements.

## Dependencies

Benchmarks use [Google Benchmark](https://github.com/google/benchmark). Its pinned version is
declared in [`package-lock.cmake`](../package-lock.cmake) and
[`benchmark/CMakeLists.txt`](CMakeLists.txt).
