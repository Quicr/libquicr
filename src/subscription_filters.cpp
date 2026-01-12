// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/subscription_filters.h"

#include "quicr/detail/uintvar.h"

namespace quicr::filters {

// ============================================================================
// Helper Functions
// ============================================================================

namespace {

// Helper to serialize UintVar to buffer
inline void SerializeUintVar(Bytes& buffer, uint64_t value)
{
    UintVar var(value);
    buffer.insert(buffer.end(), var.begin(), var.end());
}

/**
 * @brief Serialize a uint64_t range to wire format
 */
void SerializeRange(Bytes& buffer, const Range<uint64_t>& range)
{
    SerializeUintVar(buffer, range.start);
    if (range.end.has_value()) {
        SerializeUintVar(buffer, *range.end);
    }
}

/**
 * @brief Serialize a uint8_t range to wire format
 */
void SerializeRange(Bytes& buffer, const Range<uint8_t>& range)
{
    buffer.push_back(range.start);
    if (range.end.has_value()) {
        buffer.push_back(*range.end);
    }
}

/**
 * @brief Deserialize a uint64_t value from buffer
 */
BytesSpan DeserializeUint64(BytesSpan buffer, uint64_t& value)
{
    if (buffer.empty()) {
        throw std::runtime_error("Buffer too small for uint64_t");
    }
    UintVar var(buffer);
    value = var.Get();
    return buffer.subspan(var.size());
}

/**
 * @brief Deserialize a uint8_t value from buffer
 */
BytesSpan DeserializeUint8(BytesSpan buffer, uint8_t& value)
{
    if (buffer.empty()) {
        throw std::runtime_error("Buffer too small for uint8_t");
    }
    value = buffer[0];
    return buffer.subspan(1);
}

} // anonymous namespace

// ============================================================================
// LocationFilter Serialization
// ============================================================================

Bytes& operator<<(Bytes& buffer, const LocationFilter& filter)
{
    // Serialize as parameter type + length + content
    SerializeUintVar(buffer, static_cast<uint64_t>(FilterParameterType::kLocationFilter));

    // Calculate content
    Bytes content;
    SerializeUintVar(content, filter.GetStart().group);
    SerializeUintVar(content, filter.GetStart().object);

    if (filter.GetEnd().has_value()) {
        SerializeUintVar(content, filter.GetEnd()->group);
        SerializeUintVar(content, filter.GetEnd()->object);
    }

    // Write length and content
    SerializeUintVar(buffer, content.size());
    buffer.insert(buffer.end(), content.begin(), content.end());

    return buffer;
}

BytesSpan operator>>(BytesSpan buffer, LocationFilter& filter)
{
    if (buffer.empty()) {
        return buffer;
    }

    // Read length
    UintVar length_var(buffer);
    buffer = buffer.subspan(length_var.size());
    uint64_t length = length_var.Get();

    if (length == 0) {
        // Empty filter
        filter = LocationFilter{};
        return buffer;
    }

    if (buffer.size() < length) {
        throw std::runtime_error("Buffer too small for LocationFilter");
    }

    auto content = buffer.subspan(0, length);

    // Read start group
    uint64_t start_group = 0;
    content = DeserializeUint64(content, start_group);

    // Read start object (if present)
    uint64_t start_object = 0;
    if (!content.empty()) {
        content = DeserializeUint64(content, start_object);
    } else {
        // Only start_group present -> NextGroupStart special filter
        filter = LocationFilter::NextGroupStart();
        filter.SetStart({ start_group, 0 });
        return buffer.subspan(length);
    }

    messages::Location start{ start_group, start_object };

    // Read end location (if present)
    if (!content.empty()) {
        uint64_t end_group = 0;
        content = DeserializeUint64(content, end_group);

        uint64_t end_object = 0;
        if (!content.empty()) {
            content = DeserializeUint64(content, end_object);
        }

        filter = LocationFilter{ start, messages::Location{ end_group, end_object } };
    } else {
        filter = LocationFilter{ start };
    }

    return buffer.subspan(length);
}

// ============================================================================
// GroupFilter Serialization
// ============================================================================

Bytes& operator<<(Bytes& buffer, const GroupFilter& filter)
{
    SerializeUintVar(buffer, static_cast<uint64_t>(FilterParameterType::kGroupFilter));

    Bytes content;
    for (const auto& range : filter.GetRanges().GetRanges()) {
        SerializeRange(content, range);
    }

    SerializeUintVar(buffer, content.size());
    buffer.insert(buffer.end(), content.begin(), content.end());

    return buffer;
}

BytesSpan operator>>(BytesSpan buffer, GroupFilter& filter)
{
    if (buffer.empty()) {
        return buffer;
    }

    UintVar length_var(buffer);
    buffer = buffer.subspan(length_var.size());
    uint64_t length = length_var.Get();

    if (length == 0) {
        filter = GroupFilter{};
        return buffer;
    }

    if (buffer.size() < length) {
        throw std::runtime_error("Buffer too small for GroupFilter");
    }

    auto content = buffer.subspan(0, length);
    RangeSet<uint64_t> ranges;

    while (!content.empty()) {
        uint64_t start = 0;
        content = DeserializeUint64(content, start);

        std::optional<uint64_t> end;
        if (!content.empty()) {
            uint64_t maybe_end = 0;
            auto next = DeserializeUint64(content, maybe_end);
            end = maybe_end;
            content = next;
        }

        ranges.Add(start, end);
    }

    filter = GroupFilter{ std::move(ranges) };
    return buffer.subspan(length);
}

// ============================================================================
// SubgroupFilter Serialization
// ============================================================================

Bytes& operator<<(Bytes& buffer, const SubgroupFilter& filter)
{
    SerializeUintVar(buffer, static_cast<uint64_t>(FilterParameterType::kSubgroupFilter));

    Bytes content;
    for (const auto& range : filter.GetRanges().GetRanges()) {
        SerializeRange(content, range);
    }

    SerializeUintVar(buffer, content.size());
    buffer.insert(buffer.end(), content.begin(), content.end());

    return buffer;
}

BytesSpan operator>>(BytesSpan buffer, SubgroupFilter& filter)
{
    if (buffer.empty()) {
        return buffer;
    }

    UintVar length_var(buffer);
    buffer = buffer.subspan(length_var.size());
    uint64_t length = length_var.Get();

    if (length == 0) {
        filter = SubgroupFilter{};
        return buffer;
    }

    if (buffer.size() < length) {
        throw std::runtime_error("Buffer too small for SubgroupFilter");
    }

    auto content = buffer.subspan(0, length);
    RangeSet<uint64_t> ranges;

    while (!content.empty()) {
        uint64_t start = 0;
        content = DeserializeUint64(content, start);

        std::optional<uint64_t> end;
        if (!content.empty()) {
            uint64_t maybe_end = 0;
            auto next = DeserializeUint64(content, maybe_end);
            end = maybe_end;
            content = next;
        }

        ranges.Add(start, end);
    }

    filter = SubgroupFilter{ std::move(ranges) };
    return buffer.subspan(length);
}

// ============================================================================
// ObjectIdFilter Serialization
// ============================================================================

Bytes& operator<<(Bytes& buffer, const ObjectIdFilter& filter)
{
    SerializeUintVar(buffer, static_cast<uint64_t>(FilterParameterType::kObjectFilter));

    Bytes content;
    for (const auto& range : filter.GetRanges().GetRanges()) {
        SerializeRange(content, range);
    }

    SerializeUintVar(buffer, content.size());
    buffer.insert(buffer.end(), content.begin(), content.end());

    return buffer;
}

BytesSpan operator>>(BytesSpan buffer, ObjectIdFilter& filter)
{
    if (buffer.empty()) {
        return buffer;
    }

    UintVar length_var(buffer);
    buffer = buffer.subspan(length_var.size());
    uint64_t length = length_var.Get();

    if (length == 0) {
        filter = ObjectIdFilter{};
        return buffer;
    }

    if (buffer.size() < length) {
        throw std::runtime_error("Buffer too small for ObjectIdFilter");
    }

    auto content = buffer.subspan(0, length);
    RangeSet<uint64_t> ranges;

    while (!content.empty()) {
        uint64_t start = 0;
        content = DeserializeUint64(content, start);

        std::optional<uint64_t> end;
        if (!content.empty()) {
            uint64_t maybe_end = 0;
            auto next = DeserializeUint64(content, maybe_end);
            end = maybe_end;
            content = next;
        }

        ranges.Add(start, end);
    }

    filter = ObjectIdFilter{ std::move(ranges) };
    return buffer.subspan(length);
}

// ============================================================================
// PriorityFilter Serialization
// ============================================================================

Bytes& operator<<(Bytes& buffer, const PriorityFilter& filter)
{
    SerializeUintVar(buffer, static_cast<uint64_t>(FilterParameterType::kPriorityFilter));

    Bytes content;
    for (const auto& range : filter.GetRanges().GetRanges()) {
        SerializeRange(content, range);
    }

    SerializeUintVar(buffer, content.size());
    buffer.insert(buffer.end(), content.begin(), content.end());

    return buffer;
}

BytesSpan operator>>(BytesSpan buffer, PriorityFilter& filter)
{
    if (buffer.empty()) {
        return buffer;
    }

    UintVar length_var(buffer);
    buffer = buffer.subspan(length_var.size());
    uint64_t length = length_var.Get();

    if (length == 0) {
        filter = PriorityFilter{};
        return buffer;
    }

    if (buffer.size() < length) {
        throw std::runtime_error("Buffer too small for PriorityFilter");
    }

    auto content = buffer.subspan(0, length);
    RangeSet<uint8_t> ranges;

    while (!content.empty()) {
        uint8_t start = 0;
        content = DeserializeUint8(content, start);

        std::optional<uint8_t> end;
        if (!content.empty()) {
            uint8_t maybe_end = 0;
            content = DeserializeUint8(content, maybe_end);
            end = maybe_end;
        }

        ranges.Add(start, end);
    }

    filter = PriorityFilter{ std::move(ranges) };
    return buffer.subspan(length);
}

// ============================================================================
// ExtensionFilter Serialization
// ============================================================================

Bytes& operator<<(Bytes& buffer, const ExtensionFilter& filter)
{
    SerializeUintVar(buffer, static_cast<uint64_t>(FilterParameterType::kExtensionFilter));

    Bytes content;
    for (const auto& type_filter : filter.GetTypeFilters()) {
        SerializeUintVar(content, type_filter.extension_type);

        for (const auto& range : type_filter.value_ranges.GetRanges()) {
            SerializeUintVar(content, range.start);
            if (range.end.has_value()) {
                SerializeUintVar(content, *range.end);
            } else {
                // End=0 indicates no end if Start>0
                if (range.start > 0) {
                    SerializeUintVar(content, 0);
                }
            }
        }
    }

    SerializeUintVar(buffer, content.size());
    buffer.insert(buffer.end(), content.begin(), content.end());

    return buffer;
}

BytesSpan operator>>(BytesSpan buffer, ExtensionFilter& filter)
{
    if (buffer.empty()) {
        return buffer;
    }

    UintVar length_var(buffer);
    buffer = buffer.subspan(length_var.size());
    uint64_t length = length_var.Get();

    if (length == 0) {
        filter = ExtensionFilter{};
        return buffer;
    }

    if (buffer.size() < length) {
        throw std::runtime_error("Buffer too small for ExtensionFilter");
    }

    auto content = buffer.subspan(0, length);
    std::vector<ExtensionTypeFilter> type_filters;

    while (!content.empty()) {
        uint64_t ext_type = 0;
        content = DeserializeUint64(content, ext_type);

        RangeSet<uint64_t> value_ranges;

        // Read start value
        if (!content.empty()) {
            uint64_t start = 0;
            content = DeserializeUint64(content, start);

            std::optional<uint64_t> end;
            if (!content.empty()) {
                uint64_t end_val = 0;
                content = DeserializeUint64(content, end_val);
                // End=0 means no upper limit if start > 0
                if (end_val != 0 || start == 0) {
                    end = end_val;
                }
            }

            value_ranges.Add(start, end);
        }

        type_filters.emplace_back(ext_type, std::move(value_ranges));
    }

    filter = ExtensionFilter{ std::move(type_filters) };
    return buffer.subspan(length);
}

// ============================================================================
// TrackFilter Serialization
// ============================================================================

Bytes& operator<<(Bytes& buffer, const TrackFilter& filter)
{
    SerializeUintVar(buffer, static_cast<uint64_t>(FilterParameterType::kTrackFilter));

    Bytes content;
    SerializeUintVar(content, filter.GetExtensionType());
    SerializeUintVar(content, filter.GetMaxTracksSelected());
    SerializeUintVar(content, filter.GetMaxTracksDeselected());
    SerializeUintVar(content, static_cast<uint64_t>(filter.GetMaxTimeSelected().count()));

    SerializeUintVar(buffer, content.size());
    buffer.insert(buffer.end(), content.begin(), content.end());

    return buffer;
}

BytesSpan operator>>(BytesSpan buffer, TrackFilter& filter)
{
    if (buffer.empty()) {
        return buffer;
    }

    UintVar length_var(buffer);
    buffer = buffer.subspan(length_var.size());
    uint64_t length = length_var.Get();

    if (length == 0) {
        filter = TrackFilter{};
        return buffer;
    }

    if (buffer.size() < length) {
        throw std::runtime_error("Buffer too small for TrackFilter");
    }

    auto content = buffer.subspan(0, length);

    uint64_t ext_type = 0;
    content = DeserializeUint64(content, ext_type);

    uint64_t max_selected = 0;
    content = DeserializeUint64(content, max_selected);

    uint64_t max_deselected = 0;
    content = DeserializeUint64(content, max_deselected);

    uint64_t max_time_ms = 0;
    if (!content.empty()) {
        content = DeserializeUint64(content, max_time_ms);
    }

    filter = TrackFilter{ ext_type, max_selected, max_deselected, max_time_ms };
    return buffer.subspan(length);
}

// ============================================================================
// Parameter Conversion Helpers
// ============================================================================

namespace {

// Helper to create a parameter with serialized filter content
messages::Parameter CreateFilterParameter(messages::ParameterType type, const Bytes& content)
{
    return messages::Parameter{ type, content };
}

// Helper to serialize a filter into bytes
template<typename FilterT>
Bytes SerializeFilter(const FilterT& filter)
{
    Bytes content;
    content << filter;
    // Skip the parameter type that was written by operator<<
    // The parameter type is written first, then the length, then the content
    // We just need the length + content part
    if (content.size() >= 2) {
        BytesSpan span(content);
        UintVar type_var(span);
        return Bytes(content.begin() + type_var.size(), content.end());
    }
    return content;
}

} // anonymous namespace

void AppendFilterParameters(const SubscriptionFilter& filter, std::vector<messages::Parameter>& params)
{
    // Add location filter if not empty
    if (!filter.GetLocationFilter().IsEmpty()) {
        Bytes content = SerializeFilter(filter.GetLocationFilter());
        params.push_back(CreateFilterParameter(messages::ParameterType::kLocationFilter, content));
    }

    // Add group filter if not empty
    if (!filter.GetGroupFilter().IsEmpty()) {
        Bytes content = SerializeFilter(filter.GetGroupFilter());
        params.push_back(CreateFilterParameter(messages::ParameterType::kGroupFilter, content));
    }

    // Add subgroup filter if not empty
    if (!filter.GetSubgroupFilter().IsEmpty()) {
        Bytes content = SerializeFilter(filter.GetSubgroupFilter());
        params.push_back(CreateFilterParameter(messages::ParameterType::kSubgroupFilter, content));
    }

    // Add object filter if not empty
    if (!filter.GetObjectFilter().IsEmpty()) {
        Bytes content = SerializeFilter(filter.GetObjectFilter());
        params.push_back(CreateFilterParameter(messages::ParameterType::kObjectFilter, content));
    }

    // Add priority filter if not empty
    if (!filter.GetPriorityFilter().IsEmpty()) {
        Bytes content = SerializeFilter(filter.GetPriorityFilter());
        params.push_back(CreateFilterParameter(messages::ParameterType::kPriorityFilter, content));
    }

    // Add extension filter if not empty
    if (!filter.GetExtensionFilter().IsEmpty()) {
        Bytes content = SerializeFilter(filter.GetExtensionFilter());
        params.push_back(CreateFilterParameter(messages::ParameterType::kExtensionFilter, content));
    }

    // Add track filter if not empty
    if (!filter.GetTrackFilter().IsEmpty()) {
        Bytes content = SerializeFilter(filter.GetTrackFilter());
        params.push_back(CreateFilterParameter(messages::ParameterType::kTrackFilter, content));
    }
}

void ParseFilterParameters(const std::vector<messages::Parameter>& params, SubscriptionFilter& filter)
{
    for (const auto& param : params) {
        BytesSpan content(param.value);

        switch (param.type) {
            case messages::ParameterType::kLocationFilter: {
                LocationFilter loc_filter;
                content >> loc_filter;
                filter.SetLocationFilter(std::move(loc_filter));
                break;
            }
            case messages::ParameterType::kGroupFilter: {
                GroupFilter grp_filter;
                content >> grp_filter;
                filter.SetGroupFilter(std::move(grp_filter));
                break;
            }
            case messages::ParameterType::kSubgroupFilter: {
                SubgroupFilter sub_filter;
                content >> sub_filter;
                filter.SetSubgroupFilter(std::move(sub_filter));
                break;
            }
            case messages::ParameterType::kObjectFilter: {
                ObjectIdFilter obj_filter;
                content >> obj_filter;
                filter.SetObjectFilter(std::move(obj_filter));
                break;
            }
            case messages::ParameterType::kPriorityFilter: {
                PriorityFilter prio_filter;
                content >> prio_filter;
                filter.SetPriorityFilter(std::move(prio_filter));
                break;
            }
            case messages::ParameterType::kExtensionFilter: {
                ExtensionFilter ext_filter;
                content >> ext_filter;
                filter.SetExtensionFilter(std::move(ext_filter));
                break;
            }
            case messages::ParameterType::kTrackFilter: {
                TrackFilter trk_filter;
                content >> trk_filter;
                filter.SetTrackFilter(std::move(trk_filter));
                break;
            }
            default:
                // Ignore other parameter types
                break;
        }
    }
}

SubscriptionFilter CreateFilterFromParameters(const std::vector<messages::Parameter>& params)
{
    SubscriptionFilter filter;
    ParseFilterParameters(params, filter);
    return filter;
}

} // namespace quicr::filters
