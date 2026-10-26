// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include "quicr/containers/cache.h"

#include <timeq/tick_service.h>

// Compatibility shim until cache.h is migrated to timeq::tick_service directly.
namespace quicr {
    struct TickService : public timeq::tick_service
    {
        using TickType = tick_service::tick_type;
        using DurationType = std::chrono::microseconds;

        virtual TickType Milliseconds() const
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(get()).count();
        }

        virtual TickType Microseconds() const { return get().count(); }

        ~TickService() override = default;
    };
} // namespace quicr

using namespace quicr;

struct MockTickService : TickService
{
    void SetCurrentTime(std::chrono::milliseconds time) { time_ = time; }

    std::chrono::microseconds get() const override
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

    TEST_CASE("Cache expires during frequent access")
    {
        auto ticks = std::make_shared<MockTickService>();
        std::chrono::milliseconds start{ 0 };
        SUBCASE("Clock starts at zero") {}
        SUBCASE("Clock starts above zero")
        {
            start = std::chrono::milliseconds{ 37 };
        }

        ticks->SetCurrentTime(start);
        Cache<std::uint64_t, std::uint64_t> cache(1000, 100, ticks);
        cache.Insert(0, 42, 1000);

        for (int elapsed = 10; elapsed < 1000; elapsed += 10) {
            ticks->SetCurrentTime(start + std::chrono::milliseconds{ elapsed });
            CHECK(cache.Contains(0));
            cache.Insert(elapsed, 42, 1000);
        }

        ticks->SetCurrentTime(start + std::chrono::milliseconds{ 1000 });
        CHECK_FALSE(cache.Contains(0));
        CHECK(cache.Contains(990));
    }

    TEST_CASE("Cache preserves partial intervals when advancing")
    {
        auto ticks = std::make_shared<MockTickService>();
        ticks->SetCurrentTime(std::chrono::milliseconds{ 1 });
        Cache<std::uint64_t, std::uint64_t> cache(1000, 100, ticks);
        cache.Insert(0, 42, 300);

        ticks->SetCurrentTime(std::chrono::milliseconds{ 151 });
        CHECK(cache.Contains(0));
        ticks->SetCurrentTime(std::chrono::milliseconds{ 300 });
        CHECK(cache.Contains(0));
        ticks->SetCurrentTime(std::chrono::milliseconds{ 301 });
        CHECK_FALSE(cache.Contains(0));
    }

    TEST_CASE("Cache expires new entries after clearing on a long time jump")
    {
        auto ticks = std::make_shared<MockTickService>();
        Cache<std::uint64_t, std::uint64_t> cache(1000, 100, ticks);
        cache.Insert(0, 42, 1000);

        ticks->SetCurrentTime(std::chrono::milliseconds{ 2000 });
        CHECK_FALSE(cache.Contains(0));
        cache.Insert(1, 42, 300);

        ticks->SetCurrentTime(std::chrono::milliseconds{ 2299 });
        CHECK(cache.Contains(1));
        ticks->SetCurrentTime(std::chrono::milliseconds{ 2300 });
        CHECK_FALSE(cache.Contains(1));
    }
}
