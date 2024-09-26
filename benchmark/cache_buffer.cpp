// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/cache_buffer.h>
#include <quicr/detail/uintvar.h>

#include <benchmark/benchmark.h>

static void
CacheBuffer_Construct(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto buffer = quicr::CacheBuffer();
        benchmark::DoNotOptimize(buffer);
        benchmark::ClobberMemory();
    }
}

static void
CacheBuffer_Push(benchmark::State& state)
{
    quicr::CacheBuffer buffer;
    uint64_t value = 0;
    auto bytes = quicr::AsBytes(value);

    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(bytes);
    }
}

BENCHMARK(CacheBuffer_Construct);
BENCHMARK(CacheBuffer_Push);
