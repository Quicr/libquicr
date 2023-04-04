#include <benchmark/benchmark.h>

#include <quicr/quicr_name.h>

static void
BM_NameConstructFromHex(benchmark::State& state)
{
  for (auto _ : state) {
    quicr::Name("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  }
}

static void
BM_NameCopyConstruct(benchmark::State& state)
{
  quicr::Name name("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(name);
  }
}

static void
BM_NameLeftShift(benchmark::State& state)
{
  quicr::Name name("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  for (auto _ : state) {
    name <<= 1;
  }
}

static void
BM_NameRightShift(benchmark::State& state)
{
  quicr::Name name("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  for (auto _ : state) {
    name >>= 1;
  }
}

static void
BM_NameAdd(benchmark::State& state)
{
  quicr::Name name("0x0");
  for (auto _ : state) {
    ++name;
  }
}

static void
BM_NameSub(benchmark::State& state)
{
  quicr::Name name("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  for (auto _ : state) {
    --name;
  }
}

static void
BM_NameToString(benchmark::State& state)
{
  quicr::Name name("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  for (auto _ : state) {
    name.to_hex();
  }
}

BENCHMARK(BM_NameConstructFromHex);
BENCHMARK(BM_NameCopyConstruct);
BENCHMARK(BM_NameLeftShift);
BENCHMARK(BM_NameRightShift);
BENCHMARK(BM_NameAdd);
BENCHMARK(BM_NameSub);
BENCHMARK(BM_NameToString);
