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
}
