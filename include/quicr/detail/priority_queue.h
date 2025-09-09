// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "time_queue.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <numeric>
#include <queue>

namespace quicr {

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
        using TimeQueueType = TimeQueue<DataType, TimeType>;

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
            std::lock_guard _(mutex_);

            auto& queue = GetQueueByPriorityGroupId(priority, group_id);
            queue.Push(value, ttl, delay_ttl);
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
            std::lock_guard _(mutex_);

            auto& queue = GetQueueByPriorityGroupId(priority, group_id);
            queue.Push(std::move(value), ttl, delay_ttl);
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
            std::vector<uint64_t> remove_groups_ids;
            remove_groups_ids.reserve(10);

            std::lock_guard _(mutex_);

            for (auto& [_, queue] : queues_) {
                remove_groups_ids.clear();
                for (auto& [group_id, tqueue] : queue) {
                    if (tqueue.Empty()) {
                        remove_groups_ids.push_back(group_id);
                        continue;
                    }

                    tqueue.Front(elem);
                    if (elem.has_value || elem.expired_count) {

                        if (elem.expired_count) {
                            remove_groups_ids.push_back(group_id);
                            continue;
                        }

                        for (auto gid : remove_groups_ids) {
                            queue.erase(gid);
                        }
                        return;
                    }
                }

                for (auto gid : remove_groups_ids) {
                    queue.erase(gid);
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
            std::vector<uint64_t> remove_groups_ids;
            remove_groups_ids.reserve(10);

            std::lock_guard _(mutex_);

            for (auto& [_, queue] : queues_) {
                remove_groups_ids.clear();
                for (auto& [group_id, tqueue] : queue) {
                    auto t = tqueue.Empty();
                    if (t) {
                        remove_groups_ids.push_back(group_id);
                        continue;
                    }

                    tqueue.PopFront(elem);
                    if (elem.has_value || elem.expired_count) {

                        if (elem.expired_count) {
                            remove_groups_ids.push_back(group_id);
                            continue;
                        }

                        for (auto gid : remove_groups_ids) {
                            queue.erase(gid);
                        }

                        return;
                    }
                }

                for (auto gid : remove_groups_ids) {
                    queue.erase(gid);
                }
            }
        }

        /**
         * @brief Pop/remove the first object from queue
         */
        void Pop()
        {
            std::vector<uint64_t> remove_groups_ids;
            remove_groups_ids.reserve(10);

            std::lock_guard _(mutex_);

            for (auto& [_, queue] : queues_) {
                remove_groups_ids.clear();
                for (auto& [group_id, tqueue] : queue) {
                    if (tqueue.Empty()) {
                        remove_groups_ids.push_back(group_id);
                        continue;
                    }

                    tqueue.Pop();
                    return;
                }

                for (auto group_id : remove_groups_ids) {
                    queue.erase(group_id);
                }
            }
        }

        /**
         * @brief Clear queue all
         */
        void Clear()
        {
            std::lock_guard _(mutex_);
            queues_.clear();
        }

        size_t Size()
        {
            std::lock_guard _(mutex_);

            return std::accumulate(queues_.begin(), queues_.end(), 0, [](auto total_sum, auto& group) {
                auto group_sum = std::accumulate(
                  group.second.begin(), group.second.end(), 0, [](auto group_sum, const auto& group_pair) {
                      return group_sum + group_pair.second.Size();
                  });
                return total_sum + group_sum;
            });
        }

        bool Empty() const
        {
            for (auto& [_, queue] : queues_) {
                if (!queue.empty()) {
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
         * @param group_id  The group id for the timequeue
         *
         * @return Unique pointer to queue for the given priority
         */
        TimeQueueType& GetQueueByPriorityGroupId(const uint8_t priority, uint64_t group_id)
        {
            if (priority >= PMAX) {
                throw InvalidPriorityException("Priority not within range");
            }

            auto [it, _] =
              queues_[priority].try_emplace(group_id, duration_ms_, interval_ms_, tick_service_, initial_queue_size_);

            return it->second;
        }

        std::mutex mutex_;
        size_t initial_queue_size_;
        size_t duration_ms_;
        size_t interval_ms_;

        /// queues_[priority][group_id] = time queue of objects
        std::map<uint8_t, std::map<uint64_t, TimeQueueType>> queues_;
        std::shared_ptr<TickService> tick_service_;
    };
}; // end of namespace quicr
