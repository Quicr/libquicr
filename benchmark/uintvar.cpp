// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/uintvar.h>

#include <benchmark/benchmark.h>

#include <cstdint>

static void
UIntVar_FromUint64(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto value = quicr::UintVar(0x123456789);
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }
}

static void
UIntVar_ToUint64(benchmark::State& state)
{
    constexpr auto kVarInt = quicr::UintVar(0x123456789);
    for ([[maybe_unused]] const auto& _ : state) {
        auto value = uint64_t(kVarInt);
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }
}

static void
UIntVar_ToBytes(benchmark::State& state)
{
    constexpr auto kVarInt = quicr::UintVar(0x123456789);
    for ([[maybe_unused]] const auto& _ : state) {
        auto bytes = kVarInt.Bytes();
        benchmark::DoNotOptimize(bytes);
        benchmark::ClobberMemory();
    }
}

static void
UIntVar_FromBytes(benchmark::State& state)
{
    constexpr auto kVar = quicr::UintVar(0x123456789);
    auto&& bytes = kVar.Bytes();
    for ([[maybe_unused]] const auto& _ : state) {
        auto value = quicr::UintVar(bytes);
        benchmark::DoNotOptimize(value);
        benchmark::ClobberMemory();
    }
}

BENCHMARK(UIntVar_FromUint64);
BENCHMARK(UIntVar_ToUint64);
BENCHMARK(UIntVar_ToBytes);
BENCHMARK(UIntVar_FromBytes);
