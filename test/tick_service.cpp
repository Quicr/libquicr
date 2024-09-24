#include <doctest/doctest.h>
#include <iostream>

#include <quicr/detail/time_queue.h>
#include <unistd.h>

namespace var {
    auto tick_service = quicr::ThreadedTickService();
}

TEST_CASE("TickService milliseconds")
{
    constexpr int sleep_time_ms = 3;

    for (int i = 0; i < 10; i++) {
        const auto start = var::tick_service.Milliseconds();
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));

        const auto delta = var::tick_service.Milliseconds() - start;

        // Allow variance difference
        CHECK((delta >= sleep_time_ms - 2 && delta <= sleep_time_ms + 2));
    }
}

TEST_CASE("TickService microseconds")
{
    constexpr int sleep_time_us = 800;

    for (int i = 0; i < 10; i++) {
        const auto start_us = var::tick_service.Microseconds();
        std::this_thread::sleep_for(std::chrono::microseconds(sleep_time_us));

        const auto delta = var::tick_service.Microseconds() - start_us;

        // Allow variance difference
        CHECK((delta >= sleep_time_us - 500 && delta <= sleep_time_us + 500));
    }
}
