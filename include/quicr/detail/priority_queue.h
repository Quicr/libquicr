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
    class GroupTimeQueue : public TimeQueue<std::map<std::uint64_t, std::vector<T>>, Duration_t>
    {
        using Group_t = std::map<uint64_t, std::vector<T>>;
        using TickType = TickService::TickType;

        using base = TimeQueue<Group_t, Duration_t>;

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
            Front(elem);
            if (elem.has_value) {
                this->Pop();
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
            const TickType ticks = this->Advance();

            elem.has_value = false;
            elem.expired_count = 0;

            if (this->queue_.empty())
                return;

            while (this->queue_index_ < this->queue_.size()) {
                auto& [bucket, value_index, expiry_tick, pop_wait_ttl] = this->queue_.at(this->queue_index_);

                if (value_index >= bucket.size() || ticks > expiry_tick) {
                    elem.expired_count++;
                    this->queue_index_++;
                    continue;
                }

                if (pop_wait_ttl > ticks) {
                    return;
                }

                elem.has_value = true;
                elem.value = bucket.at(value_index).begin()->second.front();
                return;
            }

            this->Clear();
        }

        void Push(std::uint64_t group_id, const T& value, size_t ttl, size_t delay_ttl = 0)
        {
            InternalPush(group_id, value, ttl, delay_ttl);
        }

        void Push(std::uint64_t group_id, T&& value, size_t ttl, size_t delay_ttl = 0)
        {
            InternalPush(group_id, std::move(value), ttl, delay_ttl);
        }

        std::size_t Size() const noexcept
        {
            std::size_t full_size = 0;

            for (auto it = std::next(this->buckets_.begin(), this->queue_index_); it != this->buckets_.end(); ++it) {
                const auto& bucket = *it;
                for (const auto& groups : bucket) {
                    for (const auto& [_, objects] : groups) {
                        full_size += objects.size();
                    }
                }
            }

            return full_size;
        }

      private:
        template<typename Value>
        void InternalPush(std::uint64_t group_id, Value value, size_t ttl, size_t delay_ttl)
        {
            std::optional<std::reference_wrapper<Group_t>> current_group;
            for (auto&& bucket : this->buckets_) {
                for (auto&& group : bucket) {
                    if (!group.contains(group_id)) {
                        continue;
                    }
                    current_group = group;
                    break;
                }

                if (current_group.has_value()) {
                    break;
                }
            }

            if (!current_group.has_value()) {
                return base::Push({ { group_id, { value } } }, ttl, delay_ttl);
            }

            Group_t& g = current_group.value();
            g[group_id].emplace_back(value);
        }
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
