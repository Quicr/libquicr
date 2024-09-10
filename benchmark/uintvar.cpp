// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/uintvar.h>

#include <benchmark/benchmark.h>

#include <cstdint>

static void
UInt64_ToUintVar(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto value = quicr::ToUintV(0x123456789);
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }
}

static void
UInt64_FromUintVar(benchmark::State& state)
{
    auto var_int = quicr::ToUintV(0x123456789);
    for ([[maybe_unused]] const auto& _ : state) {
        auto value = quicr::ToUint64(var_int);
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }
}

BENCHMARK(UInt64_ToUintVar);
BENCHMARK(UInt64_FromUintVar);
