// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/common.h>
#include <quicr/hash.h>
#include <quicr/track_name.h>

#include <benchmark/benchmark.h>

using namespace quicr;
using namespace std::string_literals;

static void
ToHash(benchmark::State& state)
{
    TrackNamespace ns{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s };

    std::vector<uint8_t> ns_bytes{ ns.begin(), ns.end() };

    for ([[maybe_unused]] const auto& _ : state) {
        auto h = hash(ns_bytes);
        benchmark::DoNotOptimize(h);
    }
}

static void
ToHashStl(benchmark::State& state)
{
    TrackNamespace ns{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s };

    std::string_view ns_sv(reinterpret_cast<const char*>(ns.data()), ns.size());

    for ([[maybe_unused]] const auto& _ : state) {
        auto h = std::hash<std::string_view>{}(ns_sv);
        benchmark::DoNotOptimize(h);
        benchmark::ClobberMemory();
    }
}

BENCHMARK(ToHash);
BENCHMARK(ToHashStl);
