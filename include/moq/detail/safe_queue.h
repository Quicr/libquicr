// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <unistd.h>

namespace qtransport {

    /**
     * @brief safe_queue is a thread safe basic queue
     *
     * @details This class is a thread safe wrapper for std::queue<T>.
     * 		Not all operators or methods are implemented.
     *
     * @todo Implement any operators or methods needed
     */
    template<typename T>
    class SafeQueue
    {
      public:
        /**
         * @brief safe_queue constructor
         *
         * @param limit     Limit number of messages in queue before push blocks. Zero
         *                  is unlimited.
         */
        SafeQueue(uint32_t limit = 1000)
          : stop_waiting_{ false }
          , limit_{ limit }
        {
        }

        ~SafeQueue() { StopWaiting(); }

        /**
         * @brief inserts element at the end of queue
         *
         * @details Inserts element at the end of queue. If queue is at max size,
         *    the front element will be popped/removed to make room.
         *    In this sense, the queue is sliding forward with every new message
         *    added to queue.
         *
         * @param elem
         * @return True if successfully pushed, false if not.  The cause for false is
         * that the queue is full.
         */
        bool Push(T const& elem)
        {
            bool rval = true;

            std::lock_guard<std::mutex> _(mutex_);

            if (queue_.empty()) {
                cv_.notify_one();
                empty_ = false;
            }

            else if (queue_.size() >= limit_) { // Make room by removing first element
                queue_.pop();
                rval = false;
            }

            queue_.push(elem);

            return rval;
        }

        /**
         * @brief Remove the first object from queue (oldest object)
         *
         * @return std::nullopt if queue is empty, otherwise reference to object
         */
        std::optional<T> Pop()
        {
            std::lock_guard<std::mutex> _(mutex_);
            return PopInternal();
        }

        /**
         * @brief Get first object without removing from queue
         *
         * @return std::nullopt if queue is empty, otherwise reference to object
         */
        std::optional<T> Front()
        {
            std::lock_guard<std::mutex> _(mutex_);

            if (queue_.empty()) {
                return std::nullopt;
            }

            return queue_.front();
        }

        /**
         * @brief Remove (aka pop) the first object from queue
         *
         */
        void PopFront()
        {
            std::lock_guard<std::mutex> _(mutex_);

            PopFrontInternal();
        }

        /**
         * @brief Block waiting for data in queue, then remove the first object from
         * queue (oldest object)
         *
         * @details This will block if the queue is empty. Due to concurrency, it's
         * possible that when unblocked the queue might still be empty. In this case,
         * try again.
         *
         * @return std::nullopt if queue is empty, otherwise reference to object
         */
        std::optional<T> BlockPop()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [&]() { return (stop_waiting_ || (queue_.size() > 0)); });

            if (stop_waiting_) {
                return std::nullopt;
            }

            return PopInternal();
        }

        /**
         * @brief Size of the queue
         *
         * @return size of the queue
         */
        size_t Size()
        {
            std::lock_guard<std::mutex> _(mutex_);
            return queue_.size();
        }

        /**
         * @brief Clear the queue
         */
        void Clear()
        {
            std::lock_guard<std::mutex> _(mutex_);
            std::queue<T> empty;
            std::swap(queue_, empty);
        }

        /**
         * @brief Check if queue is empty
         *
         * @returns True if empty, false if not
         */
        bool Empty() const { return empty_; }

        /**
         * @brief Put the queue in a state such that threads will not wait
         *
         * @return Nothing
         */
        void StopWaiting()
        {
            std::lock_guard<std::mutex> _(mutex_);
            stop_waiting_ = true;
            cv_.notify_all();
        }

        void SetLimit(uint32_t limit)
        {
            std::lock_guard<std::mutex> _(mutex_);
            limit_ = limit;
        }

      private:
        /**
         * @brief Remove the first object from queue (oldest object)
         *
         * @return std::nullopt if queue is empty, otherwise reference to object
         *
         * @details The mutex must be locked by the caller
         */
        std::optional<T> PopInternal()
        {
            if (queue_.empty()) {
                empty_ = true;
                return std::nullopt;
            }

            auto elem = queue_.front();
            queue_.pop();

            if (queue_.empty()) {
                empty_ = true;
            }

            return elem;
        }

        /**
         * @brief Remove the first object from queue (oldest object)
         *
         * @details The mutex must be locked by the caller
         */
        void PopFrontInternal()
        {
            if (queue_.empty()) {
                empty_ = true;
                return;
            }

            queue_.pop();

            if (queue_.empty()) {
                empty_ = true;
            }
        }

        std::atomic<bool> empty_{ true };
        bool stop_waiting_;          // Instruct threads to stop waiting
        uint32_t limit_;             // Limit of number of messages in queue
        std::condition_variable cv_; // Signaling for thread syncronization
        std::mutex mutex_;           // read/write lock
        std::queue<T> queue_;        // Queue
    };

} /* namespace qtransport */
