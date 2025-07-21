// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/data_storage.h>
#include <quicr/detail/uintvar.h>

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

static void
DataStorage_CopyIterator(benchmark::State& state)
{
    auto buffer = quicr::DataStorage<>::Create();
    std::vector<uint8_t> bytes(1000, 0xFF);
    for (int i = 0; i < 1000; ++i) {
        buffer->Push(bytes);
    }

    uint8_t copied[1000];
    for ([[maybe_unused]] const auto& _ : state) {
        std::copy_n(std::next(buffer->begin(), 999000), 1000, copied);
    }
}

BENCHMARK(DataStorage_Construct);
BENCHMARK(DataStorage_Push);
BENCHMARK(DataStorage_CopyIterator);
