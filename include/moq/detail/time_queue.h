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

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <sys/select.h>
#include <thread>
#include <type_traits>
#include <vector>

namespace qtransport {

    /**
     * Interface for services that calculate ticks.
     */
    struct TickService
    {
        using TickType = size_t;
        using DurationType = std::chrono::microseconds;

        virtual TickType GetTicks(const DurationType& interval) const = 0;
    };

    template<typename T>
    struct TimeQueueElement
    {
        bool has_value{ false };     /// Indicates if value was set/returned in front access
        uint32_t expired_count{ 0 }; /// Number of items expired before on this front access
        T value;                     /// Value of front object
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
        ThreadedTickService() { tick_thread_ = std::thread(&ThreadedTickService::TickLoop, this); }

        ThreadedTickService(const ThreadedTickService& other)
          : ticks_{ other.ticks_ }
          , stop_{ other.stop_.load() }
        {
            tick_thread_ = std::thread(&ThreadedTickService::TickLoop, this);
        }

        virtual ~ThreadedTickService()
        {
            stop_ = true;
            if (tick_thread_.joinable())
                tick_thread_.join();
        }

        ThreadedTickService& operator=(const ThreadedTickService& other)
        {
            ticks_ = other.ticks_;
            stop_ = other.stop_.load();
            tick_thread_ = std::thread(&ThreadedTickService::TickLoop, this);
            return *this;
        }

        TickType GetTicks(const DurationType& interval) const override
        {
            const TickType increment = std::max(interval, interval_) / interval_;
            return ticks_ / increment;
        }

      private:
        void TickLoop()
        {
            const int interval_us = interval_.count();

            timeval sleep_time = { 0, interval_us };
            while (!stop_) {
                select(0, NULL, NULL, NULL, &sleep_time);
                sleep_time.tv_usec = interval_us;
                ++ticks_;
            }
        }

      private:
        /// The current ticks since the tick_service began.
        uint64_t ticks_{ 0 };

        /// Flag to stop tick_service thread.
        std::atomic<bool> stop_{ false };

        /// The interval at which ticks should increase.
        const DurationType interval_{ 500 };

        /// The thread to update ticks on.
        std::thread tick_thread_;
    };

    /**
     * @brief Aging element FIFO queue.
     *
     * @details Time based queue that maintains the push/pop order, but expires older values given a specific ttl.
     *
     * @tparam T            The element type to be stored.
     * @tparam Duration_t   The duration type to check for. All the variables that are interval, duration, ttl, ...
     *                      are of this unit. Ticks are of this unit. For example, setting to millisecond will define
     *                      the unit for ticks and all associated variables to be millisecond.
     */
    template<typename T, typename Duration_t>
    class TimeQueue
    {
        /*=======================================================================*/
        // Time queue type assertions
        /*=======================================================================*/

        template<typename>
        struct IsChronoDuration : std::false_type
        {};

        template<typename Rep, typename Period>
        struct IsChronoDuration<std::chrono::duration<Rep, Period>> : std::true_type
        {};

        static_assert(IsChronoDuration<Duration_t>::value);

        /*=======================================================================*/
        // Internal type definitions
        /*=======================================================================*/

        using TickType = TickService::TickType;
        using BucketType = std::vector<T>;
        using IndexType = std::uint32_t;

        struct QueueValueType
        {
            QueueValueType(BucketType& bucket, IndexType value_index, TickType expiry_tick, TickType wait_for_tick)
              : bucket{ bucket }
              , value_index{ value_index }
              , expiry_tick(expiry_tick)
              , wait_for_tick(wait_for_tick)
            {
            }

            BucketType& bucket;
            IndexType value_index;
            TickType expiry_tick;
            TickType wait_for_tick;
        };

        using QueueType = std::vector<QueueValueType>;

      public:
        /**
         * @brief Construct a time_queue with defaults or supplied parameters
         *
         * @param duration      Duration of the queue in Duration_t. Value must be > 0, and != interval.
         * @param interval      Interval of ticks in Duration_t. Must be > 0, < duration, duration % interval == 0.
         * @param tick_service  Shared pointer to tick_service service.
         *
         * @throws std::invalid_argument    If the duration or interval do not meet requirements or If the tick_service
         * is null.
         */
        TimeQueue(size_t duration, size_t interval, const std::shared_ptr<TickService>& tick_service)
          : duration_{ duration }
          , interval_{ interval }
          , total_buckets_{ duration_ / interval_ }
          , tick_service_(tick_service)
        {
            if (duration == 0 || duration % interval != 0 || duration == interval) {
                throw std::invalid_argument("Invalid time_queue constructor args");
            }

            if (!tick_service) {
                throw std::invalid_argument("Tick service cannot be null");
            }

            buckets_.resize(total_buckets_);
            queue_.reserve(total_buckets_);
        }

        /**
         * @brief Construct a time_queue with defaults or supplied parameters
         *
         * @param duration              Duration of the queue in Duration_t. Value must be > 0, and != interval.
         * @param interval              Interval of ticks in Duration_t. Value must be > 0, < duration, duration %
         *                              interval == 0.
         * @param tick_service          Shared pointer to tick_service.
         * @param initial_queue_size    Initial size of the queue to reserve.
         *
         * @throws std::invalid_argument If the duration or interval do not meet requirements or the tick_service is
         * null.
         */
        TimeQueue(size_t duration,
                  size_t interval,
                  const std::shared_ptr<TickService>& tick_service,
                  size_t initial_queue_size)
          : TimeQueue(duration, interval, tick_service)
        {
            queue_.reserve(initial_queue_size);
        }

        TimeQueue() = delete;
        TimeQueue(const TimeQueue&) = default;
        TimeQueue(TimeQueue&&) noexcept = default;

        TimeQueue& operator=(const TimeQueue&) = default;
        TimeQueue& operator=(TimeQueue&&) noexcept = default;

        /**
         * @brief Pushes a new value onto the queue with a time-to-live.
         *
         * @param value         The value to push onto the queue.
         * @param ttl           Time to live for an object using the unit of Duration_t
         * @param delay_ttl     Pop wait Time to live for an object using the unit of Duration_t
         *                      This will cause pop to be delayed by this TTL value
         *
         * @throws std::invalid_argument If ttl is greater than duration.
         */
        void Push(const T& value, size_t ttl, size_t delay_ttl = 0) { InternalPush(value, ttl, delay_ttl); }

        /**
         * @brief Pushes a new value onto the queue with a time-to-live.
         *
         * @param value      The value to push onto the queue.
         * @param ttl        Time to live for an object using the unit of Duration_t
         * @param delay_ttl  Pop wait Time to live for an object using the unit of Duration_t
         *                   This will cause pop to be delayed by this TTL value
         *
         * @throws std::invalid_argument If ttl is greater than duration.
         */
        void Push(T&& value, size_t ttl, size_t delay_ttl = 0) { InternalPush(std::move(value), ttl, delay_ttl); }

        /**
         * @brief Pop (increment) front
         *
         * @details This method should be called after front when the object is processed. This
         *      will move the queue forward. If at the end of the queue, it'll be cleared and reset.
         */
        void Pop() noexcept
        {
            if (queue_.empty() || ++queue_index_ < queue_.size())
                return;

            Clear();
        }

        /**
         * @brief Pops (removes) the front of the queue.
         *
         * @returns TimeQueueElement of the popped value
         */
        [[nodiscard]] TimeQueueElement<T> PopFront()
        {
            auto obj = std::move(Front());
            if (obj.has_value) {
                Pop();
            }

            return obj;
        }

        /**
         * @brief Returns the most valid front of the queue without popping.
         * @returns Element of the front value
         */
        [[nodiscard]] TimeQueueElement<T> Front()
        {
            const TickType ticks = Advance();
            TimeQueueElement<T> elem;

            if (queue_.empty())
                return elem;

            while (queue_index_ < queue_.size()) {
                auto& [bucket, value_index, expiry_tick, pop_wait_ttl] = queue_.at(queue_index_);

                if (value_index >= bucket.size() || ticks > expiry_tick) {
                    elem.expired_count++;
                    queue_index_++;
                    continue;
                }

                if (pop_wait_ttl > ticks) {
                    return elem;
                }

                elem.has_value = true;
                elem.value = bucket.at(value_index);
                return elem;
            }

            Clear();

            return elem;
        }

        size_t Size() const noexcept { return queue_.size() - queue_index_; }
        bool Empty() const noexcept { return queue_.empty() || queue_index_ >= queue_.size(); }

        /**
         * @brief Clear/reset the queue to no objects
         */
        void Clear() noexcept
        {
            queue_.clear();
            queue_index_ = bucket_index_ = 0;

            for (auto& bucket : buckets_) {
                bucket.clear();
            }
        }

      private:
        /**
         * @brief Based on current time, adjust and move the bucket index with time
         *        (sliding window)
         *
         * @returns Current tick value at time of advance
         */
        TickType Advance()
        {
            const TickType new_ticks = tick_service_->GetTicks(Duration_t(interval_));
            const TickType delta = current_ticks_ ? new_ticks - current_ticks_ : 0;
            current_ticks_ = new_ticks;

            if (delta == 0)
                return current_ticks_;

            if (delta >= static_cast<TickType>(total_buckets_)) {
                Clear();
                return current_ticks_;
            }

            for (TickType i = 0; i < delta; ++i) {
                buckets_[(bucket_index_ + i) % total_buckets_].clear();
            }

            bucket_index_ = (bucket_index_ + delta) % total_buckets_;

            return current_ticks_;
        }

        /**
         * @brief Pushes new element onto the queue and adds it to future bucket.
         *
         * @details Internal definition of push. Pushes value into specified
         *          bucket, and then emplaces the location info into the queue.
         *
         * @param value         The value to push onto the queue.
         * @param ttl           Time to live for an object using the unit of Duration_t
         * @param delay_ttl     Pop wait Time to live for an object using the unit of Duration_t
         *                      This will cause pop to be delayed by this TTL value
         *
         * @throws std::invalid_argument If ttl is greater than duration.
         */
        template<typename Value>
        inline void InternalPush(Value value, size_t ttl, size_t delay_ttl)
        {
            if (ttl > duration_) {
                throw std::invalid_argument("TTL is greater than max duration");
            } else if (ttl == 0) {
                ttl = duration_;
            }

            ttl = ttl / interval_;

            const TickType ticks = Advance();

            const TickType expiry_tick = ticks + ttl;

            const IndexType future_index = (bucket_index_ + ttl - 1) % total_buckets_;

            BucketType& bucket = buckets_[future_index];

            bucket.push_back(value);
            queue_.emplace_back(bucket, bucket.size() - 1, expiry_tick, ticks + delay_ttl);
        }

      private:
        /// The duration in ticks of the entire queue.
        const size_t duration_;

        /// The interval at which buckets are cleared in ticks.
        const size_t interval_;

        /// The total amount of buckets. Value is calculated by duration / interval.
        const size_t total_buckets_;

        /// The index in time of the current bucket.
        IndexType bucket_index_{ 0 };

        /// The index of the first valid item in the queue.
        IndexType queue_index_{ 0 };

        /// Last calculated tick value.
        TickType current_ticks_{ 0 };

        /// The memory storage for all elements to be managed.
        std::vector<BucketType> buckets_;

        /// The FIFO ordered queue of values as they were inserted.
        QueueType queue_;

        /// Tick service for calculating new tick and jumps in time.
        std::shared_ptr<TickService> tick_service_;
    };

}; // namespace qtransport
