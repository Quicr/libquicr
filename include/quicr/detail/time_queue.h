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

#include <atomic>
#include <chrono>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include "tick_service.h"

namespace quicr {

    template<typename T>
    struct TimeQueueElement
    {
        bool has_value{ false };     /// Indicates if value was set/returned in front access
        uint32_t expired_count{ 0 }; /// Number of items expired before on this front access
        T value;                     /// Value of front object
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
    template<typename T, typename Duration_t, typename BucketType = std::vector<T>>
    class TimeQueue
    {
        static constexpr uint32_t kMaxBuckets = 1000; /// Maximum number of buckets allowed

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

      protected:
        /*=======================================================================*/
        // Internal type definitions
        /*=======================================================================*/

        using TickType = TickService::TickType;
        using IndexType = std::uint64_t;

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
        TimeQueue(size_t duration, size_t interval, std::shared_ptr<TickService> tick_service)
          : duration_{ duration }
          , interval_{ (duration / interval > kMaxBuckets ? duration / kMaxBuckets : interval) }
          , total_buckets_{ duration_ / interval_ }
          , tick_service_(std::move(tick_service))
        {
            if (duration == 0 || duration % interval != 0 || duration == interval) {
                throw std::invalid_argument("Invalid time_queue constructor args");
            }

            if (!tick_service_) {
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
                  std::shared_ptr<TickService> tick_service,
                  size_t initial_queue_size)
          : TimeQueue(duration, interval, std::move(tick_service))
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

            // Clear();
        }

        /**
         * @brief Pops (removes) the front of the queue.
         *
         * @returns TimeQueueElement of the popped value
         */
        [[nodiscard]] TimeQueueElement<T> PopFront()
        {
            TimeQueueElement<T> obj{};
            Front(obj);
            if (obj.has_value) {
                Pop();
            }

            return obj;
        }

        /**
         * @brief Pops (removes) the front of the queue using provided storage
         *
         * @param elem[out]          Time queue element storage. Will be updated.
         */
        void PopFront(TimeQueueElement<T>& elem)
        {
            Front(elem);
            if (elem.has_value) {
                Pop();
            }
        }

        /**
         * @brief Returns the most valid front of the queue without popping.
         *
         * @param elem[out]          Time queue element storage. Will be updated.
         *
         * @returns Element of the front value
         */
        void Front(TimeQueueElement<T>& elem)
        {
            const TickType ticks = Advance();

            elem.has_value = false;
            elem.expired_count = 0;

            if (queue_.empty())
                return;

            while (queue_index_ < queue_.size()) {
                auto& [bucket, value_index, expiry_tick, pop_wait_ttl] = queue_.at(queue_index_);

                if (value_index >= bucket.size() || ticks > expiry_tick) {
                    elem.expired_count++;
                    queue_index_++;
                    continue;
                }

                if (pop_wait_ttl > ticks) {
                    return;
                }

                elem.has_value = true;
                elem.value = bucket.at(value_index);
                return;
            }

            // Clear();
        }

        size_t Size() const noexcept { return queue_.size() - queue_index_; }
        bool Empty() const noexcept { return queue_.empty() || queue_index_ >= queue_.size(); }

        /**
         * @brief Clear/reset the queue to no objects
         */
        void Clear() noexcept
        {
            if (queue_.empty())
                return;

            queue_.clear();

            for (const auto idx : bucket_inuse_indexes_) {
                buckets_[idx].clear();
            }

            bucket_inuse_indexes_.clear();

            queue_index_ = bucket_index_ = 0;
        }

        /**
         * @brief Clear range of buckets
         */
        void ClearRange(size_t start, size_t end) noexcept
        {
            auto in_use = bucket_inuse_indexes_;
            bucket_inuse_indexes_.clear();

            for (const auto idx : in_use) {
                if (idx >= start && idx <= end) {
                    buckets_[idx].clear();
                } else {
                    bucket_inuse_indexes_.insert(idx);
                }
            }
        }

      protected:
        /**
         * @brief Based on current time, adjust and move the bucket index with time
         *        (sliding window)
         *
         * @returns Current tick value at time of advance
         */
        TickType Advance()
        {
            const TickType new_ticks = tick_service_->Milliseconds();
            TickType delta = current_ticks_ ? new_ticks - current_ticks_ : 0;
            current_ticks_ = new_ticks;

            if (delta == 0)
                return current_ticks_;

            delta /= interval_; // relative delta based on interval

            if (delta >= static_cast<TickType>(total_buckets_)) {
                Clear();
                return current_ticks_;
            }

            if (bucket_index_ + delta > total_buckets_) {
                ClearRange(bucket_index_, total_buckets_);

                bucket_index_ = (bucket_index_ + delta) % total_buckets_;

                ClearRange(0, bucket_index_);
            } else {
                ClearRange(bucket_index_, bucket_index_ + delta);
                bucket_index_ = (bucket_index_ + delta) % total_buckets_;
            }

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
            }

            if (ttl == 0) {
                ttl = duration_;
            }

            auto relative_ttl = ttl / interval_;

            const TickType ticks = Advance();

            const TickType expiry_tick = ticks + ttl;

            const IndexType future_index = (bucket_index_ + relative_ttl - 1) % total_buckets_;

            bucket_inuse_indexes_.insert(future_index);

            BucketType& bucket = buckets_[future_index];

            bucket.emplace_back(value);
            queue_.emplace_back(bucket, bucket.size() - 1, expiry_tick, ticks + delay_ttl);
        }

      protected:
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

        /// Set of bucket indexes that are in uses.
        std::set<uint32_t> bucket_inuse_indexes_;

        /// The FIFO ordered queue of values as they were inserted.
        QueueType queue_;

        /// Tick service for calculating new tick and jumps in time.
        std::shared_ptr<TickService> tick_service_;
    };

}; // namespace quicr
