#include <benchmark/benchmark.h>

#include <quicr/quicr_name.h>
#include <vector>

static void
Name_ConstructFromHexString(benchmark::State& state)
{
  std::string str = "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(str);
  }
}

static void
Name_ConstructFromHexStringView(benchmark::State& state)
{
  std::string_view str = "0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";
  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(str);
  }
}

static void
Name_ConstructFromVector(benchmark::State& state)
{
  std::vector<uint8_t> data = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
  };
  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(data);
  }
}

static void
Name_ConstructFromBytePointer(benchmark::State& state)
{
  std::vector<uint8_t> vec_data = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0xFF, 0xFF };

  uint8_t* data = vec_data.data();
  size_t length = vec_data.size();

  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(data, length);
  }
}

static void
Name_CopyConstruct(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    [[maybe_unused]] quicr::Name __(name);
  }
}

static void
Name_LeftShift(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    name << 64;
  }
}

static void
Name_RightShift(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    name >> 64;
  }
}

static void
Name_Add(benchmark::State& state)
{
  quicr::Name name = 0x0_name;
  for (auto _ : state) {
    ++name;
  }
}

static void
Name_Sub(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    --name;
  }
}

static void
Name_ToHex(benchmark::State& state)
{
  quicr::Name name = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF_name;
  for (auto _ : state) {
    name.to_hex();
  }
}

BENCHMARK(Name_ConstructFromHexString);
BENCHMARK(Name_ConstructFromHexStringView);
BENCHMARK(Name_ConstructFromVector);
BENCHMARK(Name_ConstructFromBytePointer);
BENCHMARK(Name_CopyConstruct);
BENCHMARK(Name_LeftShift);
BENCHMARK(Name_RightShift);
BENCHMARK(Name_Add);
BENCHMARK(Name_Sub);
BENCHMARK(Name_ToHex);

constexpr quicr::Name object_id_mask = 0x00000000000000000000000000001111_name;
constexpr quicr::Name group_id_mask = 0x00000000000000000000111111110000_name;
static void
Name_RealArithmetic(benchmark::State& state)
{
  quicr::Name name = 0xA11CEE00F00001000000000000000000_name;
  for (auto _ : state) {
    name = (name & ~object_id_mask) | (++name & object_id_mask);

    auto group_id_bits = (++(name >> 16) << 16) & group_id_mask;
    name = ((name & ~group_id_mask) | group_id_bits) & ~object_id_mask;
  }
}

BENCHMARK(Name_RealArithmetic);
