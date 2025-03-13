// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include <quicr/cache.h>

using namespace quicr;

struct MockTickService : TickService
{
    void SetCurrentDuration(const DurationType duration) { milliseconds_ = duration; }
    TickType Milliseconds() const override { return milliseconds_.count(); }
    TickType Microseconds() const override { return milliseconds_.count() * 1000; }

  private:
    DurationType milliseconds_ = DurationType(0);
};

TEST_SUITE("Cache")
{
    TEST_CASE("Cache Retrieval")
    {
        // Should be able to find objects that have been inserted.
        auto time = std::make_shared<MockTickService>();
        typedef std::uint64_t Key;
        typedef std::vector<std::uint64_t> Value;
        auto cache = Cache<Key, Value>(1000, 100, std::make_shared<MockTickService>());
        constexpr Key target_key = 0;
        Value expected = { 0, 1 };
        cache.Insert(target_key, expected, 1000);
        Value expected_second = { 0 };
        cache.Insert(target_key + 1, expected_second, 1000);

        // Lookup by matching key.
        CHECK(cache.Contains(target_key));
        // Lookup by matching intra range would throw.
        CHECK_THROWS_AS(cache.Contains(target_key, target_key), const std::invalid_argument&);
        // Lookup by matching (key+1).
        CHECK(cache.Contains(target_key + 1));
        // Lookup by matching range.
        CHECK(cache.Contains(target_key, target_key + 1));

        // Check throws on intra-get and backwards range.
        CHECK_THROWS_AS(cache.Get(target_key, target_key), const std::invalid_argument&);
        CHECK_THROWS_AS(cache.Get(target_key + 1, target_key), const std::invalid_argument&);

        // Get target key.
        auto retrieved = cache.Get(target_key, target_key + 1);
        REQUIRE(retrieved.size() == 1);
        CHECK(*retrieved[0] == expected);

        // Get both keys.
        retrieved = cache.Get(target_key, target_key + 2);
        REQUIRE(retrieved.size() == 2);
        CHECK(*retrieved[0] == expected);
        CHECK(*retrieved[1] == expected_second);
    }
}