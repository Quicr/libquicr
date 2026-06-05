// SPDX-FileCopyrightText: Copyright (c) 2026 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <benchmark/benchmark.h>

#include "quicr/detail/safe_time_queue.h"

static auto service = std::make_shared<timeq::threaded_tick_service>();

constexpr size_t kIterations = 100'000'000;

static void
BM_SafeTimeQueue_Construct(benchmark::State& state)
{
    for (auto _ : state) {
        auto tq = timeq::time_queue<int>(300, 1, service, kIterations);

        std::size_t size = tq.size();
        benchmark::DoNotOptimize(size);
        benchmark::ClobberMemory();
    }
}

static void
BM_SafeTimeQueue_Push(benchmark::State& state)
{
    quicr::SafeTimeQueue<int> tq(state.range(0), 1, service, kIterations);
    int64_t items_count = 0;

    for (auto _ : state) {
        ++items_count;

        {
            std::lock_guard __(tq);
            tq.Push(items_count, 20);
        }
    }

    state.SetItemsProcessed(items_count);
}

static void
BM_SafeTimeQueue_Pop(benchmark::State& state)
{
    quicr::SafeTimeQueue<int> tq(state.range(0), 1, service, kIterations);
    for (size_t i = 0; i < kIterations; ++i) {
        tq.Push(i, 10);
    }

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;

        {
            std::lock_guard __(tq);
            tq.Pop();
        }
    }

    state.SetItemsProcessed(items_count);
}

static void
BM_SafeTimeQueue_Front(benchmark::State& state)
{
    quicr::SafeTimeQueue<int> tq(state.range(0), 1, service, kIterations);
    for (size_t i = 0; i < kIterations; ++i) {
        tq.Push(i, 15);
    }

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;
        {
            std::lock_guard __(tq);
            auto value = tq.Front();
            benchmark::DoNotOptimize(value);
        }

        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(items_count);
}

static void
BM_SafeTimeQueue_PopFront(benchmark::State& state)
{
    quicr::SafeTimeQueue<int> tq(state.range(0), 1, service, kIterations);
    for (size_t i = 0; i < kIterations; ++i) {
        tq.Push(i, 15);
    }

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;
        {
            std::lock_guard __(tq);
            auto value = tq.PopFront();
            benchmark::DoNotOptimize(value);
        }

        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(items_count);
}

static void
BM_SafeTimeQueue_Size(benchmark::State& state)
{
    quicr::SafeTimeQueue<int> tq(300, 1, service, kIterations);
    for (size_t i = 0; i < kIterations; ++i) {
        tq.Push(i, 10);
    }

    for (auto _ : state) {
        {
            std::lock_guard __(tq);
            std::size_t size = tq.Size();
            benchmark::DoNotOptimize(size);
        }

        benchmark::ClobberMemory();
    }
}

static void
BM_SafeTimeQueue_Empty(benchmark::State& state)
{
    quicr::SafeTimeQueue<int> tq(300, 1, service, kIterations);
    for (size_t i = 0; i < kIterations; ++i) {
        tq.Push(i, 10);
    }

    for (auto _ : state) {

        {
            std::lock_guard __(tq);
            std::size_t size = tq.Empty();
            benchmark::DoNotOptimize(size);
        }

        benchmark::ClobberMemory();
    }
}

static void
BM_SafeTimeQueue_PushAndPopLoaded(benchmark::State& state)
{
    quicr::SafeTimeQueue<int> tq(state.range(0), 1, service, kIterations);
    int64_t items_count = 0;

    for (auto _ : state) {
        ++items_count;
        tq.Push(items_count, 1000);

        // Simulate load by not popping all items
        if (items_count % 100 == 0) {
            {
                std::lock_guard __(tq);

                auto elem = tq.Front();
                tq.Pop();
                benchmark::DoNotOptimize(elem);
            }

            benchmark::ClobberMemory();
        }
    }

    state.SetItemsProcessed(items_count);
}

static void
BM_SafeTimeQueue_PushAndPop_Interval_1ms(benchmark::State& state)
{
    quicr::SafeTimeQueue<int> tq(state.range(0), 1, service, kIterations);
    int64_t items_count = 0;

    for (auto _ : state) {
        ++items_count;

        {
            std::lock_guard __(tq);

            tq.Push(items_count, 1000);

            auto elem = tq.Front();
            tq.Pop();

            benchmark::DoNotOptimize(elem);
        }

        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(items_count);
}

static void
BM_SafeTimeQueue_PushAndPop_Interval_125ms(benchmark::State& state)
{
    quicr::SafeTimeQueue<int> tq(state.range(0), 125, service, kIterations);
    int64_t items_count = 0;

    for (auto _ : state) {
        ++items_count;

        {
            std::lock_guard __(tq);

            tq.Push(items_count, 1000);

            auto elem = tq.Front();
            tq.Pop();

            benchmark::DoNotOptimize(elem);
        }

        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(items_count);
}

BENCHMARK(BM_SafeTimeQueue_Construct)->Arg(300);
BENCHMARK(BM_SafeTimeQueue_Push)->Iterations(kIterations)->Arg(300)->Arg(1'000'000);
BENCHMARK(BM_SafeTimeQueue_Pop)->Iterations(kIterations)->Arg(300)->Arg(1'000'000);
BENCHMARK(BM_SafeTimeQueue_Front)->Iterations(kIterations)->Arg(300)->Arg(1'000'000);
BENCHMARK(BM_SafeTimeQueue_PopFront)->Iterations(kIterations)->Arg(300)->Arg(1'000'000);
BENCHMARK(BM_SafeTimeQueue_Size)->Iterations(kIterations);
BENCHMARK(BM_SafeTimeQueue_Empty)->Iterations(kIterations);
BENCHMARK(BM_SafeTimeQueue_PushAndPopLoaded)->Arg(5000)->Arg(1'000'000);
BENCHMARK(BM_SafeTimeQueue_PushAndPop_Interval_1ms)->Arg(5000)->Arg(1'000'000);
BENCHMARK(BM_SafeTimeQueue_PushAndPop_Interval_125ms)->Arg(5000)->Arg(1'000'000);
