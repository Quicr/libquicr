// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

#include "detail/tick_service.h"

namespace quicr {
    template<typename K, typename T>
    class Cache
    {
        /*=======================================================================*/
        // Internal type definitions
        /*=======================================================================*/

        using TickType = TickService::TickType;
        using IndexType = std::uint32_t;

        using BucketType = std::vector<K>;
        using ValueType = std::shared_ptr<T>;
        using CacheType = std::map<K, ValueType>;

      public:
        Cache(size_t duration, size_t interval, std::shared_ptr<TickService> tick_service)
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

            buckets_.resize(total_buckets_);
        }

        Cache() = delete;
        Cache(const Cache&) = default;
        Cache(Cache&&) noexcept = default;

        Cache& operator=(const Cache&) = default;
        Cache& operator=(Cache&&) noexcept = default;

        size_t Size() const noexcept { return cache_.size(); }
        bool Empty() const noexcept { return cache_.empty(); }

        void Insert(const K& key, const T& value, size_t ttl) { InternalInsert(key, value, ttl); }

        void Insert(const K& key, T&& value, size_t ttl) { InternalInsert(key, std::move(value), ttl); }

        bool Contains(const K& key) noexcept
        {
            Advance();
            return cache_.find(key) != cache_.end();
        }

        bool Contains(const K& start_key, const K& end_key)
        {
            if (start_key >= end_key) {
                throw std::invalid_argument("Exclusive end key must be greater than start key");
            }

            Advance();

            for (auto key = start_key; key < end_key; ++key) {
                if (cache_.find(key) == cache_.end()) {
                    return false;
                }
            }

            return true;
        }

        ValueType Get(const K& key) noexcept
        {
            if (!Contains(key)) {
                return nullptr;
            }

            return cache_.at(key);
        }

        std::vector<ValueType> Get(const K& start_key, const K& end_key)
        {

            if (!Contains(start_key, end_key)) {
                return {};
            }

            std::vector<ValueType> entries(end_key - start_key, nullptr);
            for (auto key = start_key; key < end_key; ++key) {
                entries[key - start_key] = cache_.at(key);
            }

            return entries;
        }

        ValueType First() noexcept
        {
            Advance();

            if (cache_.empty()) {
                return nullptr;
            }

            return std::begin(cache_)->second;
        }

        ValueType Last() noexcept
        {
            Advance();

            if (cache_.empty()) {
                return nullptr;
            }

            return std::prev(std::end(cache_))->second;
        }

        void Clear() noexcept
        {
            cache_.clear();
            bucket_index_ = 0;

            for (auto& bucket : buckets_) {
                bucket.clear();
            }
        }

      protected:
        inline void Advance()
        {
            const TickType new_ticks = tick_service_->Milliseconds();
            const TickType delta = current_ticks_ ? (new_ticks - current_ticks_) / interval_: 0;
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
            }
        }

        template<typename Value>
        inline void InternalInsert(const K& key, Value value, size_t ttl)
        {
            if (ttl > duration_) {
                throw std::invalid_argument("TTL is greater than max duration");
            } else if (ttl == 0) {
                ttl = duration_;
            }

            ttl /= interval_;

            Advance();
            const IndexType future_index = (bucket_index_ + ttl - 1) % total_buckets_;

            buckets_[future_index].push_back(key);
            cache_[key] = std::make_shared<T>(value);
        }

      protected:
        /// The duration in ticks of the entire queue.
        const size_t duration_;

        /// The interval at which buckets are cleared in ticks.
        const size_t interval_;

        /// The total amount of buckets. Value is calculated by duration / interval.
        const size_t total_buckets_;

        /// The index in time of the current bucket.
        IndexType bucket_index_{ 0 };

        /// Last calculated tick value.
        TickType current_ticks_{ 0 };

        /// The memory storage for all keys to be managed.
        std::vector<BucketType> buckets_;

        /// The cache of elements being stored.
        CacheType cache_;

        /// Tick service for calculating new tick and jumps in time.
        std::shared_ptr<TickService> tick_service_;
    };

} // namespace quicr
