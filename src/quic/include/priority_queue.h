#pragma once

#include <algorithm>
#include <chrono>
#include <forward_list>
#include <numeric>
#include <optional>
#include <array>

#include "time_queue.h"

namespace qtransport {
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
        ~PriorityQueue() {
        }

        /**
         * Construct a priority queue
         * @param tick_service Shared pointer to tick_service service
         */
        PriorityQueue(const std::shared_ptr<TickService>& tick_service)
          : PriorityQueue(1000, 1, tick_service_, 1000)
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
        void Push(DataType& value, uint32_t ttl, uint8_t priority = 0, uint32_t delay_ttl=0)
        {
            std::lock_guard<std::mutex> _(mutex_);

            auto& queue = GetQueueByPriority(priority);
            queue->Push(value, ttl, delay_ttl);
        }

        /**
         * @brief Pushes a new value onto the queue with a time to live and priority
         *
         * @param value     The value to push onto the queue.
         * @param ttl       The time to live of the value in milliseconds.
         * @param priority  The priority of the value (range is 0 - PMAX)
         * @param delay_ttl Delay POP by this ttl value in milliseconds
         */
        void Push(DataType&& value, uint32_t ttl, uint8_t priority = 0, uint32_t delay_ttl=0) {
            std::lock_guard<std::mutex> _(mutex_);

            auto& queue = GetQueueByPriority(priority);
            queue->Push(std::move(value), ttl, delay_ttl);
        }

        /**
         * @brief Get the first object from queue
         *
         * @return TimeQueueElement<DataType> value from time queue
         */
        TimeQueueElement<DataType> Front()
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& tqueue : queue_) {
                if (!tqueue || tqueue->Empty())
                    continue;

                return std::move(tqueue->Front());
            }

            return {};
        }

        /**
         * @brief Get and remove the first object from queue
         *
         * @return TimeQueueElement<DataType> from time queue
         */
        TimeQueueElement<DataType> PopFront()
        {
            std::lock_guard<std::mutex> _(mutex_);

            for (auto& tqueue : queue_) {
                if (!tqueue || tqueue->Empty())
                    continue;

                return std::move(tqueue->PopFront());
            }

            return {};
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
                if (tqueue && !tqueue->Empty())
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
                queue_[priority] = std::make_unique<TimeQueueType>(duration_ms_, interval_ms_, tick_service_, initial_queue_size_);
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
}; // end of namespace qtransport
