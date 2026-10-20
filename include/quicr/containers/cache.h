// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <timeq/tick_service.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace quicr {
#ifdef _MSC_VER
#define FORCE_INLINE inline __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif

    /**
     * @brief Time-bucketed ordered cache with shared ownership of stored values.
     * @tparam K Ordered key type.
     * @tparam T Stored value type.
     * @details Expiration is lazy: time is advanced by insertion and lookup operations, but not by `Size()` or
     * `Empty()`. This class is not thread-safe; externally serialize all access, including calls to const methods.
     */
    template<typename K, typename T>
    class Cache
    {
        /*=======================================================================*/
        // Internal type definitions
        /*=======================================================================*/

        using TickType = timeq::tick_service::tick_type;

        using BucketType = std::vector<K>;
        using ValueType = std::shared_ptr<T>;
        using CacheType = std::map<K, ValueType>;

      public:
        /**
         * @brief Constructs an empty cache.
         * @param duration Maximum TTL and total bucket window, in tick-service milliseconds.
         * @param interval Bucket width in milliseconds.
         * @param tick_service Clock shared with the cache.
         * @pre `interval` is nonzero.
         * @throws std::invalid_argument If duration is zero, is not a multiple of interval, equals interval, or the
         * tick service is null.
         */
        Cache(size_t duration, size_t interval, std::shared_ptr<timeq::tick_service> tick_service)
          : duration_{ duration }
          , interval_{ interval }
          , total_buckets_{ duration_ / interval_ }
          , tick_service_(std::move(tick_service))
        {
            if (duration == 0 || duration % interval != 0 || duration == interval) {
                throw std::invalid_argument("Invalid time_queue constructor args");
            }

            if (!tick_service_) {
                throw std::invalid_argument("Tick service cannot be null");
            }
        }

        /** @brief Default construction is unavailable because timing parameters are required. */
        Cache() = delete;

        /** @brief Copies cache state and shares ownership of values and the tick service. @param other Cache to copy.
         */
        Cache(const Cache& other) = default;

        /** @brief Moves cache state. @param other Cache to move. */
        Cache(Cache&& other) noexcept = default;

        /**
         * @brief Copies cache state and shares ownership of values and the tick service.
         * @param other Cache to copy.
         * @return `*this`.
         */
        Cache& operator=(const Cache& other) = default;

        /** @brief Moves cache state. @param other Cache to move. @return `*this`. */
        Cache& operator=(Cache&& other) noexcept = default;

        /** @return Number of currently stored map entries without advancing expiration time. */
        size_t Size() const noexcept { return cache_.size(); }

        /** @return Whether the map is currently empty without advancing expiration time. */
        bool Empty() const noexcept { return cache_.empty(); }

        /**
         * @brief Copies a value into the cache.
         * @param key Lookup key.
         * @param value Value copied into shared storage.
         * @param ttl Lifetime in milliseconds; zero means the configured maximum duration.
         * @throws std::invalid_argument If `ttl` exceeds the configured duration.
         * @warning Reinserting an existing key does not remove its earlier expiration-bucket entry; that older entry
         * may erase the replacement before its requested TTL.
         */
        void Insert(const K& key, const T& value, size_t ttl) { InternalInsert(key, value, ttl); }

        /**
         * @brief Inserts a value supplied as an rvalue.
         * @copydetails Insert(const K&, const T&, size_t)
         * @note The current implementation copies from its internal parameter into shared storage.
         */
        void Insert(const K& key, T&& value, size_t ttl) { InternalInsert(key, std::move(value), ttl); }

        /**
         * @brief Advances expiration and tests a key.
         * @param key Key to find.
         * @return `true` if the key remains present.
         * @warning Allocation failure while advancing expiration terminates the process because this function is
         * `noexcept`.
         */
        bool Contains(const K& key) noexcept
        {
            Advance();
            return cache_.find(key) != cache_.end();
        }

        /**
         * @brief Advances expiration and tests whether any key lies in an inclusive ordered range.
         * @param start_key Inclusive lower bound.
         * @param end_key Inclusive upper bound.
         * @return `true` if at least one key lies in `[start_key, end_key]`.
         * @throws std::invalid_argument If `start_key > end_key`.
         */
        bool Contains(const K& start_key, const K& end_key)
        {
            if (start_key > end_key) {
                throw std::invalid_argument("Exclusive end key must be greater than start key");
            }

            Advance();

            auto it_lower = cache_.lower_bound(start_key);
            auto it_upper = cache_.upper_bound(end_key);

            return it_lower != it_upper;
        }

        /**
         * @brief Retrieves a value by key after advancing expiration.
         * @param key Key to find.
         * @return Shared ownership of the value, or null if absent. The value can outlive its cache entry.
         * @warning Allocation failure while advancing expiration terminates the process because this function is
         * `noexcept`.
         */
        ValueType Get(const K& key) noexcept
        {
            if (!Contains(key)) {
                return nullptr;
            }

            return cache_.at(key);
        }

        /**
         * @brief Retrieves values beginning at an exact start key.
         * @param start_key Required first key.
         * @param end_key Inclusive stopping key.
         * @return Empty if the inclusive range contains no entry or `start_key` itself is absent; otherwise values
         * from `start_key` in key order through `end_key`. If `end_key` is absent, iteration continues to cache end.
         * @throws std::invalid_argument If `start_key > end_key`.
         */
        std::vector<ValueType> Get(const K& start_key, const K& end_key)
        {
            if (!Contains(start_key, end_key)) {
                return {};
            }

            auto it = cache_.find(start_key);
            if (it == cache_.end()) {
                return {};
            }

            std::vector<ValueType> entries;

            for (; it != cache_.end(); ++it) {
                entries.emplace_back(it->second);
                if (it->first == end_key) {
                    break;
                }
            }

            return entries;
        }

        /**
         * @return Shared ownership of the lowest-key unexpired value, or null when empty.
         * @warning Allocation failure while advancing expiration terminates the process because this function is
         * `noexcept`.
         */
        ValueType First() noexcept
        {
            Advance();

            if (cache_.empty()) {
                return nullptr;
            }

            return std::begin(cache_)->second;
        }

        /**
         * @return Shared ownership of the highest-key unexpired value, or null when empty.
         * @warning Allocation failure while advancing expiration terminates the process because this function is
         * `noexcept`.
         */
        ValueType Last() noexcept
        {
            Advance();

            if (cache_.empty()) {
                return nullptr;
            }

            return std::prev(std::end(cache_))->second;
        }

        /** @brief Removes all entries and expiration buckets and resets the current bucket index. */
        void Clear() noexcept
        {
            cache_.clear();
            bucket_index_ = 0;
            buckets_.clear();
        }

      protected:
        /**
         * @brief Applies elapsed bucket intervals and removes keys in expired buckets.
         * @note A jump spanning the full duration clears the cache. Derived classes must externally synchronize calls.
         */
        FORCE_INLINE void Advance()
        {
            const TickType new_ticks = static_cast<uint64_t>(
              std::chrono::duration_cast<std::chrono::milliseconds>(tick_service_->get()).count());
            const TickType delta = current_ticks_ ? (new_ticks - current_ticks_) / interval_ : 0;
            current_ticks_ = new_ticks;

            if (delta == 0) {
                return;
            }

            if (delta >= static_cast<TickType>(total_buckets_)) {
                Clear();
                return;
            }

            for (TickType i = 0; i < delta; ++i) {
                auto& bucket = buckets_[(bucket_index_ + i) % total_buckets_];
                for (const auto& key : bucket) {
                    cache_.erase(key);
                }
                bucket.clear();
                bucket.shrink_to_fit();
            }

            bucket_index_ = (bucket_index_ + delta) % total_buckets_;
        }

        /**
         * @brief Shared insertion implementation.
         * @tparam Value Source value type.
         * @param key Lookup key.
         * @param value Value copied into newly allocated shared storage.
         * @param ttl Lifetime in milliseconds; zero selects the maximum duration.
         */
        template<typename Value>
        FORCE_INLINE void InternalInsert(const K& key, Value value, size_t ttl)
        {
            if (ttl > duration_) {
                throw std::invalid_argument("TTL is greater than max duration");
            } else if (ttl == 0) {
                ttl = duration_;
            }

            ttl /= interval_;

            Advance();
            const std::size_t future_index = (bucket_index_ + ttl - 1) % total_buckets_;

            buckets_[future_index].push_back(key);
            cache_[key] = std::make_shared<T>(value);
        }

      protected:
        const size_t duration_; ///< Maximum cache duration in tick-service milliseconds.

        /// Interval at which buckets are cleared, in milliseconds.
        const size_t interval_;

        const size_t total_buckets_; ///< Number of expiration buckets, calculated as duration divided by interval.

        std::uint32_t bucket_index_{ 0 }; ///< Index of the current expiration bucket.

        /// Last observed tick-service time in milliseconds.
        TickType current_ticks_{ 0 };

        std::map<std::uint64_t, BucketType> buckets_; ///< Expiration bucket index to keys scheduled for removal.

        /// Ordered map of keys to shared values.
        CacheType cache_;

        /// Shared clock used to calculate elapsed bucket intervals.
        std::shared_ptr<timeq::tick_service> tick_service_;
    };

#undef FORCE_INLINE
} // namespace quicr
