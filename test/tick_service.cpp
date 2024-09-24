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
        const auto& start_time = std::chrono::steady_clock::now();
        const auto start_ticks = var::tick_service.Milliseconds();

        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time_ms));

        const auto delta_ticks = var::tick_service.Milliseconds() - start_ticks;
        const uint64_t delta_time =
          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();

        // Allow variance difference
        CHECK((delta_ticks >= delta_time - 1 && delta_ticks <= delta_time + 1));
    }
}

TEST_CASE("TickService microseconds")
{
    constexpr int sleep_time_us = 800;

    for (int i = 0; i < 10; i++) {
        const auto& start_time = std::chrono::steady_clock::now();
        const auto start_ticks = var::tick_service.Microseconds();

        std::this_thread::sleep_for(std::chrono::microseconds(sleep_time_us));

        const auto delta_ticks = var::tick_service.Microseconds() - start_ticks;
        const uint64_t delta_time =
          std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time).count();
        
        // Allow variance difference
        CHECK((delta_ticks >= delta_time - 250 && delta_ticks <= delta_time + 250));
    }
}
