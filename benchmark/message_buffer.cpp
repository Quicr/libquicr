#include <benchmark/benchmark.h>

#include "moq/message_buffer.h"
#include <quicr/encode.h>

// The unused variables here are intentional, and we don't care about the
// quality of the randomness.
// NOLINTBEGIN(clang-analyzer-deadcode.DeadStores,cert-msc30-c,cert-msc50-cpp,concurrency-mt-unsafe)
static void
MessageBuffer_Construct(benchmark::State& state)
{
  std::vector<uint8_t> buffer(1280);
  std::generate(buffer.begin(), buffer.end(), std::rand);

  for (auto _ : state) {
    const auto _buffer = quicr::messages::MessageBuffer(buffer);
  }
}

static void
MessageBuffer_PushBack(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  for (auto _ : state) {
    buffer << uint8_t(std::rand() % 100);
  }
}

static void
MessageBuffer_PushBack16(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  for (auto _ : state) {
    buffer << uint16_t(std::rand() % 100);
  }
}

static void
MessageBuffer_PushBack32(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  for (auto _ : state) {
    buffer << uint32_t(std::rand() % 100);
  }
}

static void
MessageBuffer_PushBack64(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  for (auto _ : state) {
    buffer << uint64_t(std::rand() % 100);
  }
}

static void
MessageBuffer_PushBackName(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  constexpr quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    buffer << name;
  }
}

static void
MessageBuffer_PushBackNamespace(benchmark::State& state)
{
  quicr::messages::MessageBuffer buffer;
  constexpr quicr::Namespace ns(0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name, 120);
  for (auto _ : state) {
    buffer << ns;
  }
}

static void
MessageBuffer_PushBackVector_Copy(benchmark::State& state)
{
  std::vector<uint8_t> buf(1280);
  std::generate(buf.begin(), buf.end(), std::rand);

  quicr::messages::MessageBuffer buffer;
  for (auto _ : state) {
    buffer.push(buf);
  }
}

static void
MessageBuffer_PushBackVector_Reserved(benchmark::State& state)
{
  std::vector<uint8_t> buf(1280);
  std::generate(buf.begin(), buf.end(), std::rand);

  quicr::messages::MessageBuffer buffer(100000 * buf.size());
  for (auto _ : state) {
    buffer.push(buf);
  }
}
// NOLINTEND(clang-analyzer-deadcode.DeadStores,cert-msc30-c,cert-msc50-cpp,concurrency-mt-unsafe)

// This lint is hitting on something internal to the benchmarking system, which
// we are not responsible for.
// NOLINTBEGIN(cppcoreguidelines-owning-memory)
BENCHMARK(MessageBuffer_Construct);
BENCHMARK(MessageBuffer_PushBack);
BENCHMARK(MessageBuffer_PushBack16);
BENCHMARK(MessageBuffer_PushBack32);
BENCHMARK(MessageBuffer_PushBack64);
BENCHMARK(MessageBuffer_PushBackName);
BENCHMARK(MessageBuffer_PushBackNamespace);
BENCHMARK(MessageBuffer_PushBackVector_Copy);
BENCHMARK(MessageBuffer_PushBackVector_Reserved);
// NOLINTEND(cppcoreguidelines-owning-memory)
