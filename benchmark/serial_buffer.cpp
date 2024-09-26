// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/serial_buffer.h>

#include <benchmark/benchmark.h>

#include <cstdint>
#include <limits>

static void
SerialBuffer_Construct(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        auto serializer = quicr::SerialBuffer(1000);
        benchmark::DoNotOptimize(serializer);
    }
}

static void
SerialBuffer_Push(benchmark::State& state)
{
    quicr::SerialBuffer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer << std::numeric_limits<std::uint8_t>::max();
    }
}

static void
SerialBuffer_Push16(benchmark::State& state)
{
    quicr::SerialBuffer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer << std::numeric_limits<std::uint16_t>::max();
    }
}

static void
SerialBuffer_Push32(benchmark::State& state)
{
    quicr::SerialBuffer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer << std::numeric_limits<std::uint32_t>::max();
    }
}

static void
SerialBuffer_Push64(benchmark::State& state)
{
    quicr::SerialBuffer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer << std::numeric_limits<std::uint64_t>::max();
    }
}

static void
SerialBuffer_PushBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    quicr::SerialBuffer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(buf);
    }
}

static void
SerialBuffer_PushBytesReserved(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    quicr::SerialBuffer buffer(1000000 * buf.size());
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(buf);
    }
}

static void
SerialBuffer_PushLengthBytes(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    quicr::SerialBuffer buffer;
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.PushLengthBytes(buf);
    }
}

static void
SerialBuffer_ReuseAndPush(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    quicr::SerialBuffer buffer(1280);
    for ([[maybe_unused]] const auto& _ : state) {
        buffer.Push(buf);
        buffer.Clear();
    }
}

static void
SerialBuffer_CreateAndPush(benchmark::State& state)
{
    std::vector<uint8_t> buf(1280, 0);

    for ([[maybe_unused]] const auto& _ : state) {
        quicr::SerialBuffer buffer;
        buffer.Push(buf);
    }
}

BENCHMARK(SerialBuffer_Construct);
BENCHMARK(SerialBuffer_Push);
BENCHMARK(SerialBuffer_Push16);
BENCHMARK(SerialBuffer_Push32);
BENCHMARK(SerialBuffer_Push64);
BENCHMARK(SerialBuffer_PushBytes);
BENCHMARK(SerialBuffer_PushBytesReserved);
BENCHMARK(SerialBuffer_PushLengthBytes);
BENCHMARK(SerialBuffer_ReuseAndPush);
BENCHMARK(SerialBuffer_CreateAndPush);
