#include "quicr/containers/priority_queue.h"
#include "quicr/transport.h"

#include <timeq/tick_service.h>

#include <benchmark/benchmark.h>

static auto tick_service = std::make_shared<timeq::threaded_tick_service>();

constexpr size_t kIterations = 1'000'000;
constexpr size_t kNumSubscribers = 10;

static void
PQ_Push(benchmark::State& state)
{
    quicr::PriorityQueue<std::vector<uint8_t>> pq(30000, 300, tick_service, kIterations);
    int64_t items_count = 0;

    std::vector<uint8_t> data(1, 0);

    for (auto _ : state) {
        ++items_count;
        pq.Push(items_count % 500, data, 5000);
    }

    state.SetItemsProcessed(items_count);
}

static void
PQ_Pop(benchmark::State& state)
{
    quicr::PriorityQueue<std::vector<uint8_t>> pq(30000, 1, tick_service, kIterations);
    std::vector<uint8_t> data(1000, 0);

    for (size_t i = 0; i < kIterations; ++i) {
        pq.Push(i % 500, data, 5000);
    }

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;
        pq.Pop();
    }

    state.SetItemsProcessed(items_count);
}

static void
PQ_PopFront(benchmark::State& state)
{
    quicr::PriorityQueue<std::vector<uint8_t>> pq(30000, 1, tick_service, kIterations);
    std::vector<uint8_t> data(1000, 0);

    for (size_t i = 0; i < kIterations; ++i) {
        pq.Push(i % 1000, data, 5000);
    }

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;
        auto elem = pq.PopFront();
        benchmark::DoNotOptimize(elem);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(items_count);
}

static void
PQ_ConnDataForwarding(benchmark::State& state)
{
    std::vector<std::shared_ptr<quicr::PriorityQueue<quicr::ConnData>>> queues;
    for (size_t i = 0; i < kNumSubscribers; i++) {
        auto ptr = std::make_shared<quicr::PriorityQueue<quicr::ConnData>>(5000, 1, tick_service, 150);
        queues.emplace_back(std::move(ptr));
    }

    quicr::ConnData cd;

    auto data = std::make_shared<std::vector<uint8_t>>(1000, 0);

    cd.conn_id = 1;
    cd.priority = 128;
    cd.tick_microseconds = 0;
    cd.data = data;

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;
        cd.tick_microseconds++;
        for (auto& pq : queues) {
            pq->Push(static_cast<int>(items_count / 150), cd, 2000);
            auto elem = pq->PopFront();

            if (pq->Size() > 4 && elem.value.has_value()) {
                break;
            }
        }
    }

    benchmark::ClobberMemory();

    state.SetItemsProcessed(items_count);
}

BENCHMARK(PQ_Push)->Iterations(kIterations);
BENCHMARK(PQ_Pop)->Iterations(kIterations);
BENCHMARK(PQ_PopFront)->Iterations(kIterations);
BENCHMARK(PQ_ConnDataForwarding);
