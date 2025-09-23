// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/common.h>
#include <quicr/hash.h>
#include <quicr/track_name.h>

#include <benchmark/benchmark.h>

#include <string_view>

using namespace quicr;
using namespace std::string_literals;

const TrackNamespace kNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s };
const std::string kNameStr = "test";
const FullTrackName kFullTrackName{ kNamespace, std::vector<uint8_t>{ kNameStr.begin(), kNameStr.end() } };

static void
TrackNamespace_ToHash(benchmark::State& state)
{

    for ([[maybe_unused]] const auto& _ : state) {
        auto h = quicr::hash(kNamespace);
        benchmark::DoNotOptimize(h);
        benchmark::ClobberMemory();
    }
}

static void
TrackNamespace_ToSTLHash(benchmark::State& state)
{

    for ([[maybe_unused]] const auto& _ : state) {
        auto h = std::hash<std::string_view>{}({ reinterpret_cast<const char*>(kNamespace.data()), kNamespace.size() });
        benchmark::DoNotOptimize(h);
        benchmark::ClobberMemory();
    }
}

static void
TrackNameHash_Construct(benchmark::State& state)
{

    for ([[maybe_unused]] const auto& _ : state) {
        auto h = TrackHash(kFullTrackName);
        benchmark::DoNotOptimize(h);
        benchmark::ClobberMemory();
    }
}

BENCHMARK(TrackNamespace_ToHash);
BENCHMARK(TrackNamespace_ToSTLHash);
BENCHMARK(TrackNameHash_Construct);
