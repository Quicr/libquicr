#include <quicr/priority_queue.h>

#include <benchmark/benchmark.h>
#include <quicr/quic_transport.h>

static auto tick_service = std::make_shared<quicr::ThreadedTickService>();

constexpr size_t kIterations = 1'000'000;
constexpr size_t kNumSubscribers = 10;

template<typename T, std::enable_if_t<std::is_integral<T>::value, bool> = true>
std::span<const uint8_t>
BytesOf(const T& value)
{
    return std::span(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

template<typename T, std::enable_if_t<std::is_integral<T>::value, bool> = true>
T
ValueOf(std::span<uint8_t const> value, bool host_order = true)
{
    T rval{ 0 };
    auto rval_ptr = reinterpret_cast<uint8_t*>(&rval);

    if (!host_order) {
        for (size_t i = 0; i < sizeof(T); i++) {
            rval_ptr[i] = value[i];
        }
    } else {
        constexpr auto last = sizeof(T) - 1;
        for (size_t i = 0; i < sizeof(T); i++) {
            rval_ptr[i] = value[last - i];
        }
    }

    return rval;
}

static void
Encode(benchmark::State& state)
{
    int64_t items_count = 0;
    uint64_t value = 1234;

    for (auto _ : state) {
        auto bytes = BytesOf(value);
        benchmark::DoNotOptimize(bytes);
        benchmark::ClobberMemory();

        ++items_count;
    }

    state.SetItemsProcessed(items_count);
}

static void
Decode(benchmark::State& state)
{

    int64_t items_count = 0;

    std::vector<uint8_t> data = { 0x1, 0x2, 0x3, 0x4 };

    for (auto _ : state) {
        uint32_t bytes = ValueOf<uint32_t>({ data.begin(), data.end() });
        benchmark::DoNotOptimize(bytes);
        benchmark::ClobberMemory();

        ++items_count;
    }

    state.SetItemsProcessed(items_count);
}

static void
PQ_Push(benchmark::State& state)
{
    quicr::PriorityQueue<std::vector<uint8_t>, 3> pq(30000, 300, tick_service, kIterations);
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
    quicr::PriorityQueue<std::vector<uint8_t>, 3> pq(30000, 1, tick_service, kIterations);
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
    quicr::PriorityQueue<std::vector<uint8_t>, 3> pq(30000, 1, tick_service, kIterations);
    std::vector<uint8_t> data(1000, 0);

    for (size_t i = 0; i < kIterations; ++i) {
        pq.Push(i % 1000, data, 5000);
    }

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;
        quicr::TimeQueueElement<std::vector<uint8_t>> elem;
        pq.PopFront(elem);
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
        queues.emplace_back(new quicr::PriorityQueue<quicr::ConnData>(5000, 1, tick_service, 150));
    }

    quicr::ConnData cd;

    auto data = std::make_shared<std::vector<uint8_t>>(1000, 0);

    cd.conn_id = 1;
    cd.data_ctx_id = 0xffaabbcc;
    cd.priority = 128;
    cd.tick_microseconds = 0;
    cd.data = data;

    int64_t items_count = 0;
    for (auto _ : state) {
        ++items_count;
        cd.tick_microseconds++;
        for (auto& pq : queues) {
            pq->Push(static_cast<int>(items_count / 150), cd, 2000);
            quicr::TimeQueueElement<quicr::ConnData> elem;
            pq->PopFront(elem);

            if (pq->Size() > 4 and elem.has_value) {
                break;
            }
        }
    }

    benchmark::ClobberMemory();

    state.SetItemsProcessed(items_count);
}

BENCHMARK(Encode);
BENCHMARK(Decode);
BENCHMARK(PQ_Push)->Iterations(kIterations)->Threads(1);
BENCHMARK(PQ_Pop)->Iterations(kIterations)->Threads(1);
BENCHMARK(PQ_PopFront)->Iterations(kIterations)->Threads(1);
BENCHMARK(PQ_ConnDataForwarding)->Threads(1);
