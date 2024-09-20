// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <quicr/detail/uintvar.h>

#include <benchmark/benchmark.h>

#include <quicr/detail/messages.h>
#include <quicr/detail/serializer.h>

static void
Moq_EncodeGroupHeader(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        quicr::Serializer buffer;
        auto hdr = quicr::messages::MoqStreamHeaderGroup{};
        hdr.subscribe_id = 0x100;
        hdr.track_alias = 0x100;
        hdr.priority = 0xA;
        hdr.group_id = 0x1;
        buffer << hdr;
        benchmark::DoNotOptimize(hdr);
        benchmark::DoNotOptimize(buffer);
        benchmark::ClobberMemory();
    }
}

static void
Moq_EncodeGroupObject1000Bytes(benchmark::State& state)
{
    const auto payload = std::vector<uint8_t>(1000, 0);
    for ([[maybe_unused]] const auto& _ : state) {
        quicr::Serializer buffer;
        auto obj = quicr::messages::MoqStreamGroupObject{};
        obj.object_id = 0x1;
        obj.payload.reserve(payload.size());
        obj.payload.assign(payload.begin(), payload.end());
        buffer << obj;
        benchmark::DoNotOptimize(obj);
        benchmark::DoNotOptimize(buffer);
        benchmark::ClobberMemory();
    }
}

static void
Moq_EncodeGroupObject1Byte(benchmark::State& state)
{
    for ([[maybe_unused]] const auto& _ : state) {
        quicr::Serializer buffer;
        auto obj = quicr::messages::MoqStreamGroupObject{};
        obj.object_id = 0x1;
        obj.payload = { 0x01 };
        buffer << obj;
        benchmark::DoNotOptimize(obj);
        benchmark::DoNotOptimize(buffer);
        benchmark::ClobberMemory();
    }
}

static void
Moq_DecodeGroupHeader(benchmark::State& state)
{
    quicr::Serializer buffer;
    auto hdr = quicr::messages::MoqStreamHeaderGroup{};
    hdr.subscribe_id = 0x100;
    hdr.track_alias = 0x100;
    hdr.priority = 0xA;
    hdr.group_id = 0x1;
    buffer << hdr;

    const auto net_data = buffer.Take();
    quicr::StreamBuffer<uint8_t> sbuf;

    for ([[maybe_unused]] const auto& _ : state) {
        sbuf.PushLengthBytes(net_data);
        hdr = quicr::messages::MoqStreamHeaderGroup{};
        sbuf >> hdr;

        benchmark::DoNotOptimize(hdr);
        benchmark::ClobberMemory();
    }
}


static void
Moq_DecodeGroupObject(benchmark::State& state)
{
    const auto payload = std::vector<uint8_t>(1000,0);
    quicr::Serializer buffer;
    auto hdr = quicr::messages::MoqStreamGroupObject{};
    hdr.object_id = 0x100;
    hdr.payload.assign(payload.begin(), payload.end());
    buffer << hdr;

    const auto net_data = buffer.Take();
    quicr::StreamBuffer<uint8_t> sbuf;

    for ([[maybe_unused]] const auto& _ : state) {
        sbuf.PushLengthBytes(net_data);
        auto obj = quicr::messages::MoqStreamGroupObject{};
        sbuf >> hdr;

        benchmark::DoNotOptimize(obj);
        benchmark::ClobberMemory();
    }
}



BENCHMARK(Moq_EncodeGroupHeader);
BENCHMARK(Moq_EncodeGroupObject1000Bytes);
BENCHMARK(Moq_EncodeGroupObject1Byte);
BENCHMARK(Moq_DecodeGroupHeader);
BENCHMARK(Moq_DecodeGroupObject);