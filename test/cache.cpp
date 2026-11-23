// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include "quicr/clock.h"
#include "quicr/containers/cache.h"

// Compatibility shim until cache.h is migrated to timeq::tick_service directly.
namespace quicr {
    struct TickService : public Clock
    {
        using TickType = std::size_t;
        using DurationType = std::chrono::microseconds;

        virtual ~TickService() = default;

        virtual TickType Milliseconds() const
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(Get()).count();
        }

        virtual TickType Microseconds() const { return Get().count(); }
    };
} // namespace quicr

using namespace quicr;

struct MockTickService : TickService
{
    virtual ~MockTickService() = default;

    void SetCurrentTime(std::chrono::milliseconds time) { time_ = time; }

    std::chrono::microseconds Get() const override
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(time_);
    }

  private:
    std::chrono::milliseconds time_{ 0 };
};

TEST_SUITE("Cache")
{
    TEST_CASE("Cache Retrieval")
    {
        typedef std::uint64_t Key;
        typedef std::vector<std::uint64_t> Value;
        auto cache = Cache<Key, Value>(1000, 100, std::make_shared<MockTickService>());
        constexpr Key target_key = 0;
        Value expected = { 0, 1 };
        cache.Insert(target_key, expected, 1000);
        Value expected_second = { 0 };
        cache.Insert(target_key + 1, expected_second, 1000);

        CHECK(cache.Contains(target_key));
        CHECK(cache.Contains(target_key + 1));
        CHECK(cache.Contains(target_key, target_key + 1));
        CHECK_THROWS_AS(cache.Contains(target_key + 1, target_key), const std::invalid_argument&);

        auto retrieved = cache.Get(target_key, target_key + 1);
        CHECK(*retrieved[0] == expected);
        CHECK(*retrieved[1] == expected_second);
    }

    TEST_CASE("Cache expires entries during frequent access")
    {
        auto ticks = std::make_shared<MockTickService>();
        std::chrono::milliseconds start{ 0 };
        SUBCASE("Clock starts at zero") {}
        SUBCASE("Clock starts mid-interval")
        {
            start = std::chrono::milliseconds{ 37 };
        }
        SUBCASE("Clock starts far from zero")
        {
            start = std::chrono::milliseconds{ 123'456'789 };
        }

        ticks->SetCurrentTime(start);
        Cache<std::uint64_t, std::uint64_t> cache(1000, 100, ticks);
        cache.Insert(0, 42, 1000);

        for (int elapsed = 10; elapsed < 1000; elapsed += 10) {
            ticks->SetCurrentTime(start + std::chrono::milliseconds{ elapsed });
            CHECK(cache.Contains(0));
            cache.Insert(elapsed, 42, 1000);
        }

        ticks->SetCurrentTime(start + std::chrono::milliseconds{ 1099 });
        CHECK_FALSE(cache.Contains(0));
        CHECK(cache.Contains(990));
    }

    TEST_CASE("Cache entry lives for at least its TTL and less than TTL plus interval")
    {
        constexpr int interval = 100;
        int ttl = 0;
        SUBCASE("Single interval")
        {
            ttl = 100;
        }
        SUBCASE("Partial interval")
        {
            ttl = 150;
        }
        SUBCASE("Maximum duration")
        {
            ttl = 1000;
        }

        for (int offset = 0; offset < interval; ++offset) {
            CAPTURE(offset);
            auto ticks = std::make_shared<MockTickService>();
            Cache<std::uint64_t, std::uint64_t> cache(1000, interval, ticks);
            const int inserted = 1000 + offset;
            ticks->SetCurrentTime(std::chrono::milliseconds{ inserted });
            cache.Insert(0, 42, ttl);

            ticks->SetCurrentTime(std::chrono::milliseconds{ inserted + ttl - 1 });
            CHECK(cache.Contains(0));
            ticks->SetCurrentTime(std::chrono::milliseconds{ inserted + ttl + interval - 1 });
            CHECK_FALSE(cache.Contains(0));
        }
    }

    TEST_CASE("Cache expires entries inserted after a long time jump")
    {
        auto ticks = std::make_shared<MockTickService>();
        Cache<std::uint64_t, std::uint64_t> cache(1000, 100, ticks);
        cache.Insert(0, 42, 1000);

        ticks->SetCurrentTime(std::chrono::milliseconds{ 2050 });
        CHECK_FALSE(cache.Contains(0));
        cache.Insert(1, 42, 300);

        ticks->SetCurrentTime(std::chrono::milliseconds{ 2349 });
        CHECK(cache.Contains(1));
        ticks->SetCurrentTime(std::chrono::milliseconds{ 2449 });
        CHECK_FALSE(cache.Contains(1));
    }
}
