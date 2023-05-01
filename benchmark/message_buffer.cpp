#include <benchmark/benchmark.h>

#include <quicr/encode.h>
#include <quicr/message_buffer.h>

void
MessageBuffer_Construct(benchmark::State& state)
{
  std::vector<uint8_t> buffer(1280);
  std::generate(buffer.begin(), buffer.end(), std::rand);

  for (auto _ : state) {
    quicr::messages::MessageBuffer __(buffer);
  }
}

void
MessageBuffer_PushBack(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  for (auto _ : state) {
    buffer << uint8_t(std::rand() % 100);
  }
}

void
MessageBuffer_PushBack16(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  for (auto _ : state) {
    buffer << uint16_t(std::rand() % 100);
  }
}

void
MessageBuffer_PushBack32(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  for (auto _ : state) {
    buffer << uint32_t(std::rand() % 100);
  }
}

void
MessageBuffer_PushBack64(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  for (auto _ : state) {
    buffer << uint64_t(std::rand() % 100);
  }
}

void
MessageBuffer_PushBackVector(benchmark::State& state)
{
  std::vector<uint8_t> buf(1280);
  std::generate(buf.begin(), buf.end(), std::rand);

  quicr::messages::MessageBuffer buffer(1280 * 1000);
  for (auto _ : state) {
    buffer.push(buf);
  }
}

void
MessageBuffer_WriteName(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    buffer << name;
  }
}

BENCHMARK(MessageBuffer_Construct);
BENCHMARK(MessageBuffer_PushBack);
BENCHMARK(MessageBuffer_PushBack16);
BENCHMARK(MessageBuffer_PushBack32);
BENCHMARK(MessageBuffer_PushBack64);
BENCHMARK(MessageBuffer_PushBackVector);
BENCHMARK(MessageBuffer_WriteName);
