// SPDX-FileCopyrightText: Copyright (c) 2023 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

/**
 *  time_queue.h
 *
 *  Description:
 *      A time based queue, where the length of the queue is a duration,
 *      divided into buckets based on a given time interval. As time
 *      progresses, buckets in the past are cleared, and the main queue
 *      is updated so that the front only returns a valid object that
 *      has not expired. To improve performance, buckets are only cleared
 *      on push or pop operations. Thus, buckets in the past can be
 *      cleared in bulk based on how many we should have advanced since
 *      the last time we updated.
 *
 *  Portability Issues:
 *      None.
 */

#pragma once

#include <pthread.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace quicr {

#ifdef ESP_PLATFORM
#define SET_THREAD_STACKSIZE(stack_size)                                                                               \
    pthread_attr_t attr;                                                                                               \
    pthread_attr_init(&attr);                                                                                          \
    pthread_attr_setstacksize(&attr, stack_size)
#else
#define SET_THREAD_STACKSIZE(stack_size)
#endif

    /**
     * Interface for services that calculate ticks.
     */
    struct TickService
    {
        using TickType = size_t;
        using DurationType = std::chrono::microseconds;

        virtual TickType Milliseconds() const = 0;
        virtual TickType Microseconds() const = 0;
        virtual ~TickService() = default;
    };

    /**
     * @brief Calculates elapsed time in ticks.
     *
     * @details Calculates time that's elapsed between update calls. Keeps
     *          track of time using ticks as a counter of elapsed time. The
     *          precision 500us or greater, which results in the tick interval
     *          being >= 500us.
     */
    class ThreadedTickService : public TickService
    {
        using ClockType = std::chrono::steady_clock;

      public:
        ThreadedTickService(std::uint64_t sleep_delay_us = 333)
          : sleep_delay_us_{ sleep_delay_us }
        {
            SET_THREAD_STACKSIZE(1024);
            tick_thread_ = std::thread(&ThreadedTickService::TickLoop, this);
        }

        ThreadedTickService(const ThreadedTickService& other)
          : ticks_{ other.ticks_ }
          , sleep_delay_us_{ other.sleep_delay_us_ }
          , stop_{ other.stop_.load() }
        {
            SET_THREAD_STACKSIZE(1024);
            tick_thread_ = std::thread(&ThreadedTickService::TickLoop, this);
        }

        ~ThreadedTickService() override
        {
            stop_ = true;
            if (tick_thread_.joinable())
                tick_thread_.join();
        }

        ThreadedTickService& operator=(const ThreadedTickService& other)
        {
            ticks_ = other.ticks_;
            sleep_delay_us_ = other.sleep_delay_us_;
            stop_ = other.stop_.load();

            SET_THREAD_STACKSIZE(1024);
            tick_thread_ = std::thread(&ThreadedTickService::TickLoop, this);
            return *this;
        }

        TickType Microseconds() const override { return ticks_; }

        TickType Milliseconds() const override { return ticks_ / 1000; }

      private:
        void TickLoop()
        {
            auto prev_time = ClockType::now();

            while (!stop_) {
                const auto& now = ClockType::now();
                const uint64_t delta = std::chrono::duration_cast<std::chrono::microseconds>(now - prev_time).count();
                std::this_thread::sleep_for(std::chrono::microseconds(sleep_delay_us_));

                if (delta >= sleep_delay_us_) {
                    ticks_ += delta;
                    prev_time = now;
                }
            }
        }

      private:
        /// The current ticks since the tick_service began.
        uint64_t ticks_{ 0 };

        /// Sleep delay in microseconds
        uint64_t sleep_delay_us_;

        /// Flag to stop tick_service thread.
        std::atomic<bool> stop_{ false };

        /// The thread to update ticks on.
        std::thread tick_thread_;
    };

}; // namespace quicr
