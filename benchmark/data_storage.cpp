// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/data_storage.h>
#include <quicr/uintvar.h>

#include <benchmark/benchmark.h>

static void
DataStorage_Construct(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto buffer = quicr::DataStorage<>::Create();
        benchmark::DoNotOptimize(buffer);
        benchmark::ClobberMemory();
    }
}

static void
DataStorage_Push(benchmark::State& state)
{
    auto buffer = quicr::DataStorage<>::Create();
    uint64_t value = 0;
    auto bytes = quicr::AsBytes(value);

    for ([[maybe_unused]] const auto& _ : state) {
        buffer->Push(bytes);
    }
}

BENCHMARK(DataStorage_Construct);
BENCHMARK(DataStorage_Push);
