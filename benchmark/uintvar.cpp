// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <moq/detail/uintvar.h>

#include <benchmark/benchmark.h>

#include <cstdint>

static void
Serializer_ToUintVar(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto value = moq::ToUintV(0x123456789);
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }
}

static void
Serializer_FromUintVar(benchmark::State& state)
{
    auto var_int = moq::ToUintV(0x123456789);
    for ([[maybe_unused]] const auto& _ : state) {
        auto value = moq::ToUint64(var_int);
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }
}

BENCHMARK(Serializer_ToUintVar);
BENCHMARK(Serializer_FromUintVar);