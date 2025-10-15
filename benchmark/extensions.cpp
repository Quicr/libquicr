#include <benchmark/benchmark.h>
#include <quicr/detail/messages.h>
#include <quicr/detail/stream_buffer.h>

using namespace quicr;
using namespace quicr::messages;

// Helper function to create test extensions
static Extensions
CreateTestExtensions(const std::uint64_t count)
{
    Extensions extensions;
    for (std::uint64_t i = 0; i < count; ++i) {
        std::uint64_t index = 1000 + i;
        Bytes bytes(sizeof(index));
        std::memcpy(bytes.data(), &index, sizeof(index));
        extensions.insert({ index, std::move(bytes) });
    }
    return extensions;
}

// Benchmark serialization with varying extension counts
static void
ExtensionsSerialize(benchmark::State& state)
{
    const auto extension_count = static_cast<std::uint64_t>(state.range(0));

    // Pre-create test data outside the benchmark loop
    Extensions extensions = CreateTestExtensions(extension_count);
    Extensions immutable = CreateTestExtensions(extension_count);

    // Track bytes processed for throughput calculation
    std::size_t bytes_processed = 0;

    for ([[maybe_unused]] const auto& _ : state) {
        Bytes buffer;
        SerializeExtensions(buffer, extensions, immutable);
        bytes_processed += buffer.size();
        benchmark::DoNotOptimize(buffer);
        benchmark::ClobberMemory();
    }

    state.SetBytesProcessed(static_cast<int64_t>(bytes_processed));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * (extensions.size() + immutable.size())));
}

// Benchmark deserialization
static void
ExtensionsDeserialize(benchmark::State& state)
{
    const auto extension_count = static_cast<std::uint64_t>(state.range(0));

    // Pre-serialize test data
    Extensions original_extensions = CreateTestExtensions(extension_count);
    Extensions original_immutable = CreateTestExtensions(extension_count);

    Bytes serialized_data;
    SerializeExtensions(serialized_data, original_extensions, original_immutable);

    std::size_t bytes_processed = 0;

    for ([[maybe_unused]] const auto& _ : state) {
        // Create a stream buffer from the serialized data
        auto stream_buffer = SafeStreamBuffer<uint8_t>();
        stream_buffer.Push(std::span<const uint8_t>(serialized_data));

        // Parse the extensions
        std::optional<std::size_t> extension_headers_length;
        std::optional<Extensions> extensions;
        std::optional<Extensions> immutable_extensions;
        std::size_t extension_bytes_remaining = 0;
        std::optional<std::uint64_t> current_header;

        bool success = ParseExtensions(stream_buffer,
                                       extension_headers_length,
                                       extensions,
                                       immutable_extensions,
                                       extension_bytes_remaining,
                                       current_header);

        benchmark::DoNotOptimize(success);
        benchmark::DoNotOptimize(extensions);
        benchmark::DoNotOptimize(immutable_extensions);
        benchmark::ClobberMemory();

        bytes_processed += serialized_data.size();
    }

    state.SetBytesProcessed(static_cast<int64_t>(bytes_processed));
    state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations() * (original_extensions.size() + original_immutable.size())));
}

// Benchmark round-trip (serialize + deserialize)
static void
ExtensionsRoundTrip(benchmark::State& state)
{
    const auto extension_count = static_cast<std::uint64_t>(state.range(0));

    Extensions original_extensions = CreateTestExtensions(extension_count);
    Extensions original_immutable = CreateTestExtensions(extension_count);

    std::size_t bytes_processed = 0;

    for ([[maybe_unused]] const auto& _ : state) {
        // Serialize
        Bytes serialized_data;
        SerializeExtensions(serialized_data, original_extensions, original_immutable);

        // Deserialize
        auto stream_buffer = SafeStreamBuffer<uint8_t>();
        stream_buffer.Push(std::span<const uint8_t>(serialized_data));

        std::optional<std::size_t> extension_headers_length;
        std::optional<Extensions> extensions;
        std::optional<Extensions> immutable_extensions;
        std::size_t extension_bytes_remaining = 0;
        std::optional<std::uint64_t> current_header;

        bool success = ParseExtensions(stream_buffer,
                                       extension_headers_length,
                                       extensions,
                                       immutable_extensions,
                                       extension_bytes_remaining,
                                       current_header);

        benchmark::DoNotOptimize(success);
        benchmark::DoNotOptimize(extensions);
        benchmark::DoNotOptimize(immutable_extensions);
        benchmark::ClobberMemory();

        bytes_processed += serialized_data.size();
    }

    state.SetBytesProcessed(static_cast<int64_t>(bytes_processed));
    state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations() * (original_extensions.size() + original_immutable.size())));
}

// Benchmark with different extension counts
BENCHMARK(ExtensionsSerialize)->Args({ 1 })->Args({ 10 })->Args({ 100 })->Args({ 1000 })->Unit(benchmark::kMicrosecond);

BENCHMARK(ExtensionsDeserialize)
  ->Args({ 1 })
  ->Args({ 10 })
  ->Args({ 100 })
  ->Args({ 1000 })
  ->Unit(benchmark::kMicrosecond);

BENCHMARK(ExtensionsRoundTrip)->Args({ 1 })->Args({ 10 })->Args({ 100 })->Args({ 1000 })->Unit(benchmark::kMicrosecond);
