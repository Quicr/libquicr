// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/tick_service.h>

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

static void
TickService_MultiThreadRead(benchmark::State& state)
{
    const ThreadedTickService service(100);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    std::atomic<bool> start_flag{ false };
    std::atomic<uint64_t> total_reads{ 0 };

    // Create reader threads
    std::thread reader([&service, &start_flag, &total_reads]() {
        while (!start_flag.load()) {
            std::this_thread::yield();
        }

        int64_t local_reads = 0;
        while (start_flag.load()) {
            auto ticks = service.Microseconds();
            benchmark::DoNotOptimize(ticks);
            ++local_reads;
        }
        total_reads.fetch_add(local_reads);
    });

    for (auto _ : state) {
        start_flag = true;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        start_flag = false;
    }

    // Join all threads
    reader.join();

    state.SetItemsProcessed(total_reads.load());
}

// Single-threaded use.
BENCHMARK(TickService_Microseconds);
BENCHMARK(TickService_Milliseconds);

// Multi-threaded use.
BENCHMARK(TickService_MultiThreadRead);
