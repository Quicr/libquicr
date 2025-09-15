// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/common.h>
#include <quicr/hash.h>
#include <quicr/track_name.h>

#include <benchmark/benchmark.h>

using namespace quicr;
using namespace std::string_literals;

const TrackNamespace kNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s };
const std::string kNameStr = "test";
const FullTrackName kFullTrackName(kNamespace, { kNameStr.begin(), kNameStr.end() });

static void
TrackNamespace_ToHash(benchmark::State& state)
{

    for ([[maybe_unused]] const auto& _ : state) {
        auto h = hash(ns);
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
BENCHMARK(TrackNameHash_Construct);
