// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "time_queue.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <numeric>

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
            InitFreeGroups();
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

            /*
             * Initialize free time queues with two starting entries.
             */
            for (int i = free_tqueues_.size(); i < 2; ++i) {
                free_tqueues_.emplace_back(
                  std::make_shared<TimeQueueType>(duration_ms_, interval_ms_, tick_service_, initial_queue_size_));
            }
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
            elem.expired_count = 0;
            elem.has_value = false;

            std::map<uint8_t, std::vector<uint64_t>> remove_group_ids;

            std::lock_guard _(mutex_);

            for (auto& [priority, queue] : queues_) {
                remove_group_ids.clear();
                for (auto& [group_id, tqueue] : queue) {
                    if (tqueue->Empty()) {
                        remove_group_ids[priority].push_back(group_id);
                        continue;
                    }

                    tqueue->Front(elem);
                    if (elem.has_value || elem.expired_count) {

                        if (elem.expired_count) {
                            remove_group_ids[priority].push_back(group_id);
                            continue;
                        }

                        RemoveGroupTimeQueue(priority, remove_group_ids[priority]);
                        return;
                    }
                }
            }

            for (auto& [priority, groups] : remove_group_ids) {
                RemoveGroupTimeQueue(priority, groups);
            }
        }

        /**
         * @brief Get and remove the first object from queue
         *
         * @param elem[out]          Time queue element storage. Will be updated.
         */
        void PopFront(TimeQueueElement<DataType>& elem)
        {
            elem.expired_count = 0;
            elem.has_value = false;

            std::map<uint8_t, std::vector<uint64_t>> remove_group_ids;

            std::lock_guard _(mutex_);

            for (auto& [priority, queue] : queues_) {
                remove_group_ids.clear();
                for (auto& [group_id, tqueue] : queue) {
                    if (tqueue->Empty()) {
                        remove_group_ids[priority].push_back(group_id);
                        continue;
                    }

                    tqueue->PopFront(elem);
                    if (elem.has_value || elem.expired_count) {
                        if (elem.expired_count) {
                            remove_group_ids[priority].push_back(group_id);
                            continue;
                        }

                        RemoveGroupTimeQueue(priority, remove_group_ids[priority]);
                        return;
                    }
                }

            }

            for (auto& [priority, groups] : remove_group_ids) {
                RemoveGroupTimeQueue(priority, groups);
            }
        }

        /**
         * @brief Pop/remove the first object from queue
         */
        void Pop()
        {
            std::map<uint8_t, std::vector<uint64_t>> remove_group_ids;

            std::lock_guard _(mutex_);

            for (auto& [priority, queue] : queues_) {
                remove_group_ids.clear();
                for (auto& [group_id, tqueue] : queue) {
                    if (tqueue->Empty()) {
                        remove_group_ids[priority].push_back(group_id);
                        continue;
                    }

                    tqueue->Pop();
                    return;
                }
            }

            for (auto& [priority, groups] : remove_group_ids) {
                RemoveGroupTimeQueue(priority, groups);
            }
        }

        /**
         * @brief Clear queue all
         */
        void Clear()
        {
            std::lock_guard _(mutex_);
            queues_.clear();

            InitFreeGroups();
        }

        size_t Size()
        {
            std::lock_guard _(mutex_);

            return std::accumulate(queues_.begin(), queues_.end(), 0, [](auto total_sum, auto& group) {
                auto group_sum = std::accumulate(
                  group.second.begin(), group.second.end(), 0, [](auto group_sum, const auto& group_pair) {
                      return group_sum + group_pair.second->Size();
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

        void InitFreeGroups()
        {
            /*
             * Initialize free time queues with two starting entries.
             */
            for (int i = free_tqueues_.size(); i < 2; ++i) {
                free_tqueues_.emplace_back(
                  std::make_shared<TimeQueueType>(duration_ms_, interval_ms_, tick_service_, initial_queue_size_));
            }
        }

        /**
         * @brief Removes the group from the queues and adds the timequeue back to the free list
         *
         * @param priority      Queue priority
         * @param group_id      Group Id to remove
         */
        void RemoveGroupTimeQueue(uint8_t priority, uint64_t group_id)
        {
            auto grp_it = queues_[priority].find(group_id);

            if (grp_it != queues_[priority].end()) {
                grp_it->second->Clear();

                if (free_tqueues_.size() < 10) {
                    free_tqueues_.emplace_back(grp_it->second);
                }

                queues_[priority].erase(grp_it);
            }
        }

        /**
         * @brief Removes groups from the queues and adds the timequeue back to the free list
         * @param priority      Queue priority
         * @param groups        List of groups to remove
         */
        void RemoveGroupTimeQueue(uint8_t priority, const std::vector<uint64_t>& groups)
        {
            for (const auto group_id : groups) {
                RemoveGroupTimeQueue(priority, group_id);
            }
        }

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

            auto grp_it = queues_[priority].find(group_id);
            if (grp_it != queues_[priority].end()) {
                return *grp_it->second;
            }

            if (free_tqueues_.empty()) {
                auto [tqueue, _] = queues_[priority].emplace(
                  group_id,
                  std::make_shared<TimeQueueType>(duration_ms_, interval_ms_, tick_service_, initial_queue_size_));

                InitFreeGroups();

                return *tqueue->second;
            }

            auto [tqueue, _] = queues_[priority].try_emplace(group_id, free_tqueues_.back());
            free_tqueues_.pop_back();
            return *tqueue->second;
        }

        std::mutex mutex_;
        size_t initial_queue_size_;
        size_t duration_ms_;
        size_t interval_ms_;

        /// queues_[priority][group_id] = time queue of objects
        std::map<uint8_t, std::map<uint64_t, std::shared_ptr<TimeQueueType>>> queues_;

        /**
         * List of unused and available time queues. Free should be removed when in use. It should
         * be added back when freed from being used.
         */
        std::vector<std::shared_ptr<TimeQueueType>> free_tqueues_;

        std::shared_ptr<TickService> tick_service_;
    };
}; // end of namespace quicr
