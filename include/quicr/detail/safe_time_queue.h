// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "time_queue.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <numeric>
#include <quicr/defer.h>

namespace quicr {

    /**
     * @brief Thread-safe time queue
     *
     * @tparam DataType   The element type to be stored.
     */
    template<typename DataType>
    class SafeTimeQueue
    {
        using TimeQueueType = TimeQueue<DataType>;

        struct Exception : public std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        struct InvalidPriorityException : public Exception
        {
            using Exception::Exception;
        };

      public:
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
                      const std::shared_ptr<TickService>& tick_service,
                      size_t initial_queue_size)
          : time_queue_(duration, interval, tick_service, initial_queue_size)
          , tick_service_(tick_service)
        {
        }

        /**
         * Construct a time queue
         * @param tick_service Shared pointer to tick_service service
         */
        SafeTimeQueue(const std::shared_ptr<TickService>& tick_service)
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
        void Push(DataType& value, uint32_t ttl, uint32_t delay_ttl = 0)
        {
            std::lock_guard _(mutex_);
            time_queue_.Push(value, ttl, delay_ttl);
        }

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param value     The value to push onto the queue.
         * @param ttl       The time to live of the value in milliseconds.
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         */
        void Push(DataType&& value, uint32_t ttl, uint32_t delay_ttl = 0)
        {
            std::lock_guard _(mutex_);
            time_queue_.Push(std::move(value), ttl, delay_ttl);
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

            std::lock_guard _(mutex_);

            time_queue_.Front(elem);
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

            std::lock_guard _(mutex_);
            time_queue_.PopFront(elem);
        }

        /**
         * @brief Pop/remove the first object from queue
         */
        void Pop()
        {
            std::lock_guard _(mutex_);
            if (!time_queue_.Empty()) {
                time_queue_.Pop();
            }
        }

        /**
         * @brief Clear queue all
         */
        void Clear()
        {
            std::lock_guard _(mutex_);
            time_queue_.Clear();
        }

        size_t Size()
        {
            std::lock_guard _(mutex_);

            return time_queue_.Size();
        }

        bool Empty() const { return time_queue_.Empty(); }

      private:
        std::mutex mutex_;
        TimeQueueType time_queue_;

        std::shared_ptr<TickService> tick_service_;
    };
}; // end of namespace quicr
