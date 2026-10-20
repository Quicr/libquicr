// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/utilities/thread_safety.h"

#include <timeq/time_queue.h>

#include <algorithm>
#include <chrono>
#include <mutex>

namespace quicr {

    /**
     * @brief Lockable wrapper around `timeq::time_queue`.
     *
     * Queue operations do not lock automatically. A caller performing an annotated queue operation must first hold
     * this object's capability, for example with `std::lock_guard<SafeTimeQueue>`. The `lock`, `unlock`, and
     * `try_lock` members satisfy the BasicLockable/Lockable interface.
     *
     * @tparam DataType The element type to be stored.
     */
    template<typename DataType>
    class QUICR_CAPABILITY("mutex") SafeTimeQueue
    {
        using TimeQueueType = timeq::time_queue<DataType>;

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
         * Construct a time queue
         *
         * @param duration Max duration of time for the queue
         * @param interval Interval per bucket, Default is 1
         * @param tick_service Shared pointer to tick_service service
         * @param initial_queue_size Number of default fifo queue size (reserve)
         * @throws Any exception thrown by the underlying `timeq::time_queue` constructor.
         * @note The queue shares ownership of `tick_service`.
         */
        SafeTimeQueue(size_t duration,
                      size_t interval,
                      const std::shared_ptr<timeq::tick_service>& tick_service,
                      size_t initial_queue_size)
          : time_queue_(duration, interval, tick_service, initial_queue_size)
        {
        }

        /**
         * Construct a time queue
         * @param tick_service Shared pointer to tick_service service
         * @throws Any exception thrown by the full constructor.
         */
        SafeTimeQueue(const std::shared_ptr<timeq::tick_service>& tick_service)
          : SafeTimeQueue(1000, 1, tick_service, 1000)
        {
        }

        /** @brief Destroys the queue; no thread may hold or wait on its mutex. */
        ~SafeTimeQueue() = default;

        /**
         * @brief Pushes a new value onto the queue with a time to live
         *
         * @param value The value to push onto the queue.
         * @param ttl The time to live of the value in milliseconds.
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         * @pre The caller holds this queue's lock.
         * @throws Any exception thrown while copying or inserting `value`.
         */
        void Push(DataType& value, uint32_t ttl, uint32_t delay_ttl = 0) QUICR_REQUIRES(this)
        {
            time_queue_.push(value, ttl, delay_ttl);
        }

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param value The value to push onto the queue.
         * @param ttl The time to live of the value in milliseconds.
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         * @pre The caller holds this queue's lock.
         * @throws Any exception thrown while moving or inserting `value`.
         */
        void Push(DataType&& value, uint32_t ttl, uint32_t delay_ttl = 0) QUICR_REQUIRES(this)
        {
            time_queue_.push(std::move(value), ttl, delay_ttl);
        }

        /**
         * @brief Get the first object from queue
         *
         * @return The underlying queue's front reference, including its optional value and expiration metadata.
         * @pre The caller holds this queue's lock.
         * @note The returned reference follows `timeq::time_queue` invalidation rules and must not be used after the
         * lock is released or the queue is mutated.
         */
        typename timeq::time_queue<DataType>::reference Front() QUICR_REQUIRES(this) { return time_queue_.front(); }

        /**
         * @brief Get and remove the first object from queue
         *
         * @return The removed front element and expiration metadata, or the underlying empty sentinel.
         * @pre The caller holds this queue's lock.
         */
        typename timeq::time_queue<DataType>::value_type PopFront() QUICR_REQUIRES(this)
        {
            return time_queue_.pop_front();
        }

        /**
         * @brief Removes the first object, following the underlying queue's empty-queue behavior.
         * @pre The caller holds this queue's lock.
         */
        void Pop() QUICR_REQUIRES(this) { time_queue_.pop(); }

        /**
         * @brief Removes all queued objects.
         * @pre The caller holds this queue's lock.
         */
        void Clear() QUICR_REQUIRES(this) { time_queue_.clear(); }

        /** @return Number of queued elements. The caller must hold this queue's lock. */
        size_t Size() QUICR_REQUIRES(this) { return time_queue_.size(); }

        /** @return `true` if no elements are queued. The caller must hold this queue's lock. */
        bool Empty() const QUICR_REQUIRES(this) { return time_queue_.empty(); }

      private:
        mutable std::mutex mutex_;
        TimeQueueType time_queue_;
    };
}; // end of namespace quicr
