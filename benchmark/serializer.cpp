// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <moq/detail/serializer.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <limits>

static void
Serializer_Construct(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        const [[maybe_unused]] const auto& __ = moq::Serializer(1000);
    }
}

static void
Serializer_Push(benchmark::State& state)
{
    moq::Serializer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer << std::numeric_limits<std::uint8_t>::max();
    }
}

static void
Serializer_Push16(benchmark::State& state)
{
    moq::Serializer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer << std::numeric_limits<std::uint16_t>::max();
    }
}

static void
Serializer_Push32(benchmark::State& state)
{
    moq::Serializer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer << std::numeric_limits<std::uint32_t>::max();
    }
}

static void
Serializer_Push64(benchmark::State& state)
{
    moq::Serializer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer << std::numeric_limits<std::uint64_t>::max();
    }
}

static void
Serializer_PushBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    moq::Serializer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(buf);
    }
}

static void
Serializer_PushBytesReserved(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    moq::Serializer buffer(100000000);
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(buf);
    }
}

static void
Serializer_PushLengthBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    moq::Serializer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.PushLengthBytes(buf);
    }
}

BENCHMARK(Serializer_Construct);
BENCHMARK(Serializer_Push);
BENCHMARK(Serializer_Push16);
BENCHMARK(Serializer_Push32);
BENCHMARK(Serializer_Push64);
BENCHMARK(Serializer_PushBytes);
BENCHMARK(Serializer_PushBytesReserved);
BENCHMARK(Serializer_PushLengthBytes);
