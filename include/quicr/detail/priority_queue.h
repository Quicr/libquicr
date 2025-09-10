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
#include <set>
#include <unordered_map>

namespace quicr {
    template<typename T>
    class GroupTimeQueue
    {
        static constexpr uint32_t kMaxBuckets = 1000; /// Maximum number of buckets allowed

      protected:
        using TickType = TickService::TickType;
        using IndexType = std::uint64_t;
        using GroupIdType = std::uint64_t;

        using BucketType = std::vector<T>;

        struct GroupType
        {
            struct ObjectRefType
            {
                BucketType& bucket;
                IndexType value_index;
            };

            GroupType(IndexType offset)
              : offset(offset)
            {
            }

            IndexType offset;
            std::vector<ObjectRefType> objects;
        };

        struct QueueValueType
        {
            GroupType& group;
            TickType expiry_tick;
            TickType wait_for_tick;
        };

        using QueueType = std::vector<QueueValueType>;

      public:
        GroupTimeQueue(size_t duration,
                       size_t interval,
                       std::shared_ptr<TickService> tick_service,
                       std::size_t initial_queue_size,
                       std::size_t group_size)
          : duration_{ duration }
          , interval_{ (duration / interval > kMaxBuckets ? duration / kMaxBuckets : interval) }
          , total_buckets_{ duration_ / interval_ }
          , group_size_{ group_size }
          , tick_service_(std::move(tick_service))
        {
            if (duration == 0 || duration % interval != 0 || duration == interval) {
                throw std::invalid_argument("Invalid time_queue constructor args");
            }

            if (!tick_service_) {
                throw std::invalid_argument("Tick service cannot be null");
            }

            buckets_.resize(total_buckets_);
            queue_.reserve(initial_queue_size);
        }

        GroupTimeQueue() = delete;
        GroupTimeQueue(const GroupTimeQueue&) = default;
        GroupTimeQueue(GroupTimeQueue&&) noexcept = default;

        GroupTimeQueue& operator=(const GroupTimeQueue&) = default;
        GroupTimeQueue& operator=(GroupTimeQueue&&) noexcept = default;

        /**
         * @brief Clear/reset the queue to no objects
         */
        void Clear() noexcept
        {
            if (queue_.empty())
                return;

            queue_.clear();
            groups_.clear();

            for (std::size_t i = 0; i < this->buckets_.size(); ++i) {
                if (!buckets_[i].empty())
                    buckets_[i].clear();
            }

            queue_index_ = bucket_index_ = 0;
        }

        void Push(GroupIdType group_id, const T& value, size_t ttl, size_t delay_ttl = 0)
        {
            InternalPush(group_id, value, ttl, delay_ttl);
        }

        void Push(GroupIdType group_id, T&& value, size_t ttl, size_t delay_ttl = 0)
        {
            InternalPush(group_id, std::move(value), ttl, delay_ttl);
        }

        void Pop() noexcept
        {
            if (queue_.empty())
                return;

            auto& group = queue_[queue_index_].group;

            if (++group.offset < group.objects.size())
                return;

            if (++queue_index_ < queue_.size())
                return;

            Clear();
        }

        [[nodiscard]] TimeQueueElement<T> PopFront()
        {
            TimeQueueElement<T> obj{};
            Front(obj);
            if (obj.has_value) {
                Pop();
            }

            return obj;
        }

        void PopFront(TimeQueueElement<T>& elem)
        {
            Front(elem);
            if (elem.has_value) {
                Pop();
            }
        }

        void Front(TimeQueueElement<T>& elem)
        {
            const TickType ticks = Advance();

            elem.has_value = false;
            elem.expired_count = 0;

            if (queue_.empty())
                return;

            while (queue_index_ < queue_.size()) {
                auto& [group, expiry_tick, pop_wait_ttl] = queue_[queue_index_];
                auto& [offset, objects] = group;

                if (offset >= objects.size() || ticks > expiry_tick) {
                    offset = 0;
                    objects.clear();
                    ++elem.expired_count;
                    ++queue_index_;
                    continue;
                }

                auto& [bucket, value_index] = objects[offset];

                if (value_index >= bucket.size()) {
                    ++elem.expired_count;
                    ++queue_index_;
                    continue;
                }

                if (pop_wait_ttl > ticks) {
                    return;
                }

                elem.has_value = true;
                elem.value = bucket[value_index];
                return;
            }

            Clear();
        }

        constexpr size_t Size() const noexcept { return queue_.size() - queue_index_; }

        constexpr bool Empty() const noexcept { return queue_.empty() || queue_index_ >= queue_.size(); }

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

            bucket_index_ = (bucket_index_ + delta) % total_buckets_;
            if (!buckets_[bucket_index_].empty())
                buckets_[bucket_index_].clear();

            return current_ticks_;
        }

        template<typename Value>
        inline void InternalPush(GroupIdType group_id, Value value, size_t ttl, size_t delay_ttl)
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

            bucket.push_back(value);

            auto [group, is_new] = groups_.try_emplace(group_id, 0);
            if (is_new) {
                group->second.objects.reserve(group_size_);
            }

            group->second.objects.emplace_back(bucket, bucket.size() - 1);

            queue_.emplace_back(group->second, expiry_tick, ticks + delay_ttl);
        }

      protected:
        /// The duration in ticks of the entire queue.
        const std::size_t duration_;

        /// The interval at which buckets are cleared in ticks.
        const std::size_t interval_;

        /// The total amount of buckets. Value is calculated by duration / interval.
        const std::size_t total_buckets_;

        ///
        const std::size_t group_size_;

        /// The index in time of the current bucket.
        IndexType bucket_index_{ 0 };

        /// The index of the first valid item in the queue.
        IndexType queue_index_{ 0 };

        /// Last calculated tick value.
        TickType current_ticks_{ 0 };

        /// The memory storage for all elements to be managed.
        std::vector<BucketType> buckets_;

        ///
        std::unordered_map<GroupIdType, GroupType> groups_;

        /// The FIFO ordered queue of values as they were inserted.
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
        using TimeQueueType = GroupTimeQueue<DataType>;

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
          : PriorityQueue(1000, 1, tick_service, 1000, 150)
        {
        }

        /**
         * Construct a priority queue
         *
         * @param duration              Max duration of time for the queue
         * @param interval              Interval per bucket, Default is 1
         * @param tick_service          Shared pointer to tick_service service
         * @param initial_queue_size    Number of default fifo queue size (reserve)
         * @param group_size            The expected size of a group.
         */
        PriorityQueue(size_t duration,
                      size_t interval,
                      const std::shared_ptr<TickService>& tick_service,
                      size_t initial_queue_size,
                      size_t group_size)
          : tick_service_(tick_service)
        {

            if (tick_service == nullptr) {
                throw std::invalid_argument("Tick service cannot be null");
            }

            initial_queue_size_ = initial_queue_size;
            group_size_ = group_size;
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
            queue.Push(group_id, value, ttl, delay_ttl);
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
            queue.Push(group_id, std::move(value), ttl, delay_ttl);
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

            for (auto& [_, tqueue] : queue_) {
                if (tqueue.Empty())
                    continue;

                tqueue.Front(elem);
                if (elem.has_value) {
                    return;
                }
            }
        }

        /**
         * @brief Get and remove the first object from queue
         *
         * @param elem[out]          Time queue element storage. Will be updated.
         */
        void PopFront(TimeQueueElement<DataType>& elem)
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& [_, tqueue] : queue_) {
                if (tqueue.Empty())
                    continue;

                tqueue.PopFront(elem);
                if (elem.has_value)
                    return;
            }
        }

        /**
         * @brief Pop/remove the first object from queue
         */
        void Pop()
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& [_, tqueue] : queue_) {
                if (!tqueue.Empty())
                    return tqueue.Pop();
            }
        }

        /**
         * @brief Clear queue
         */
        void Clear()
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& [_, tqueue] : queue_) {
                tqueue.Clear();
            }
        }

        // TODO: Consider changing empty/size to look at timeQueue sizes - maybe support blocking pops
        size_t Size() const
        {
            return std::accumulate(
              queue_.begin(), queue_.end(), 0, [](auto sum, auto& tqueue) { return sum + tqueue.second.Size(); });
        }

        bool Empty() const
        {
            for (auto& [_, tqueue] : queue_) {
                if (!tqueue.Empty()) {
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
        TimeQueueType& GetQueueByPriority(const uint8_t priority)
        {
            if (priority >= PMAX) {
                throw InvalidPriorityException("Priority not within range");
            }

            auto [it, _] =
              queue_.try_emplace(priority, duration_ms_, interval_ms_, tick_service_, initial_queue_size_, group_size_);

            return it->second;
        }

        std::mutex mutex_;
        size_t initial_queue_size_;
        size_t group_size_;
        size_t duration_ms_;
        size_t interval_ms_;

        std::map<uint8_t, TimeQueueType> queue_;
        std::shared_ptr<TickService> tick_service_;
    };
}; // end of namespace quicr
