// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/ctrl_message_types.h"

#include <numeric>
#include <quicr/common.h>
#include <quicr/track_name.h>
#include <unordered_set>
#include <vector>

#include <benchmark/benchmark.h>

using namespace quicr;
using namespace std::string_literals;

const TrackNamespace kNamespace{ "example"s, "chat555"s, "user1"s, "dev1"s, "time1"s };
const std::string kNameStr = "test";
const FullTrackName kFullTrackName{ kNamespace, std::vector<uint8_t>{ kNameStr.begin(), kNameStr.end() } };

template<typename Key, typename Value, unsigned int N>
struct VarMapHelper
{
    typedef std::map<Key, typename VarMapHelper<Key, Value, N - 1>::type> type;
};

template<typename Key, typename Value>
struct VarMapHelper<Key, Value, 1>
{
    typedef std::map<Key, Value> type;
};

template<typename Key, typename Value, unsigned int N>
using VarMap = typename VarMapHelper<Key, Value, N>::type;

struct ValueObject
{
    std::string some_string;
    uint64_t some_value;
};

template<typename T, std::enable_if_t<std::is_integral<T>::value || std::is_floating_point<T>::value, bool> = true>
std::span<const uint8_t>
BytesOf(const T& value)
{
    return std::span(reinterpret_cast<const uint8_t*>(&value), sizeof(value));
}

std::vector<std::size_t>
PrefixHashNamespaceTuples(TrackNamespace name_space)
{
    const auto& entries = name_space.GetHashes();
    std::vector<std::size_t> hashes(entries.size());

    uint64_t hash = 0;
    for (std::size_t i = 0; i < hashes.size(); ++i) {
        hash_combine(hash, hashes[i]);
        hashes[i] = hash;
    }

    return hashes;
}

static void
TrackNamespace_ToStateMap(benchmark::State& state)
{
    std::map<uint64_t, ValueObject> value_map;
    ValueObject value_object{ "hello", 0x123456 };
    value_map.emplace(1, value_object);

    std::map<uint64_t, std::map<uint64_t, ValueObject>> data_map;
    std::map<uint64_t, std::unordered_set<uint64_t>> prefix_lookup_map;

    TrackNamespace name_space{ "one"s, "two"s, "3"s, "this is value 4"s, "last value is five"s };

    uint64_t i = 0;
    for ([[maybe_unused]] const auto& _ : state) {
        auto index = BytesOf(i);
        auto tfn = FullTrackName{ name_space, { index.begin(), index.end() } };
        auto th = TrackHash(tfn);

        const auto data_map_it = data_map.find(th.track_fullname_hash);
        if (data_map_it == data_map.end()) {
            data_map.emplace(th.track_fullname_hash, value_map);
        } else {
            const auto node_map_it = data_map_it->second.find(1);
            if (node_map_it == data_map_it->second.end()) {
                data_map_it->second.emplace(1, value_object);
            }

            // If needed, update value of node map entry
        }

        for (const auto& prefix_hash : PrefixHashNamespaceTuples(name_space)) {
            const auto it = prefix_lookup_map.find(prefix_hash);
            if (it == prefix_lookup_map.end()) {
                prefix_lookup_map.emplace(prefix_hash, std::unordered_set{ th.track_fullname_hash });
            } else {
                if (auto s_it = it->second.find(th.track_fullname_hash); s_it != it->second.end()) {
                    it->second.emplace(th.track_fullname_hash);
                }
            }
        }

        ++i;

        benchmark::DoNotOptimize(tfn);
        benchmark::DoNotOptimize(th);
        benchmark::DoNotOptimize(data_map);
        benchmark::DoNotOptimize(prefix_lookup_map);
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(i);
}

BENCHMARK(TrackNamespace_ToStateMap);
