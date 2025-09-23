// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/tick_service.h>

#include <benchmark/benchmark.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace quicr;

static void
TickService_Microseconds(benchmark::State& state)
{
    const ThreadedTickService service(100);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for ([[maybe_unused]] const auto& _ : state) {
        auto ticks = service.Microseconds();
        benchmark::DoNotOptimize(ticks);
    }
}

static void
TickService_Milliseconds(benchmark::State& state)
{
    const ThreadedTickService service(100);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for ([[maybe_unused]] const auto& _ : state) {
        auto ticks = service.Milliseconds();
        benchmark::DoNotOptimize(ticks);
    }
}

// Single-threaded use.
BENCHMARK(TickService_Microseconds);
BENCHMARK(TickService_Milliseconds);
