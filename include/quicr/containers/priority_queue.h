// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/utilities/thread_safety.h"

#include <timeq/time_queue.h>

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <numeric>

namespace quicr {

    /**
     * @brief Lockable priority/group queue backed by `timeq::time_queue` instances.
     *
     * @details Order is maintained for objects pushed by priority.
     *          During each `Front()`/`PopFront()` the numerically lowest priority is selected first, then the
     *          numerically lowest group ID. Objects within that group retain the ordering of `timeq::time_queue`.
     *          Queue operations do not acquire the mutex automatically; callers must hold this object's capability.
     *
     * @tparam DataType The element type to be stored.
     */
    template<typename DataType>
    class QUICR_CAPABILITY("mutex") PriorityQueue
    {
        using TimeQueueType = timeq::time_queue<DataType>;
        using TimeQueueReference = typename TimeQueueType::reference;
        using TimeQueueValueType = typename TimeQueueType::value_type;

        static constexpr int kMinFreeTimeQueues = 2;
        static constexpr int kMaxFreeTimeQueues = 10;

        struct Exception : public std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        struct InvalidPriorityException : public Exception
        {
            using Exception::Exception;
        };

      public:
        /** @brief Acquires the queue mutex, blocking until available. */
        constexpr void lock() QUICR_ACQUIRE() { mutex_.lock(); }

        /** @brief Releases the queue mutex; the calling thread must own it. */
        constexpr void unlock() QUICR_RELEASE() { mutex_.unlock(); }

        /** @return `true` if the queue mutex was acquired without blocking. */
        constexpr bool try_lock() QUICR_TRY_ACQUIRE(true) { return mutex_.try_lock(); }

        /**
         * Construct a priority queue
         *
         * @param duration Max duration of time for the queue
         * @param interval Interval per bucket, Default is 1
         * @param tick_service Shared pointer to tick_service service
         * @param initial_queue_size Number of default fifo queue size (reserve)
         * @throws std::invalid_argument If `tick_service` is null.
         * @throws Any exception thrown while constructing the underlying time queues.
         * @note The queue shares ownership of `tick_service`.
         */
        PriorityQueue(size_t duration,
                      size_t interval,
                      const std::shared_ptr<timeq::tick_service>& tick_service,
                      size_t initial_queue_size)
          : tick_service_(tick_service)
        {

            if (tick_service == nullptr) {
                throw std::invalid_argument("Tick service cannot be null");
            }

            initial_queue_size_ = initial_queue_size;
            duration_ms_ = duration;
            interval_ms_ = interval;

            InitFreeTimeQueues();
        }

        /**
         * Construct a priority queue
         * @param tick_service Shared pointer to tick_service service
         * @throws std::invalid_argument If `tick_service` is null.
         */
        PriorityQueue(const std::shared_ptr<timeq::tick_service>& tick_service)
          : PriorityQueue(1000, 1, tick_service, 1000)
        {
        }

        /** @brief Destroys all queues; no thread may hold or wait on its mutex. */
        ~PriorityQueue() = default;

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param group_id Group used to order values within a priority.
         * @param value The value to push onto the queue.
         * @param ttl The time to live of the value in milliseconds.
         * @param priority The priority of the value.
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         * @pre The caller holds this queue's lock.
         * @throws Any exception thrown while allocating a group queue or copying `value`.
         */
        void Push(std::uint64_t group_id, DataType& value, uint32_t ttl, uint8_t priority = 0, uint32_t delay_ttl = 0)
          QUICR_REQUIRES(this)
        {
            auto& queue = GetQueueByPriorityGroupId(priority, group_id);
            queue.push(value, ttl, delay_ttl);
        }

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param group_id Group used to order values within a priority.
         * @param value The value to push onto the queue.
         * @param ttl The time to live of the value in milliseconds.
         * @param priority The priority of the value.
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         * @pre The caller holds this queue's lock.
         * @throws Any exception thrown while allocating a group queue or moving `value`.
         */
        void Push(std::uint64_t group_id, DataType&& value, uint32_t ttl, uint8_t priority = 0, uint32_t delay_ttl = 0)
          QUICR_REQUIRES(this)
        {
            auto& queue = GetQueueByPriorityGroupId(priority, group_id);
            queue.push(std::move(value), ttl, delay_ttl);
        }

        /**
         * @brief Get the first object from queue
         *
         * @return A reference-like element from the first nonempty priority/group queue, or the underlying empty
         * sentinel when none remains.
         * @pre The caller holds this queue's lock.
         * @note Empty/expired group queues encountered during the search are removed. The result follows
         * `timeq::time_queue` invalidation rules and must not outlive the lock or a queue mutation.
         */
        TimeQueueReference Front() QUICR_REQUIRES(this)
        {
            TimeQueueReference front{ std::nullopt, 0 };

            while (!(queues_.empty() || front.value.has_value())) {
                auto& [_, groups] = *queues_.begin();
                auto& [__, queue] = *groups.begin();

                front = queue->front();
                if (front.value.has_value()) {
                    break;
                }

                if (queue->empty()) {
                    if (free_tqueues_.size() < kMaxFreeTimeQueues) {
                        free_tqueues_.push_back(std::move(queue));
                    }
                    groups.erase(groups.begin());
                }

                if (groups.empty()) {
                    queues_.erase(queues_.begin());
                }
            }

            return front;
        }

        /**
         * @brief Get and remove the first object from queue
         *
         * @return The removed first nonexpired element and expiration metadata, or the underlying empty sentinel.
         * @pre The caller holds this queue's lock.
         */
        TimeQueueValueType PopFront() QUICR_REQUIRES(this)
        {
            TimeQueueValueType front{ std::nullopt, 0 };

            while (!(queues_.empty() || front.value.has_value())) {
                auto& [_, groups] = *queues_.begin();
                auto& [__, queue] = *groups.begin();

                front = queue->pop_front();

                if (queue->empty()) {
                    if (free_tqueues_.size() < kMaxFreeTimeQueues) {
                        free_tqueues_.push_back(std::move(queue));
                    }
                    groups.erase(groups.begin());
                }

                if (groups.empty()) {
                    queues_.erase(queues_.begin());
                }

                if (front.value.has_value()) {
                    break;
                }
            }

            return front;
        }

        /**
         * @brief Removes one object from the numerically first priority/group queue.
         * @pre The caller holds this queue's lock.
         * @note Does nothing when no group queue exists.
         */
        void Pop() QUICR_REQUIRES(this)
        {
            if (queues_.empty()) {
                return;
            }

            auto& [_, groups] = *queues_.begin();
            auto& [__, queue] = *groups.begin();

            queue->pop();
            if (queue->empty()) {
                if (free_tqueues_.size() < kMaxFreeTimeQueues) {
                    free_tqueues_.push_back(std::move(queue));
                }
                groups.erase(groups.begin());
            }

            if (groups.empty()) {
                queues_.erase(queues_.begin());
            }
        }

        /**
         * @brief Removes all active queues and replenishes the reusable queue pool.
         * @pre The caller holds this queue's lock.
         */
        void Clear() QUICR_REQUIRES(this)
        {
            queues_.clear();

            InitFreeTimeQueues();
        }

        /**
         * @return Sum of sizes reported by all active group queues.
         * @pre The caller holds this queue's lock.
         */
        size_t Size() QUICR_REQUIRES(this)
        {
            return std::accumulate(queues_.begin(), queues_.end(), 0, [](auto total_sum, auto& group) {
                auto group_sum = std::accumulate(
                  group.second.begin(), group.second.end(), 0, [](auto group_sum, const auto& group_pair) {
                      return group_sum + group_pair.second->size();
                  });
                return total_sum + group_sum;
            });
        }

        /**
         * @return `true` when no priority contains a registered group queue.
         * @pre The caller holds this queue's lock.
         * @note Expired underlying queues are removed by `Front()`, `PopFront()`, or `Pop()`, so this may remain
         * `false` until one of those operations performs cleanup.
         */
        bool Empty() const QUICR_REQUIRES(this)
        {
            for (auto& [_, queue] : queues_) {
                if (!queue.empty()) {
                    return false;
                }
            }

            return true;
        }

      private:
        void InitFreeTimeQueues()
        {
            /*
             * Initialize free time queues with two starting entries.
             */
            for (int i = static_cast<int>(free_tqueues_.size()); i < kMinFreeTimeQueues; ++i) {
                free_tqueues_.emplace_back(
                  std::make_shared<TimeQueueType>(duration_ms_, interval_ms_, tick_service_, initial_queue_size_));
            }
        }

        /**
         * @brief Get queue by priority
         *
         * @param priority The priority queue value.
         * @param group_id The group id for the timequeue
         *
         * @return Unique pointer to queue for the given priority
         */
        TimeQueueType& GetQueueByPriorityGroupId(const uint8_t priority, uint64_t group_id)
        {
            auto grp_it = queues_[priority].find(group_id);
            if (grp_it != queues_[priority].end()) {
                return *grp_it->second;
            }

            if (free_tqueues_.empty()) {
                auto [tqueue, _] = queues_[priority].emplace(
                  group_id,
                  std::make_shared<TimeQueueType>(duration_ms_, interval_ms_, tick_service_, initial_queue_size_));

                InitFreeTimeQueues();

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

        std::map<uint8_t, std::map<uint64_t, std::shared_ptr<TimeQueueType>>>
          queues_; ///< queues_[priority][group_id] = time queue of objects

        /**
         * List of unused and available time queues. Free should be removed when in use. It should
         * be added back when freed from being used.
         */
        std::vector<std::shared_ptr<TimeQueueType>> free_tqueues_;

        std::shared_ptr<timeq::tick_service> tick_service_;
    };
}; // end of namespace quicr
