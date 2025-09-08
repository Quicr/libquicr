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
    class GroupTimeQueue : public TimeQueue<std::vector<T>, Duration_t, std::map<std::uint64_t, std::vector<T>>>
    {
        using Group_t = std::map<uint64_t, std::vector<T>>;
        using TickType = TickService::TickType;

        using base = TimeQueue<std::vector<T>, Duration_t, Group_t>;

      public:
        GroupTimeQueue(size_t duration, size_t interval, std::shared_ptr<TickService> tick_service)
          : base(duration, interval, std::move(tick_service))
        {
        }

        GroupTimeQueue(size_t duration,
                       size_t interval,
                       std::shared_ptr<TickService> tick_service,
                       size_t initial_queue_size)
          : base(duration, interval, std::move(tick_service), initial_queue_size)
        {
        }

        void Clear() noexcept
        {
            if (this->queue_.empty())
                return;

            this->queue_.clear();

            for (std::size_t i = 0; i < this->buckets_.size(); ++i) {
                this->buckets_[i].clear();
            }

            this->queue_index_ = this->bucket_index_ = object_index_ = 0;
        }

        /**
         * @brief Pop (increment) front
         *
         * @details This method should be called after front when the object is processed. This
         *      will move the queue forward. If at the end of the queue, it'll be cleared and reset.
         */
        void Pop() noexcept
        {
            if (this->queue_.empty())
                return;

            const auto& front = this->queue_.at(this->queue_index_);
            const auto& group = front.bucket.at(front.value_index);

            if (++object_index_ < group.size()) {
                --size_;
                return;
            }

            --size_;
            object_index_ = 0;

            if (this->queue_.empty() || ++this->queue_index_ < this->queue_.size())
                return;

            this->Clear();
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
            const TickType ticks = this->Advance();

            elem.has_value = false;
            elem.expired_count = 0;

            if (this->queue_.empty())
                return;

            while (this->queue_index_ < this->queue_.size()) {
                auto& [bucket, value_index, expiry_tick, pop_wait_ttl] = this->queue_.at(this->queue_index_);

                if (!bucket.contains(value_index) || ticks > expiry_tick) {
                    elem.expired_count++; // TODO: increment this to show the amount of objects that just expired.
                    this->queue_index_++;
                    object_index_ = 0;
                    continue;
                }

                if (pop_wait_ttl > ticks) {
                    return;
                }

                elem.has_value = true;
                elem.value = bucket.at(value_index).at(object_index_);
                return;
            }

            this->Clear();
        }

        /**
         * @brief Pops (removes) the front of the queue.
         *
         * @returns TimeQueueElement of the popped value
         */
        [[nodiscard]] TimeQueueElement<T> PopFront()
        {
            TimeQueueElement<T> obj{};
            this->Front(obj);
            if (obj.has_value) {
                this->Pop();
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
            this->Front(elem);
            if (elem.has_value) {
                this->Pop();
            }
        }

        void Push(std::uint64_t group_id, const T& value, size_t ttl, size_t delay_ttl = 0)
        {
            this->InternalPush(group_id, value, ttl, delay_ttl);
        }

        void Push(std::uint64_t group_id, T&& value, size_t ttl, size_t delay_ttl = 0)
        {
            this->InternalPush(group_id, std::move(value), ttl, delay_ttl);
        }

        std::size_t Size() const noexcept { return size_; }

      private:
        TickType Advance() override
        {
            const TickType new_ticks = this->tick_service_->Milliseconds();
            TickType delta = this->current_ticks_ ? new_ticks - this->current_ticks_ : 0;
            this->current_ticks_ = new_ticks;

            if (delta == 0)
                return this->current_ticks_;

            delta /= this->interval_; // relative delta based on interval

            if (delta >= static_cast<TickType>(this->total_buckets_)) {
                Clear();
                return this->current_ticks_;
            }

            this->bucket_index_ = (this->bucket_index_ + delta) % this->total_buckets_;
            if (!this->buckets_[this->bucket_index_].empty()) {
                size_ -= this->buckets_[this->bucket_index_].size();
                this->buckets_[this->bucket_index_].clear();
            }

            return this->current_ticks_;
        }

        template<typename Value>
        void InternalPush(std::uint64_t group_id, Value value, size_t ttl, size_t delay_ttl)
        {
            if (ttl > this->duration_) {
                throw std::invalid_argument("TTL is greater than max duration");
            }

            if (ttl == 0) {
                ttl = this->duration_;
            }

            auto relative_ttl = ttl / this->interval_;

            const TickType ticks = this->Advance();

            const TickType expiry_tick = ticks + ttl;

            auto found = std::find_if(
              this->queue_.begin(), this->queue_.end(), [&](const auto& v) { return v.value_index == group_id; });

            if (found != this->queue_.end()) {
                found->bucket.at(group_id).emplace_back(value);
                return;
            }

            const typename base::IndexType future_index =
              (this->bucket_index_ + relative_ttl - 1) % this->total_buckets_;

            auto& bucket = this->buckets_[future_index];
            bucket[group_id].emplace_back(value);
            this->queue_.emplace_back(bucket, group_id, expiry_tick, ticks + delay_ttl);
            ++size_;
        }

      private:
        std::size_t object_index_ = 0;
        std::size_t size_ = 0;
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
                queue_[priority] =
                  std::make_unique<TimeQueueType>(duration_ms_, interval_ms_, tick_service_, initial_queue_size_);
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
