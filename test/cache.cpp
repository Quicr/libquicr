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
        typedef std::uint64_t Value;
        auto cache = Cache<Key, Value>(1000, 100, std::make_shared<MockTickService>());
        constexpr Key target_key = 0;
        cache.Insert(target_key, 0, 1000);
        cache.Insert(target_key, 1, 1000);
        cache.Insert(target_key + 1, 0, 1000);

        // Lookup by matching key.
        CHECK(cache.Contains(target_key));
        // Lookup by matching intra range.
        CHECK(cache.Contains(target_key, target_key));
        // Lookup by matching (key+1).
        CHECK(cache.Contains(target_key + 1));
        // Lookup by matching range.
        CHECK(cache.Contains(target_key, target_key + 1));
    }
}