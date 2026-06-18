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
        auto buffer = quicr::StreamBuffer<std::uint8_t>();
        benchmark::DoNotOptimize(buffer);
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

    for ([[maybe_unused]] const auto& _ : state) {
        quicr::StreamBuffer<std::uint8_t>{}.Push(buf);
    }
}

static void
StreamBuffer_PushLengthBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    for ([[maybe_unused]] const auto& _ : state) {
        quicr::StreamBuffer<std::uint8_t>{}.PushLengthBytes(buf);
    }
}

static void
StreamBuffer_Pop(benchmark::State& state)
{
    quicr::StreamBuffer<std::uint8_t> buffer;
    for (int64_t i = 0; i < state.max_iterations; ++i) {
        buffer.Push(std::vector<uint8_t>(1280, uint8_t(0)));
    }

    const std::size_t num = state.range(0);

    int64_t items_count = 0;
    for ([[maybe_unused]] const auto& _ : state) {
        ++items_count;

        buffer.Pop(num);
    }

    state.SetItemsProcessed(items_count);
}

static void
StreamBuffer_Front(benchmark::State& state)
{
    quicr::StreamBuffer<std::uint8_t> buffer;

    buffer.Push(std::vector<uint8_t>(1280, 0));

    for ([[maybe_unused]] const auto& _ : state) {
        auto data = buffer.Front(1280);
        benchmark::DoNotOptimize(data);
        benchmark::ClobberMemory();
    }
}

static void
SafeStreamBuffer_Construct(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto buffer = quicr::SafeStreamBuffer<std::uint8_t>();
        benchmark::DoNotOptimize(buffer);
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

    for ([[maybe_unused]] const auto& _ : state) {
        quicr::StreamBuffer<std::uint8_t>{}.Push(buf);
    }
}

static void
SafeStreamBuffer_PushLengthBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    for ([[maybe_unused]] const auto& _ : state) {
        quicr::StreamBuffer<std::uint8_t>{}.PushLengthBytes(buf);
    }
}

static void
SafeStreamBuffer_Pop(benchmark::State& state)
{
    quicr::SafeStreamBuffer<std::uint8_t> buffer;
    for (int64_t i = 0; i < state.max_iterations; ++i) {
        buffer.Push(std::vector<uint8_t>(1280, uint8_t(0)));
    }

    const std::size_t num = state.range(0);

    int64_t items_count = 0;
    for ([[maybe_unused]] const auto& _ : state) {
        ++items_count;

        buffer.Pop(num);
    }

    state.SetItemsProcessed(items_count);
}

static void
SafeStreamBuffer_Front(benchmark::State& state)
{
    quicr::SafeStreamBuffer<std::uint8_t> buffer;

    buffer.Push(std::vector<uint8_t>(1280, 0));

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
BENCHMARK(StreamBuffer_Pop)->Iterations(1'000'000)->Arg(1)->Arg(16)->Arg(128)->Arg(1280);
BENCHMARK(StreamBuffer_Front);
BENCHMARK(SafeStreamBuffer_Construct);
BENCHMARK(SafeStreamBuffer_Push);
BENCHMARK(SafeStreamBuffer_PushBytes);
BENCHMARK(SafeStreamBuffer_PushLengthBytes);
BENCHMARK(SafeStreamBuffer_Pop)->Iterations(1'000'000)->Arg(1)->Arg(16)->Arg(128)->Arg(1280);
BENCHMARK(SafeStreamBuffer_Front);
