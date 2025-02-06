// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <array>
#include <chrono>
#include <numeric>
#include <optional>

#include "safe_queue.h"

namespace quicr {
    /**
     * @brief Priority queue that uses a queue for each priority
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
         *
         * @param tick_service          Shared pointer to tick_service service
         * @param max_queue_size        Number of default fifo queue size (reserve)
         */
        PriorityQueue(const std::shared_ptr<TickService>& tick_service,
                      size_t max_queue_size)
          : tick_service_(tick_service)
          , max_queue_size_(max_queue_size)
        {
            if (tick_service == nullptr) {
                throw std::invalid_argument("Tick service cannot be null");
            }

            for (auto& queue: queue_) {
                queue.SetLimit(max_queue_size);
            }
        }

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param value     The value to push onto the queue.
         * @param priority  The priority of the value (range is 0 - PMAX)
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         */
        void Push(DataType& value, uint8_t priority = 0, uint32_t delay_ttl = 0)
        {
            std::lock_guard<std::mutex> _(mutex_);
            queue_[priority].Push(value);
        }

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param value     The value to push onto the queue.
         * @param ttl       The time to live of the value in milliseconds.
         * @param priority  The priority of the value (range is 0 - PMAX)
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         */
        void Push(DataType&& value, uint32_t ttl, uint8_t priority = 0, uint32_t delay_ttl = 0)
        {
            std::lock_guard<std::mutex> _(mutex_);

            queue_[priority].Push(std::move(value));
        }

        /**
         * @brief Get the first object from queue
         *
         * @param elem[out]          Value reference that will be updated
         */
        void Front(DataType& elem)
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& queue : queue_) {
                if (queue.Empty())
                    continue;

                if (auto value = queue.Front()) {
                    elem = std::move(*value);
                }
            }
        }

        /**
         * @brief Get and remove the first object from queue
         *
         * @param elem[out]          Value reference that will be updated
         */
        void PopFront(DataType& elem)
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& queue : queue_) {
                if (queue.Empty())
                    continue;

                if (auto value = queue.Pop()) {
                    elem = std::move(*value);
                }
            }
        }

        /**
         * @brief Pop/remove the first object from queue
         */
        void Pop()
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& queue : queue_) {
                if (not queue.Empty())
                    return queue.PopFront();
            }
        }

        /**
         * @brief Clear queue
         */
        void Clear()
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& queue : queue_) {
                if (not queue.Empty())
                    queue.Clear();
            }
        }

        size_t Size()
        {
            return std::accumulate(queue_.begin(), queue_.end(), 0, [](auto sum, auto& queue) {
                return sum + queue.Size();
            });
        }

        bool Empty()
        {
            for (auto& queue : queue_) {
                if (not queue.Empty()) {
                    return false;
                }
            }

            return true;
        }

      private:
        std::mutex mutex_;
        size_t max_queue_size_;

        std::array<SafeQueue<DataType>, PMAX> queue_;
        std::shared_ptr<TickService> tick_service_;
    };
}; // end of namespace quicr
