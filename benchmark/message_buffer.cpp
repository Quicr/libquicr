#include <benchmark/benchmark.h>

#include <quicr/message_buffer.h>

void BM_MessageBufferConstruct(benchmark::State& state)
{
    std::vector<uint8_t> buffer(1280);
    std::generate(buffer.begin(), buffer.end(), std::rand);

    for (auto _ : state)
    {
        quicr::messages::MessageBuffer __(buffer);
    }
}

void BM_MessageBufferPushBack(benchmark::State& state)
{
    quicr::messages::MessageBuffer buffer;
    for (auto _ : state)
    {
        buffer.push_back(uint8_t(std::rand() % 100));
    }
}

void BM_MessageBufferPushBackVector(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280);
    std::generate(buf.begin(), buf.end(), std::rand);

    quicr::messages::MessageBuffer buffer;
    for (auto _ : state)
    {
        buffer.push_back(buf);
    }
}

BENCHMARK(BM_MessageBufferConstruct);
BENCHMARK(BM_MessageBufferPushBack);
BENCHMARK(BM_MessageBufferPushBackVector);
