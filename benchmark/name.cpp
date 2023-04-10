#include <benchmark/benchmark.h>

#include <quicr/quicr_name.h>
#include <vector>

static void
BM_NameConstructFromHex(benchmark::State& state)
{
  std::string str = "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(str);
  }
}

static void
BM_NameConstructFromVector(benchmark::State& state)
{
  std::vector<uint8_t> data = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(data);
  }
}

static void
BM_NameConstructFromBytePointer(benchmark::State& state)
{
  std::vector<uint8_t> vec_data = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0xFF, 0xFF };

  uint8_t* data = vec_data.data();
  size_t length = vec_data.size();

  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(data, length);
  }
}

static void
BM_NameCopyConstruct(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(name);
  }
}

static void
BM_NameLeftShift(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    name << 64;
  }
}

static void
BM_NameRightShift(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    name >> 64;
  }
}

static void
BM_NameAdd(benchmark::State& state)
{
  quicr::Name name = 0x0_name;
  for (auto _ : state) {
    ++name;
  }
}

static void
BM_NameSub(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    --name;
  }
}

static void
BM_NameToHex(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    name.to_hex();
  }
}

BENCHMARK(BM_NameConstructFromHex);
BENCHMARK(BM_NameConstructFromVector);
BENCHMARK(BM_NameConstructFromBytePointer);
BENCHMARK(BM_NameCopyConstruct);
BENCHMARK(BM_NameLeftShift);
BENCHMARK(BM_NameRightShift);
BENCHMARK(BM_NameAdd);
BENCHMARK(BM_NameSub);
BENCHMARK(BM_NameToHex);

constexpr quicr::Name object_id_mask = 0x00000000000000000000000000001111_name;
constexpr quicr::Name group_id_mask = 0x00000000000000000000111111110000_name;
static void
BM_NameRealArithmetic(benchmark::State& state)
{
  quicr::Name name = 0xA11CEE00F00001000000000000000000_name;
  for (auto _ : state) {
    name = (name & ~object_id_mask) | (++name & object_id_mask);

    auto group_id_bits = (++(name >> 16) << 16) & group_id_mask;
    name = ((name & ~group_id_mask) | group_id_bits) & ~object_id_mask;
  }
}

BENCHMARK(BM_NameRealArithmetic);
