#include <benchmark/benchmark.h>

#include <quicr/hex_endec.h>

static void
BM_HexEndecEncode4x32_to_128(benchmark::State& state)
{
  quicr::HexEndec<128, 32, 32, 32, 32> format;
  for (auto _ : state) {
    format.Encode(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
  }
}

static void
BM_HexEndecDecode128_to_4x32(benchmark::State& state)
{
  quicr::HexEndec<128, 32, 32, 32, 32> format;
  for (auto _ : state) {
    format.Decode("0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF");
  }
}

static void
BM_HexEndecEncode4x16_to_64(benchmark::State& state)
{
  quicr::HexEndec<64, 16, 16, 16, 16> format;
  for (auto _ : state) {
    format.Encode(0xFFFFu, 0xFFFFu, 0xFFFFu, 0xFFFFu);
  }
}

static void
BM_HexEndecDecode64_to_4x16(benchmark::State& state)
{
  quicr::HexEndec<64, 16, 16, 16, 16> format;
  for (auto _ : state) {
    format.Decode("0xFFFFFFFFFFFFFFFF");
  }
}

BENCHMARK(BM_HexEndecEncode4x32_to_128);
BENCHMARK(BM_HexEndecDecode128_to_4x32);
BENCHMARK(BM_HexEndecEncode4x16_to_64);
BENCHMARK(BM_HexEndecDecode64_to_4x16);

static void
BM_HexEndecRealEncode(benchmark::State& state)
{
  quicr::HexEndec<128, 24, 8, 24, 8, 16, 48> format;
  auto time = std::time(0);
  const uint32_t orgId = 0x00A11CEE;
  const uint8_t appId = 0x00;
  const uint32_t confId = 0x00F00001;
  const uint8_t mediaType = 0x00 | 0x1;
  const uint16_t clientId = 0xFFFF;
  const uint64_t uniqueId = time;
  for (auto _ : state) {
    format.Encode(orgId, appId, confId, mediaType, clientId, uniqueId);
  }
}

static void
BM_HexEndecRealDecode(benchmark::State& state)
{
  quicr::HexEndec<128, 24, 8, 24, 8, 16, 48> format;
  quicr::Name name{ "0xA11CEE00F00001000000000000000000" };
  for (auto _ : state) {
    format.Decode(name);
  }
}

BENCHMARK(BM_HexEndecRealEncode);
BENCHMARK(BM_HexEndecRealDecode);
