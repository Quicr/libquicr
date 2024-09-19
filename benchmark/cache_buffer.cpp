// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/cache_buffer.h>
#include <quicr/detail/uintvar.h>

#include <benchmark/benchmark.h>

static void
CacheBuffer_Construct(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto buffer = CacheBuffer();
        benchmark::DoNotOptimize(buffer);
        benchmark::ClobberMemory();
    }
}

static void
CacheBuffer_Push(benchmark::State& state)
{
    CacheBuffer buffer;
    uint64_t value = 0;
    Span<const uint8_t> bytes{ reinterpret_cast<uint8_t*>(&value), sizeof(uint64_t) };

    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(bytes);
    }
}

BENCHMARK(CacheBuffer_Construct);
BENCHMARK(CacheBuffer_Push);
