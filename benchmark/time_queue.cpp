#include <quicr/detail/time_queue.h>

#include <benchmark/benchmark.h>

static auto service = std::make_shared<quicr::ThreadedTickService>();

constexpr size_t kIterations = 1'000'000;

static void
TimeQueue_Push(benchmark::State& state)
{
    quicr::TimeQueue<int, std::chrono::milliseconds> tq(300, 1, service, kIterations);
    int64_t items_count = 0;

    for (auto _ : state) {
        ++items_count;
        tq.Push(items_count, 20);
    }

    state.SetItemsProcessed(items_count);
}

static void
TimeQueue_Pop(benchmark::State& state)
{
    quicr::TimeQueue<int, std::chrono::milliseconds> tq(300, 1, service, kIterations);
    for (size_t i = 0; i < kIterations; ++i) {
        tq.Push(i, 10);
    }

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;
        tq.Pop();
    }

    state.SetItemsProcessed(items_count);
}

static void
TimeQueue_PopFront(benchmark::State& state)
{
    quicr::TimeQueue<int, std::chrono::milliseconds> tq(300, 1, service, kIterations);
    for (size_t i = 0; i < kIterations; ++i) {
        tq.Push(i, 15);
    }

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;
        auto value = tq.PopFront();
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(items_count);
}

BENCHMARK(TimeQueue_Push)->Iterations(kIterations);
BENCHMARK(TimeQueue_Pop)->Iterations(kIterations);
BENCHMARK(TimeQueue_PopFront)->Iterations(kIterations);