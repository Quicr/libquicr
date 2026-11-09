// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>

namespace quicr {

    /**
     * @brief Thread-safe FIFO queue with optional bounded, drop-oldest insertion.
     *
     * All queue mutations and value access are serialized by an internal mutex. `Empty()` is a lock-free atomic
     * snapshot and may be stale immediately after it returns. Waiting can be permanently disabled with
     * `StopWaiting()`.
     *
     * @tparam T Stored value type.
     */
    template<typename T>
    class SafeQueue
    {
      public:
        /**
         * @brief Constructs an empty queue.
         *
         * @param limit Maximum retained element count; zero means unlimited. Insertion never blocks.
         */
        SafeQueue(uint32_t limit = 5'000)
          : stop_waiting_{ false }
          , limit_{ limit }
        {
        }

        /** @brief Wakes current waiters before destroying the queue; callers must ensure no operation remains active.
         */
        ~SafeQueue() { StopWaiting(); }

        /**
         * @brief Copies an element to the back of the queue.
         *
         * If the configured nonzero limit has already been reached, the oldest element is discarded and the new
         * element is still inserted. The call never waits for capacity.
         *
         * @param elem The element to insert.
         * @return `true` if no element was evicted; `false` if the oldest element was evicted to make room.
         * @throws Any exception thrown while copying or allocating storage for `T`.
         */
        bool Push(T const& elem)
        {
            bool rval = true;

            std::lock_guard<std::mutex> _(mutex_);

            if (queue_.empty()) {
                cv_.notify_one();
                empty_ = false;
            }

            else if (limit_ != 0 && queue_.size() >= limit_) { // Make room by removing first element
                queue_.pop();
                rval = false;
            }

            queue_.push(elem);

            return rval;
        }

        /**
         * @brief Moves an element to the back, evicting the oldest element when at a nonzero limit.
         * @param elem Element to move into the queue.
         * @return `true` if no element was evicted; `false` if insertion evicted the oldest element.
         * @throws Any exception thrown while moving or allocating storage for `T`.
         */
        bool Push(T&& elem)
        {
            bool rval = true;

            std::lock_guard<std::mutex> _(mutex_);

            if (queue_.empty()) {
                cv_.notify_one();
                empty_ = false;
            }

            else if (limit_ != 0 && queue_.size() >= limit_) { // Make room by removing first element
                queue_.pop();
                rval = false;
            }

            queue_.emplace(std::move(elem));

            return rval;
        }

        /**
         * @brief Remove the first object from queue (oldest object)
         *
         * @return `std::nullopt` if empty; otherwise a copy of the removed value.
         * @throws Any exception thrown while copying `T`.
         */
        std::optional<T> Pop()
        {
            std::lock_guard<std::mutex> _(mutex_);
            return PopInternal();
        }

        /**
         * @brief Get first object without removing from queue
         *
         * @return `std::nullopt` if empty; otherwise a copy of the oldest value. The queue is unchanged.
         * @throws Any exception thrown while copying `T`.
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
         * @brief Removes the oldest object, or does nothing when empty.
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
         * Waits until an element is available or `StopWaiting()` is called. The predicate is checked while holding
         * the queue mutex, so spurious wakeups do not produce an empty result.
         *
         * @return `std::nullopt` after waiting is stopped; otherwise a copy of and removal of the oldest value.
         * @throws Any exception thrown while copying `T`.
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
         * @return Number of retained elements at the instant the mutex is held.
         */
        size_t Size()
        {
            std::lock_guard<std::mutex> _(mutex_);
            return queue_.size();
        }

        /**
         * @brief Removes all elements without changing the limit or stopped-waiting state.
         * @note This does not wake a thread waiting in `BlockPop()`.
         */
        void Clear()
        {
            std::lock_guard<std::mutex> _(mutex_);
            std::queue<T> empty;
            std::swap(queue_, empty);
            empty_ = true;
        }

        /**
         * @brief Check if queue is empty
         *
         * @returns Atomic snapshot of whether the queue is empty.
         */
        bool Empty() const { return empty_; }

        /**
         * @brief Permanently causes current and future `BlockPop()` calls to return without waiting.
         * @details Wakes all current waiters. It does not clear the queue and does not prevent later insertions.
         */
        void StopWaiting()
        {
            std::lock_guard<std::mutex> _(mutex_);
            stop_waiting_ = true;
            cv_.notify_all();
        }

        /**
         * @brief Changes the maximum retained element count.
         * @param limit New limit; zero means unlimited.
         * @note Existing elements are not removed when lowering the limit. A later insertion evicts at most one
         * oldest element, so the queue can remain above the new limit until enough insertions or removals occur.
         */
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

} /* namespace quicr */
