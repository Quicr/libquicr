// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "time_queue.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <forward_list>
#include <map>
#include <numeric>
#include <optional>

namespace quicr {

    template<typename T, typename Duration_t>
    class GroupTimeQueue
    {
        using TickType = TickService::TickType;
        using IndexType = std::uint64_t;
        using GroupIdType = std::uint64_t;

        using BucketType = std::vector<GroupIdType>;
        using ValueType = std::vector<T>;

        struct QueueValueType
        {
            GroupIdType group_id;
            TickType expiry_tick;
            TickType wait_for_tick;
            ValueType objects;

            auto operator<=>(const QueueValueType& other) const { return group_id <=> other.group_id; }
        };

        using QueueType = std::map<GroupIdType, QueueValueType>;

      public:
        GroupTimeQueue(size_t duration, size_t interval, std::shared_ptr<TickService> tick_service)
          : duration_{ duration }
          , interval_{ interval }
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
        }

        void Clear() noexcept
        {
            if (queue_.empty())
                return;

            queue_.clear();

            for (auto& bucket : buckets_) {
                bucket.clear();
            }

            queue_index_ = bucket_index_ = object_index_ = size_ = 0;
        }

        /**
         * @brief Pop (increment) front
         *
         * @details This method should be called after front when the object is processed. This
         *      will move the queue forward. If at the end of the queue, it'll be cleared and reset.
         */
        void Pop() noexcept
        {
            if (queue_.empty())
                return;

            const auto& [_, group] = *std::next(queue_.begin(), queue_index_);

            if (++object_index_ < group.objects.size()) {
                --size_;
                return;
            }

            size_ -= object_index_;
            object_index_ = 0;

            if (queue_.empty() || ++queue_index_ < queue_.size())
                return;

            Clear();
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
                auto& [group_id, item] = *std::next(queue_.begin(), queue_index_);
                auto& [_, expiry_tick, pop_wait_ttl, objects] = item;

                if (ticks > expiry_tick) {
                    elem.expired_count += object_index_;
                    queue_index_++;
                    size_ -= object_index_;
                    object_index_ = 0;
                    continue;
                }

                if (pop_wait_ttl > ticks) {
                    return;
                }

                elem.has_value = true;
                elem.value = objects.at(object_index_);
                return;
            }

            Clear();
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

            return std::move(obj);
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

        void Push(std::uint64_t group_id, const T& value, size_t ttl, size_t delay_ttl = 0)
        {
            InternalPush(group_id, value, ttl, delay_ttl);
        }

        void Push(std::uint64_t group_id, T&& value, size_t ttl, size_t delay_ttl = 0)
        {
            InternalPush(group_id, std::move(value), ttl, delay_ttl);
        }

        size_t Size() const noexcept { return size_; }

        bool Empty() const noexcept { return queue_.empty(); }

      protected:
        inline TickType Advance()
        {
            const TickType new_ticks = tick_service_->Milliseconds();
            const TickType delta = current_ticks_ ? (new_ticks - current_ticks_) / interval_ : 0;
            current_ticks_ = new_ticks;

            if (delta == 0) {
                return current_ticks_;
            }

            if (delta >= static_cast<TickType>(total_buckets_)) {
                Clear();
                return current_ticks_;
            }

            for (TickType i = 0; i < delta; ++i) {
                auto& bucket = buckets_[(bucket_index_ + i) % total_buckets_];
                for (const auto& key : bucket) {
                    queue_.erase(key);
                }
                bucket.clear();
            }

            bucket_index_ = (bucket_index_ + delta) % total_buckets_;

            return current_ticks_;
        }

        template<typename Value>
        inline void InternalPush(const GroupIdType& key, Value value, size_t ttl, size_t delay_ttl)
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

            BucketType& bucket = buckets_[future_index];
            bucket.push_back(key);

            auto& group = queue_[key];
            group.group_id = key;
            group.expiry_tick = expiry_tick;
            group.wait_for_tick = ticks + delay_ttl;
            group.objects.emplace_back(value);

            ++size_;
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

        /// The index into the current group.
        IndexType object_index_{ 0 };

        /// Last calculated tick value.
        TickType current_ticks_{ 0 };

        /// TOtal count of objects from all groups.
        std::size_t size_{ 0 };

        /// The memory storage for all keys to be managed.
        std::vector<BucketType> buckets_;

        /// The cache of elements being stored.
        QueueType queue_;

        /// Tick service for calculating new tick and jumps in time.
        std::shared_ptr<TickService> tick_service_;
    };

    /**
     * @brief Priority queue that uses time_queue for each priority
     *
     * @details Order is maintained for objects pushed by priority.
     *          During each `front()`/`pop()` the queue will always
     *          pop the lower priority objects first. Lower priority
     *          objects will be serviced first in the order they were
     *          added to the queue.
     *
     * @tparam DataType   The element type to be stored.
     * @tparam PMAX       Max priorities to allow - Range becomes 0 - PMAX
     */
    template<typename DataType, uint8_t PMAX = 32>
    class PriorityQueue
    {
        using TimeType = std::chrono::milliseconds;
        using TimeQueueType = GroupTimeQueue<DataType, TimeType>;

        struct Exception : public std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        struct InvalidPriorityException : public Exception
        {
            using Exception::Exception;
        };

      public:
        ~PriorityQueue() {}

        /**
         * Construct a priority queue
         * @param tick_service Shared pointer to tick_service service
         */
        PriorityQueue(const std::shared_ptr<TickService>& tick_service)
          : PriorityQueue(1000, 1, tick_service, 1000)
        {
        }

        /**
         * Construct a priority queue
         *
         * @param duration              Max duration of time for the queue
         * @param interval              Interval per bucket, Default is 1
         * @param tick_service          Shared pointer to tick_service service
         * @param initial_queue_size    Number of default fifo queue size (reserve)
         */
        PriorityQueue(size_t duration,
                      size_t interval,
                      const std::shared_ptr<TickService>& tick_service,
                      size_t initial_queue_size)
          : tick_service_(tick_service)
        {

            if (tick_service == nullptr) {
                throw std::invalid_argument("Tick service cannot be null");
            }

            initial_queue_size_ = initial_queue_size;
            duration_ms_ = duration;
            interval_ms_ = interval;
        }

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param value     The value to push onto the queue.
         * @param ttl       The time to live of the value in milliseconds.
         * @param priority  The priority of the value (range is 0 - PMAX)
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         */
        void Push(std::uint64_t group_id, DataType& value, uint32_t ttl, uint8_t priority = 0, uint32_t delay_ttl = 0)
        {
            std::lock_guard<std::mutex> _(mutex_);

            auto& queue = GetQueueByPriority(priority);
            queue->Push(group_id, value, ttl, delay_ttl);
        }

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param value     The value to push onto the queue.
         * @param ttl       The time to live of the value in milliseconds.
         * @param priority  The priority of the value (range is 0 - PMAX)
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         */
        void Push(std::uint64_t group_id, DataType&& value, uint32_t ttl, uint8_t priority = 0, uint32_t delay_ttl = 0)
        {
            std::lock_guard<std::mutex> _(mutex_);

            auto& queue = GetQueueByPriority(priority);
            queue->Push(group_id, std::move(value), ttl, delay_ttl);
        }

        /**
         * @brief Get the first object from queue
         *
         * @param elem[out]          Time queue element storage. Will be updated.
         *
         * @return TimeQueueElement<DataType> value from time queue
         */
        void Front(TimeQueueElement<DataType>& elem)
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& tqueue : queue_) {
                if (!tqueue || tqueue->Empty())
                    continue;

                tqueue->Front(elem);
            }
        }

        /**
         * @brief Get and remove the first object from queue
         *
         * @param elem[out]          Time queue element storage. Will be updated.
         *
         * @return TimeQueueElement<DataType> from time queue
         */
        void PopFront(TimeQueueElement<DataType>& elem)
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& tqueue : queue_) {
                if (!tqueue || tqueue->Empty())
                    continue;

                tqueue->PopFront(elem);
            }
        }

        /**
         * @brief Pop/remove the first object from queue
         */
        void Pop()
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& tqueue : queue_) {
                if (tqueue && !tqueue->Empty())
                    return tqueue->Pop();
            }
        }

        /**
         * @brief Clear queue
         */
        void Clear()
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& tqueue : queue_) {
                if (tqueue)
                    tqueue->Clear();
            }
        }

        // TODO: Consider changing empty/size to look at timeQueue sizes - maybe support blocking pops
        size_t Size() const
        {
            return std::accumulate(queue_.begin(), queue_.end(), 0, [](auto sum, auto& tqueue) {
                return tqueue ? sum + tqueue->Size() : sum;
            });
        }

        bool Empty() const
        {
            for (auto& tqueue : queue_) {
                if (tqueue && !tqueue->Empty()) {
                    return false;
                }
            }

            return true;
        }

      private:
        /**
         * @brief Get queue by priority
         *
         * @param priority  The priority queue value (range is 0 - PMAX)
         *
         * @return Unique pointer to queue for the given priority
         */
        std::unique_ptr<TimeQueueType>& GetQueueByPriority(const uint8_t priority)
        {
            if (priority >= PMAX) {
                throw InvalidPriorityException("Priority not within range");
            }

            if (!queue_[priority]) {
                queue_[priority] = std::make_unique<TimeQueueType>(duration_ms_, interval_ms_, tick_service_);
            }

            return queue_[priority];
        }

        std::mutex mutex_;
        size_t initial_queue_size_;
        size_t duration_ms_;
        size_t interval_ms_;

        std::array<std::unique_ptr<TimeQueueType>, PMAX> queue_;
        std::shared_ptr<TickService> tick_service_;
    };
}; // end of namespace quicr
