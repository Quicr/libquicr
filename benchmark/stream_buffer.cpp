// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/stream_buffer.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <limits>

static void
StreamBuffer_Construct(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto __ = quicr::StreamBuffer<std::uint8_t>();
        benchmark::DoNotOptimize(__);
        benchmark::ClobberMemory();
    }
}

static void
StreamBuffer_Push(benchmark::State& state)
{
    quicr::StreamBuffer<std::uint8_t> buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(std::numeric_limits<std::uint8_t>::max());
    }
}

static void
StreamBuffer_PushBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    quicr::StreamBuffer<std::uint8_t> buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(buf);
    }
}

static void
StreamBuffer_PushLengthBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    quicr::StreamBuffer<std::uint8_t> buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.PushLengthBytes(buf);
    }
}

static void
StreamBuffer_Front(benchmark::State& state)
{
    quicr::StreamBuffer<std::uint8_t> buffer;
    std::vector<uint8_t> bytes(1280, 0);
    buffer.Push(bytes);

    for ([[maybe_unused]] const auto& _ : state) {
        auto data = buffer.Front(1280);
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }
}

static void
StreamBuffer_Pop(benchmark::State& state)
{
    quicr::StreamBuffer<std::uint8_t> buffer;
    std::vector<uint8_t> bytes(1'000'000'000, 0);
    buffer.Push(bytes);

    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Pop();
    }
}

static void
StreamBuffer_PopBytes(benchmark::State& state)
{
    quicr::StreamBuffer<std::uint8_t> buffer;
    std::vector<uint8_t> bytes(1'000'000'000, 0);
    buffer.Push(bytes);

    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Pop(10);
    }
}

static void
SafeStreamBuffer_Construct(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto __ = quicr::SafeStreamBuffer<std::uint8_t>();
        benchmark::DoNotOptimize(__);
        benchmark::ClobberMemory();
    }
}

static void
SafeStreamBuffer_Push(benchmark::State& state)
{
    quicr::SafeStreamBuffer<std::uint8_t> buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(std::numeric_limits<std::uint8_t>::max());
    }
}

static void
SafeStreamBuffer_PushBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    quicr::SafeStreamBuffer<std::uint8_t> buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(buf);
    }
}

static void
SafeStreamBuffer_PushLengthBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    quicr::SafeStreamBuffer<std::uint8_t> buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.PushLengthBytes(buf);
    }
}

static void
SafeStreamBuffer_Front(benchmark::State& state)
{
    quicr::SafeStreamBuffer<std::uint8_t> buffer;
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
BENCHMARK(StreamBuffer_Pop);
BENCHMARK(StreamBuffer_PopBytes);
BENCHMARK(SafeStreamBuffer_Construct);
BENCHMARK(SafeStreamBuffer_Push);
BENCHMARK(SafeStreamBuffer_PushBytes);
BENCHMARK(SafeStreamBuffer_PushLengthBytes);
BENCHMARK(SafeStreamBuffer_Front);
