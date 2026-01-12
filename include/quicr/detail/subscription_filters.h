// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "quicr/common.h"
#include "quicr/detail/ctrl_message_types.h"
#include "quicr/detail/uintvar.h"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <span>
#include <vector>

namespace quicr::filters {

// ============================================================================
// Filter Parameter Types (per MoQ Transport spec)
// ============================================================================

enum struct FilterParameterType : uint64_t
{
    kLocationFilter = 0x21,
    kGroupFilter = 0x23,
    kSubgroupFilter = 0x25,
    kObjectFilter = 0x27,
    kPriorityFilter = 0x29,
    kExtensionFilter = 0x2B,
    kTrackFilter = 0x2D,
};

// ============================================================================
// C++20 Concepts for Filter Framework
// ============================================================================

/**
 * @brief Concept for types that can be used as filter range bounds
 */
template<typename T>
concept RangeBound = std::integral<T> || std::same_as<T, uint8_t>;

// Extensions type used in filters - matches quicr::Extensions from object.h
using FilterExtensions = std::map<uint64_t, std::vector<std::vector<uint8_t>>>;

/**
 * @brief Object context passed to filters for evaluation
 */
struct ObjectContext
{
    uint64_t group_id{ 0 };
    uint64_t subgroup_id{ 0 };
    uint64_t object_id{ 0 };
    uint8_t priority{ 0 };
    const std::optional<FilterExtensions>* extensions{ nullptr };
    const std::optional<FilterExtensions>* immutable_extensions{ nullptr };

    constexpr ObjectContext() noexcept = default;

    constexpr ObjectContext(uint64_t group,
                            uint64_t subgroup,
                            uint64_t object,
                            uint8_t prio,
                            const std::optional<FilterExtensions>* ext = nullptr,
                            const std::optional<FilterExtensions>* immut_ext = nullptr) noexcept
      : group_id(group)
      , subgroup_id(subgroup)
      , object_id(object)
      , priority(prio)
      , extensions(ext)
      , immutable_extensions(immut_ext)
    {
    }
};

/**
 * @brief Concept for filter types that can evaluate objects
 */
template<typename F>
concept ObjectFilter = requires(const F& filter, const ObjectContext& ctx) {
    { filter.Matches(ctx) } -> std::same_as<bool>;
    { filter.IsEmpty() } -> std::same_as<bool>;
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Extract a uint64_t value from a byte vector
 *
 * Interprets up to 8 bytes as a little-endian uint64_t value.
 * Used for extracting extension header values for filtering.
 *
 * @param bytes The byte vector to extract from
 * @return The extracted uint64_t value
 */
inline uint64_t ExtractExtensionValue(const std::vector<uint8_t>& bytes)
{
    uint64_t value = 0;
    std::size_t bytes_to_copy = std::min(bytes.size(), sizeof(uint64_t));
    std::memcpy(&value, bytes.data(), bytes_to_copy);
    return value;
}

// ============================================================================
// Range: Core building block for range-based filters
// ============================================================================

/**
 * @brief Represents an inclusive range [start, end]
 * @tparam T The type of the range bounds
 */
template<RangeBound T>
struct Range
{
    T start{};
    std::optional<T> end; // nullopt means unbounded (no upper limit)

    constexpr Range() noexcept = default;

    constexpr explicit Range(T start_val) noexcept
      : start(start_val)
      , end(std::nullopt)
    {
    }

    constexpr Range(T start_val, T end_val) noexcept
      : start(start_val)
      , end(end_val)
    {
    }

    constexpr Range(T start_val, std::optional<T> end_val) noexcept
      : start(start_val)
      , end(end_val)
    {
    }

    /**
     * @brief Check if a value falls within this range
     */
    [[nodiscard]] constexpr bool Contains(T value) const noexcept
    {
        if (value < start) {
            return false;
        }
        if (end.has_value() && value > *end) {
            return false;
        }
        return true;
    }

    /**
     * @brief Check if range is valid (start <= end if end exists)
     */
    [[nodiscard]] constexpr bool IsValid() const noexcept { return !end.has_value() || start <= *end; }

    /**
     * @brief Check if this is an open-ended range (no upper bound)
     */
    [[nodiscard]] constexpr bool IsOpenEnded() const noexcept { return !end.has_value(); }

    bool operator==(const Range& other) const = default;
};

// ============================================================================
// RangeSet: Collection of ranges with efficient lookup
// ============================================================================

/**
 * @brief A set of ranges that can be queried for containment
 * @tparam T The type of the range bounds
 *
 * Optimized for:
 * - Fast containment checks via sorted ranges
 * - Memory efficiency with small vector optimization
 * - Zero allocation for small range sets
 */
template<RangeBound T>
class RangeSet
{
  public:
    using RangeType = Range<T>;
    static constexpr std::size_t kSmallSize = 4;

    RangeSet() = default;

    explicit RangeSet(std::vector<RangeType> ranges)
      : ranges_(std::move(ranges))
    {
        SortAndValidate();
    }

    RangeSet(std::initializer_list<RangeType> ranges)
      : ranges_(ranges)
    {
        SortAndValidate();
    }

    /**
     * @brief Add a range to the set
     */
    void Add(RangeType range)
    {
        if (range.IsValid()) {
            ranges_.push_back(range);
            sorted_ = false;
        }
    }

    /**
     * @brief Add a range defined by start and end values
     */
    void Add(T start, std::optional<T> end = std::nullopt) { Add(RangeType{ start, end }); }

    /**
     * @brief Check if a value is contained in any range
     */
    [[nodiscard]] bool Contains(T value) const
    {
        if (ranges_.empty()) {
            return true; // Empty filter means match all
        }

        EnsureSorted();

        // Binary search for potential containing range
        auto it = std::ranges::lower_bound(ranges_, value, {}, [](const RangeType& r) { return r.start; });

        // Check the range at or before the found position
        if (it != ranges_.begin()) {
            --it;
            if (it->Contains(value)) {
                return true;
            }
            ++it;
        }

        // Check ranges starting at or after value
        while (it != ranges_.end() && it->start <= value) {
            if (it->Contains(value)) {
                return true;
            }
            ++it;
        }

        return false;
    }

    /**
     * @brief Check if the range set is empty (matches everything)
     */
    [[nodiscard]] bool IsEmpty() const noexcept { return ranges_.empty(); }

    /**
     * @brief Get the number of ranges
     */
    [[nodiscard]] std::size_t Size() const noexcept { return ranges_.size(); }

    /**
     * @brief Clear all ranges
     */
    void Clear() noexcept
    {
        ranges_.clear();
        sorted_ = true;
    }

    /**
     * @brief Get read-only access to ranges
     */
    [[nodiscard]] std::span<const RangeType> GetRanges() const noexcept { return ranges_; }

    bool operator==(const RangeSet& other) const
    {
        EnsureSorted();
        other.EnsureSorted();
        return ranges_ == other.ranges_;
    }

  private:
    void SortAndValidate()
    {
        // Remove invalid ranges
        std::erase_if(ranges_, [](const RangeType& r) { return !r.IsValid(); });

        // Sort by start value
        std::ranges::sort(ranges_, {}, [](const RangeType& r) { return r.start; });

        sorted_ = true;
    }

    void EnsureSorted() const
    {
        if (!sorted_) {
            const_cast<RangeSet*>(this)->SortAndValidate();
        }
    }

    std::vector<RangeType> ranges_;
    mutable bool sorted_{ true };
};

// ============================================================================
// Location Filter
// ============================================================================

/**
 * @brief Filter by location (group_id, object_id) range
 *
 * Special location filters when start_group == 0:
 * - start_object == 0: Largest Object (start after largest observed)
 * - start_object omitted: Next Group Start
 */
class LocationFilter
{
  public:
    LocationFilter() = default;

    LocationFilter(messages::Location start, std::optional<messages::Location> end = std::nullopt)
      : start_(start)
      , end_(end)
    {
    }

    /**
     * @brief Create a "Largest Object" special filter
     */
    static LocationFilter LargestObject() noexcept { return LocationFilter{ { 0, 0 } }; }

    /**
     * @brief Create a "Next Group Start" special filter
     */
    static LocationFilter NextGroupStart() noexcept
    {
        LocationFilter filter;
        filter.start_ = messages::Location{ 0, 0 };
        filter.is_next_group_start_ = true;
        return filter;
    }

    [[nodiscard]] bool Matches(const ObjectContext& ctx) const noexcept
    {
        if (IsEmpty()) {
            return true;
        }

        messages::Location current{ ctx.group_id, ctx.object_id };

        // Check start bound
        if (current < start_) {
            return false;
        }

        // Check end bound if present
        if (end_.has_value() && current > *end_) {
            return false;
        }

        return true;
    }

    [[nodiscard]] bool IsEmpty() const noexcept { return start_.group == 0 && start_.object == 0 && !end_.has_value(); }

    [[nodiscard]] bool IsLargestObject() const noexcept
    {
        return start_.group == 0 && start_.object == 0 && !is_next_group_start_;
    }

    [[nodiscard]] bool IsNextGroupStart() const noexcept { return is_next_group_start_; }

    [[nodiscard]] const messages::Location& GetStart() const noexcept { return start_; }
    [[nodiscard]] const std::optional<messages::Location>& GetEnd() const noexcept { return end_; }

    void SetStart(messages::Location loc) noexcept { start_ = loc; }
    void SetEnd(std::optional<messages::Location> loc) noexcept { end_ = loc; }

    bool operator==(const LocationFilter& other) const = default;

  private:
    messages::Location start_{ 0, 0 };
    std::optional<messages::Location> end_;
    bool is_next_group_start_{ false };
};

// ============================================================================
// Group Filter
// ============================================================================

/**
 * @brief Filter objects by group ID ranges
 */
class GroupFilter
{
  public:
    using RangeType = Range<uint64_t>;

    GroupFilter() = default;

    explicit GroupFilter(RangeSet<uint64_t> ranges)
      : ranges_(std::move(ranges))
    {
    }

    GroupFilter(std::initializer_list<RangeType> ranges)
      : ranges_(ranges)
    {
    }

    [[nodiscard]] bool Matches(const ObjectContext& ctx) const { return ranges_.Contains(ctx.group_id); }

    [[nodiscard]] bool IsEmpty() const noexcept { return ranges_.IsEmpty(); }

    void AddRange(uint64_t start, std::optional<uint64_t> end = std::nullopt) { ranges_.Add(start, end); }

    [[nodiscard]] const RangeSet<uint64_t>& GetRanges() const noexcept { return ranges_; }

    bool operator==(const GroupFilter& other) const = default;

  private:
    RangeSet<uint64_t> ranges_;
};

// ============================================================================
// Subgroup Filter
// ============================================================================

/**
 * @brief Filter objects by subgroup ID ranges
 */
class SubgroupFilter
{
  public:
    using RangeType = Range<uint64_t>;

    SubgroupFilter() = default;

    explicit SubgroupFilter(RangeSet<uint64_t> ranges)
      : ranges_(std::move(ranges))
    {
    }

    SubgroupFilter(std::initializer_list<RangeType> ranges)
      : ranges_(ranges)
    {
    }

    [[nodiscard]] bool Matches(const ObjectContext& ctx) const { return ranges_.Contains(ctx.subgroup_id); }

    [[nodiscard]] bool IsEmpty() const noexcept { return ranges_.IsEmpty(); }

    void AddRange(uint64_t start, std::optional<uint64_t> end = std::nullopt) { ranges_.Add(start, end); }

    [[nodiscard]] const RangeSet<uint64_t>& GetRanges() const noexcept { return ranges_; }

    bool operator==(const SubgroupFilter& other) const = default;

  private:
    RangeSet<uint64_t> ranges_;
};

// ============================================================================
// Object Filter
// ============================================================================

/**
 * @brief Filter objects by object ID ranges
 */
class ObjectIdFilter
{
  public:
    using RangeType = Range<uint64_t>;

    ObjectIdFilter() = default;

    explicit ObjectIdFilter(RangeSet<uint64_t> ranges)
      : ranges_(std::move(ranges))
    {
    }

    ObjectIdFilter(std::initializer_list<RangeType> ranges)
      : ranges_(ranges)
    {
    }

    [[nodiscard]] bool Matches(const ObjectContext& ctx) const { return ranges_.Contains(ctx.object_id); }

    [[nodiscard]] bool IsEmpty() const noexcept { return ranges_.IsEmpty(); }

    void AddRange(uint64_t start, std::optional<uint64_t> end = std::nullopt) { ranges_.Add(start, end); }

    [[nodiscard]] const RangeSet<uint64_t>& GetRanges() const noexcept { return ranges_; }

    bool operator==(const ObjectIdFilter& other) const = default;

  private:
    RangeSet<uint64_t> ranges_;
};

// ============================================================================
// Priority Filter
// ============================================================================

/**
 * @brief Filter objects by publisher priority ranges
 */
class PriorityFilter
{
  public:
    using RangeType = Range<uint8_t>;

    PriorityFilter() = default;

    explicit PriorityFilter(RangeSet<uint8_t> ranges)
      : ranges_(std::move(ranges))
    {
    }

    PriorityFilter(std::initializer_list<RangeType> ranges)
      : ranges_(ranges)
    {
    }

    [[nodiscard]] bool Matches(const ObjectContext& ctx) const { return ranges_.Contains(ctx.priority); }

    [[nodiscard]] bool IsEmpty() const noexcept { return ranges_.IsEmpty(); }

    void AddRange(uint8_t start, std::optional<uint8_t> end = std::nullopt) { ranges_.Add(start, end); }

    [[nodiscard]] const RangeSet<uint8_t>& GetRanges() const noexcept { return ranges_; }

    bool operator==(const PriorityFilter& other) const = default;

  private:
    RangeSet<uint8_t> ranges_;
};

// ============================================================================
// Extension Filter
// ============================================================================

/**
 * @brief A single extension type filter with value ranges
 */
struct ExtensionTypeFilter
{
    uint64_t extension_type{ 0 };
    RangeSet<uint64_t> value_ranges;

    ExtensionTypeFilter() = default;

    ExtensionTypeFilter(uint64_t type, RangeSet<uint64_t> ranges)
      : extension_type(type)
      , value_ranges(std::move(ranges))
    {
    }

    [[nodiscard]] bool Matches(const ObjectContext& ctx) const
    {
        if (value_ranges.IsEmpty()) {
            return true;
        }

        // Check in extensions
        if (ctx.extensions && ctx.extensions->has_value()) {
            auto it = (*ctx.extensions)->find(extension_type);
            if (it != (*ctx.extensions)->end() && !it->second.empty()) {
                // Get numeric value from first entry
                uint64_t value = ExtractExtensionValue(it->second[0]);
                if (value_ranges.Contains(value)) {
                    return true;
                }
            }
        }

        // Check in immutable extensions
        if (ctx.immutable_extensions && ctx.immutable_extensions->has_value()) {
            auto it = (*ctx.immutable_extensions)->find(extension_type);
            if (it != (*ctx.immutable_extensions)->end() && !it->second.empty()) {
                uint64_t value = ExtractExtensionValue(it->second[0]);
                if (value_ranges.Contains(value)) {
                    return true;
                }
            }
        }

        return false;
    }

    bool operator==(const ExtensionTypeFilter& other) const = default;
};

/**
 * @brief Filter objects by extension header values
 */
class ExtensionFilter
{
  public:
    ExtensionFilter() = default;

    explicit ExtensionFilter(std::vector<ExtensionTypeFilter> filters)
      : type_filters_(std::move(filters))
    {
    }

    [[nodiscard]] bool Matches(const ObjectContext& ctx) const
    {
        if (type_filters_.empty()) {
            return true;
        }

        // All extension type filters must match (AND semantics)
        return std::ranges::all_of(type_filters_, [&ctx](const auto& f) { return f.Matches(ctx); });
    }

    [[nodiscard]] bool IsEmpty() const noexcept { return type_filters_.empty(); }

    void AddTypeFilter(ExtensionTypeFilter filter) { type_filters_.push_back(std::move(filter)); }

    void AddTypeFilter(uint64_t extension_type, RangeSet<uint64_t> ranges)
    {
        type_filters_.emplace_back(extension_type, std::move(ranges));
    }

    [[nodiscard]] const std::vector<ExtensionTypeFilter>& GetTypeFilters() const noexcept { return type_filters_; }

    bool operator==(const ExtensionFilter& other) const = default;

  private:
    std::vector<ExtensionTypeFilter> type_filters_;
};

// ============================================================================
// Track Filter (for namespace subscriptions)
// ============================================================================

/**
 * @brief Track selection state for TrackFilter
 */
struct TrackSelectionState
{
    uint64_t highest_extension_value{ 0 };
    std::chrono::steady_clock::time_point last_object_time{};
    bool is_selected{ false };
};

/**
 * @brief Track filter configuration (copyable, stateless)
 *
 * Defines the parameters for track selection in namespace subscriptions.
 * The actual track state is maintained separately.
 */
struct TrackFilterConfig
{
    uint64_t extension_type{ 0 };
    uint64_t max_tracks_selected{ 0 };
    uint64_t max_tracks_deselected{ 0 };
    std::chrono::milliseconds max_time_selected{ 0 };

    TrackFilterConfig() = default;

    TrackFilterConfig(uint64_t ext_type, uint64_t max_selected, uint64_t max_deselected, uint64_t max_time_ms)
      : extension_type(ext_type)
      , max_tracks_selected(max_selected)
      , max_tracks_deselected(max_deselected)
      , max_time_selected(std::chrono::milliseconds(max_time_ms))
    {
    }

    [[nodiscard]] bool IsEmpty() const noexcept { return max_tracks_selected == 0; }

    bool operator==(const TrackFilterConfig& other) const = default;
};

/**
 * @brief Filter for selecting tracks based on extension header values
 *
 * Used in SUBSCRIBE_NAMESPACE to select tracks with highest extension values.
 * Thread-safe for concurrent access. Uses shared state for track management.
 */
class TrackFilter
{
  public:
    TrackFilter() = default;

    TrackFilter(uint64_t extension_type,
                uint64_t max_tracks_selected,
                uint64_t max_tracks_deselected,
                uint64_t max_time_selected_ms)
      : config_(extension_type, max_tracks_selected, max_tracks_deselected, max_time_selected_ms)
      , state_(std::make_shared<State>())
    {
    }

    explicit TrackFilter(TrackFilterConfig config)
      : config_(std::move(config))
      , state_(std::make_shared<State>())
    {
    }

    // Copy constructor - shares state
    TrackFilter(const TrackFilter& other)
      : config_(other.config_)
      , state_(other.state_ ? other.state_ : std::make_shared<State>())
    {
    }

    // Copy assignment - shares state
    TrackFilter& operator=(const TrackFilter& other)
    {
        if (this != &other) {
            config_ = other.config_;
            state_ = other.state_ ? other.state_ : std::make_shared<State>();
        }
        return *this;
    }

    // Move constructor
    TrackFilter(TrackFilter&& other) noexcept = default;

    // Move assignment
    TrackFilter& operator=(TrackFilter&& other) noexcept = default;

    /**
     * @brief Evaluate if a track should be selected based on object delivery
     * @param track_id Unique identifier for the track
     * @param ctx Object context with extension values
     * @return true if the track is selected after this object
     */
    [[nodiscard]] bool EvaluateTrackSelection(uint64_t track_id, const ObjectContext& ctx)
    {
        if (!state_) {
            state_ = std::make_shared<State>();
        }

        std::unique_lock lock(state_->mutex);

        // Get extension value from object
        uint64_t ext_value = GetExtensionValue(ctx);

        auto now = std::chrono::steady_clock::now();

        // Update or create track state
        auto& track_state = state_->track_states[track_id];
        track_state.highest_extension_value = std::max(track_state.highest_extension_value, ext_value);
        track_state.last_object_time = now;

        // Expire old selections
        ExpireStaleSelections(now);

        // Recalculate selections
        RecalculateSelections();

        return track_state.is_selected;
    }

    /**
     * @brief Check if a specific track is currently selected
     */
    [[nodiscard]] bool IsTrackSelected(uint64_t track_id) const
    {
        if (!state_) {
            return false;
        }

        std::shared_lock lock(state_->mutex);
        auto it = state_->track_states.find(track_id);
        return it != state_->track_states.end() && it->second.is_selected;
    }

    /**
     * @brief Get the number of currently selected tracks
     */
    [[nodiscard]] uint64_t GetSelectedTrackCount() const
    {
        if (!state_) {
            return 0;
        }

        std::shared_lock lock(state_->mutex);
        return std::ranges::count_if(state_->track_states, [](const auto& pair) { return pair.second.is_selected; });
    }

    [[nodiscard]] bool IsEmpty() const noexcept { return config_.IsEmpty(); }

    [[nodiscard]] uint64_t GetExtensionType() const noexcept { return config_.extension_type; }
    [[nodiscard]] uint64_t GetMaxTracksSelected() const noexcept { return config_.max_tracks_selected; }
    [[nodiscard]] uint64_t GetMaxTracksDeselected() const noexcept { return config_.max_tracks_deselected; }
    [[nodiscard]] std::chrono::milliseconds GetMaxTimeSelected() const noexcept { return config_.max_time_selected; }
    [[nodiscard]] const TrackFilterConfig& GetConfig() const noexcept { return config_; }

    bool operator==(const TrackFilter& other) const { return config_ == other.config_; }

  private:
    struct State
    {
        mutable std::shared_mutex mutex;
        std::map<uint64_t, TrackSelectionState> track_states;
    };

    uint64_t GetExtensionValue(const ObjectContext& ctx) const
    {
        // Check extensions for the configured extension type
        if (ctx.extensions && ctx.extensions->has_value()) {
            auto it = (*ctx.extensions)->find(config_.extension_type);
            if (it != (*ctx.extensions)->end() && !it->second.empty()) {
                return ExtractExtensionValue(it->second[0]);
            }
        }

        if (ctx.immutable_extensions && ctx.immutable_extensions->has_value()) {
            auto it = (*ctx.immutable_extensions)->find(config_.extension_type);
            if (it != (*ctx.immutable_extensions)->end() && !it->second.empty()) {
                return ExtractExtensionValue(it->second[0]);
            }
        }

        return 0;
    }

    void ExpireStaleSelections(std::chrono::steady_clock::time_point now)
    {
        if (config_.max_time_selected.count() == 0 || !state_) {
            return;
        }

        for (auto& [id, track_state] : state_->track_states) {
            if (track_state.is_selected) {
                auto elapsed = now - track_state.last_object_time;
                if (elapsed > config_.max_time_selected) {
                    track_state.is_selected = false;
                }
            }
        }
    }

    void RecalculateSelections()
    {
        if (!state_) {
            return;
        }

        // Collect all tracks and sort by extension value (descending)
        std::vector<std::pair<uint64_t, TrackSelectionState*>> tracks;
        tracks.reserve(state_->track_states.size());

        for (auto& [id, track_state] : state_->track_states) {
            tracks.emplace_back(id, &track_state);
        }

        std::ranges::sort(tracks, [](const auto& a, const auto& b) {
            return a.second->highest_extension_value > b.second->highest_extension_value;
        });

        // Select top N tracks
        uint64_t selected_count = 0;
        for (auto& [id, state_ptr] : tracks) {
            if (selected_count < config_.max_tracks_selected) {
                state_ptr->is_selected = true;
                ++selected_count;
            } else {
                state_ptr->is_selected = false;
            }
        }

        // Trim deselected track list if needed
        if (state_->track_states.size() > config_.max_tracks_selected + config_.max_tracks_deselected) {
            // Remove oldest deselected tracks
            std::vector<uint64_t> to_remove;
            for (const auto& [id, track_state] : state_->track_states) {
                if (!track_state.is_selected) {
                    to_remove.push_back(id);
                }
            }

            // Sort by last object time and remove oldest
            std::ranges::sort(to_remove, [this](uint64_t a, uint64_t b) {
                return state_->track_states[a].last_object_time < state_->track_states[b].last_object_time;
            });

            std::size_t excess = state_->track_states.size() - (config_.max_tracks_selected + config_.max_tracks_deselected);
            for (std::size_t i = 0; i < excess && i < to_remove.size(); ++i) {
                state_->track_states.erase(to_remove[i]);
            }
        }
    }

    TrackFilterConfig config_;
    mutable std::shared_ptr<State> state_;
};

// ============================================================================
// Composite Subscription Filter
// ============================================================================

/**
 * @brief Composite filter combining all filter types for a subscription
 *
 * Filters are evaluated in order:
 * 1. Object-level filters (AND of all): Group, Subgroup, Object, Priority, Extension, Location
 * 2. Track filter (if namespace subscription)
 *
 * Empty filters match all objects.
 */
class SubscriptionFilter
{
  public:
    SubscriptionFilter() = default;

    /**
     * @brief Evaluate if an object passes all filters
     *
     * Object filters are evaluated first, then track filter.
     * All filters use AND semantics.
     *
     * @param ctx Object context to evaluate
     * @return true if object passes all filters
     */
    [[nodiscard]] bool Matches(const ObjectContext& ctx) const
    {
        // Evaluate object-level filters first (order: most selective first)

        // Priority filter (cheap check, 8-bit comparison)
        if (!priority_filter_.IsEmpty() && !priority_filter_.Matches(ctx)) {
            return false;
        }

        // Group filter
        if (!group_filter_.IsEmpty() && !group_filter_.Matches(ctx)) {
            return false;
        }

        // Object ID filter
        if (!object_filter_.IsEmpty() && !object_filter_.Matches(ctx)) {
            return false;
        }

        // Subgroup filter
        if (!subgroup_filter_.IsEmpty() && !subgroup_filter_.Matches(ctx)) {
            return false;
        }

        // Location filter
        if (!location_filter_.IsEmpty() && !location_filter_.Matches(ctx)) {
            return false;
        }

        // Extension filter (potentially expensive, check last)
        if (!extension_filter_.IsEmpty() && !extension_filter_.Matches(ctx)) {
            return false;
        }

        return true;
    }

    /**
     * @brief Evaluate if a track is selected (for namespace subscriptions)
     *
     * @param track_id Unique track identifier
     * @param ctx Object context for the track
     * @return true if track is selected
     */
    [[nodiscard]] bool EvaluateTrack(uint64_t track_id, const ObjectContext& ctx)
    {
        if (track_filter_.IsEmpty()) {
            return true;
        }
        return track_filter_.EvaluateTrackSelection(track_id, ctx);
    }

    /**
     * @brief Full evaluation: object filters AND track filter
     */
    [[nodiscard]] bool FullMatch(uint64_t track_id, const ObjectContext& ctx)
    {
        // Object filters first
        if (!Matches(ctx)) {
            return false;
        }

        // Then track filter
        return EvaluateTrack(track_id, ctx);
    }

    /**
     * @brief Check if all filters are empty (match everything)
     */
    [[nodiscard]] bool IsEmpty() const noexcept
    {
        return location_filter_.IsEmpty() && group_filter_.IsEmpty() && subgroup_filter_.IsEmpty() &&
               object_filter_.IsEmpty() && priority_filter_.IsEmpty() && extension_filter_.IsEmpty() &&
               track_filter_.IsEmpty();
    }

    // Setters
    void SetLocationFilter(LocationFilter filter) { location_filter_ = std::move(filter); }
    void SetGroupFilter(GroupFilter filter) { group_filter_ = std::move(filter); }
    void SetSubgroupFilter(SubgroupFilter filter) { subgroup_filter_ = std::move(filter); }
    void SetObjectFilter(ObjectIdFilter filter) { object_filter_ = std::move(filter); }
    void SetPriorityFilter(PriorityFilter filter) { priority_filter_ = std::move(filter); }
    void SetExtensionFilter(ExtensionFilter filter) { extension_filter_ = std::move(filter); }
    void SetTrackFilter(TrackFilter filter) { track_filter_ = std::move(filter); }

    // Getters
    [[nodiscard]] const LocationFilter& GetLocationFilter() const noexcept { return location_filter_; }
    [[nodiscard]] const GroupFilter& GetGroupFilter() const noexcept { return group_filter_; }
    [[nodiscard]] const SubgroupFilter& GetSubgroupFilter() const noexcept { return subgroup_filter_; }
    [[nodiscard]] const ObjectIdFilter& GetObjectFilter() const noexcept { return object_filter_; }
    [[nodiscard]] const PriorityFilter& GetPriorityFilter() const noexcept { return priority_filter_; }
    [[nodiscard]] const ExtensionFilter& GetExtensionFilter() const noexcept { return extension_filter_; }
    [[nodiscard]] const TrackFilter& GetTrackFilter() const noexcept { return track_filter_; }

    // Mutable getters for modification
    [[nodiscard]] LocationFilter& GetLocationFilter() noexcept { return location_filter_; }
    [[nodiscard]] GroupFilter& GetGroupFilter() noexcept { return group_filter_; }
    [[nodiscard]] SubgroupFilter& GetSubgroupFilter() noexcept { return subgroup_filter_; }
    [[nodiscard]] ObjectIdFilter& GetObjectFilter() noexcept { return object_filter_; }
    [[nodiscard]] PriorityFilter& GetPriorityFilter() noexcept { return priority_filter_; }
    [[nodiscard]] ExtensionFilter& GetExtensionFilter() noexcept { return extension_filter_; }
    [[nodiscard]] TrackFilter& GetTrackFilter() noexcept { return track_filter_; }

  private:
    LocationFilter location_filter_;
    GroupFilter group_filter_;
    SubgroupFilter subgroup_filter_;
    ObjectIdFilter object_filter_;
    PriorityFilter priority_filter_;
    ExtensionFilter extension_filter_;
    TrackFilter track_filter_;
};

// ============================================================================
// Wire Format Serialization
// ============================================================================

// Forward declarations for serialization
Bytes& operator<<(Bytes& buffer, const LocationFilter& filter);
Bytes& operator<<(Bytes& buffer, const GroupFilter& filter);
Bytes& operator<<(Bytes& buffer, const SubgroupFilter& filter);
Bytes& operator<<(Bytes& buffer, const ObjectIdFilter& filter);
Bytes& operator<<(Bytes& buffer, const PriorityFilter& filter);
Bytes& operator<<(Bytes& buffer, const ExtensionFilter& filter);
Bytes& operator<<(Bytes& buffer, const TrackFilter& filter);

BytesSpan operator>>(BytesSpan buffer, LocationFilter& filter);
BytesSpan operator>>(BytesSpan buffer, GroupFilter& filter);
BytesSpan operator>>(BytesSpan buffer, SubgroupFilter& filter);
BytesSpan operator>>(BytesSpan buffer, ObjectIdFilter& filter);
BytesSpan operator>>(BytesSpan buffer, PriorityFilter& filter);
BytesSpan operator>>(BytesSpan buffer, ExtensionFilter& filter);
BytesSpan operator>>(BytesSpan buffer, TrackFilter& filter);

// ============================================================================
// Parameter Conversion Helpers
// ============================================================================

/**
 * @brief Convert a SubscriptionFilter to Parameters for wire encoding
 *
 * Adds filter parameters to the parameter list for Subscribe/SubscribeUpdate messages.
 *
 * @param filter The subscription filter to convert
 * @param params The parameter list to add to (will be appended)
 */
void AppendFilterParameters(const SubscriptionFilter& filter, std::vector<messages::Parameter>& params);

/**
 * @brief Parse filter parameters from a parameter list and apply to SubscriptionFilter
 *
 * Extracts filter parameters from Subscribe/SubscribeUpdate message parameters.
 *
 * @param params The parameter list to parse
 * @param filter The subscription filter to populate
 */
void ParseFilterParameters(const std::vector<messages::Parameter>& params, SubscriptionFilter& filter);

/**
 * @brief Create a SubscriptionFilter from parameter list
 *
 * @param params The parameter list to parse
 * @return Populated SubscriptionFilter
 */
[[nodiscard]] SubscriptionFilter CreateFilterFromParameters(const std::vector<messages::Parameter>& params);

} // namespace quicr::filters
