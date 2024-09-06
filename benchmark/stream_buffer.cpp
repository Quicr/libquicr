// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <moq/detail/stream_buffer.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <limits>

static void
StreamBuffer_Construct(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto __ = moq::StreamBuffer<std::uint8_t>();
        benchmark::DoNotOptimize(__);
        benchmark::ClobberMemory();
    }
}

static void
StreamBuffer_Push(benchmark::State& state)
{
    moq::StreamBuffer<std::uint8_t> buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(std::numeric_limits<std::uint8_t>::max());
    }
}

static void
StreamBuffer_PushBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    moq::StreamBuffer<std::uint8_t> buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(buf);
    }
}

static void
StreamBuffer_PushLengthBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    moq::StreamBuffer<std::uint8_t> buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.PushLengthBytes(buf);
    }
}

static void
StreamBuffer_Front(benchmark::State& state)
{
    moq::StreamBuffer<std::uint8_t> buffer;
    std::vector<uint8_t> bytes(1280, 0);
    buffer.Push(bytes);

    for ([[maybe_unused]] const auto& _ : state) {
        auto data = buffer.Front(1280);
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }
}

BENCHMARK(StreamBuffer_Construct);
BENCHMARK(StreamBuffer_Push);
BENCHMARK(StreamBuffer_PushBytes);
BENCHMARK(StreamBuffer_PushLengthBytes);
BENCHMARK(StreamBuffer_Front);
