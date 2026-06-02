// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <quicr/detail/thread_safety.h>

#include <timeq/time_queue.h>

#include <algorithm>
#include <chrono>
#include <mutex>

namespace quicr {

    /**
     * @brief Thread-safe time queue
     *
     * @tparam DataType   The element type to be stored.
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
        constexpr void lock() QUICR_ACQUIRE() { mutex_.lock(); }
        constexpr void unlock() QUICR_RELEASE() { mutex_.unlock(); }
        constexpr bool try_lock() QUICR_TRY_ACQUIRE(true) { return mutex_.try_lock(); }

        /**
         * Construct a time queue
         *
         * @param duration              Max duration of time for the queue
         * @param interval              Interval per bucket, Default is 1
         * @param tick_service          Shared pointer to tick_service service
         * @param initial_queue_size    Number of default fifo queue size (reserve)
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
         */
        SafeTimeQueue(const std::shared_ptr<timeq::tick_service>& tick_service)
          : SafeTimeQueue(1000, 1, tick_service, 1000)
        {
        }

        ~SafeTimeQueue() = default;

        /**
         * @brief Pushes a new value onto the queue with a time to live
         *
         * @param value     The value to push onto the queue.
         * @param ttl       The time to live of the value in milliseconds.
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         */
        void Push(DataType& value, uint32_t ttl, uint32_t delay_ttl = 0) QUICR_REQUIRES(this)
        {
            time_queue_.push(value, ttl, delay_ttl);
        }

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param value     The value to push onto the queue.
         * @param ttl       The time to live of the value in milliseconds.
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         */
        void Push(DataType&& value, uint32_t ttl, uint32_t delay_ttl = 0) QUICR_REQUIRES(this)
        {
            time_queue_.push(std::move(value), ttl, delay_ttl);
        }

        /**
         * @brief Get the first object from queue
         *
         * @return timeq::element<DataType> reference
         */
        typename timeq::time_queue<DataType>::reference Front() QUICR_REQUIRES(this) { return time_queue_.front(); }

        /**
         * @brief Get and remove the first object from queue
         *
         * @return timeq::element<DataType> element
         */
        typename timeq::time_queue<DataType>::value_type PopFront() QUICR_REQUIRES(this)
        {
            return time_queue_.pop_front();
        }

        /**
         * @brief Pop/remove the first object from queue
         */
        void Pop() QUICR_REQUIRES(this) { time_queue_.pop(); }

        /**
         * @brief Clear queue all
         */
        void Clear() QUICR_REQUIRES(this) { time_queue_.clear(); }

        size_t Size() QUICR_REQUIRES(this) { return time_queue_.size(); }

        bool Empty() const QUICR_REQUIRES(this) { return time_queue_.empty(); }

      private:
        mutable std::mutex mutex_;
        TimeQueueType time_queue_;
    };
}; // end of namespace quicr
