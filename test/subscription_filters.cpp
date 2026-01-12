// SPDX-FileCopyrightText: Copyright (c) 2025 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include "quicr/detail/subscription_filters.h"
#include "quicr/object.h"

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

using namespace quicr;
using namespace quicr::filters;

// Alias for extensions type
using TestExtensions = FilterExtensions;

// ============================================================================
// Range Tests
// ============================================================================

TEST_CASE("Range - Basic Construction")
{
    SUBCASE("Default construction")
    {
        Range<uint64_t> range;
        CHECK_EQ(range.start, 0);
        CHECK_FALSE(range.end.has_value());
        CHECK(range.IsOpenEnded());
        CHECK(range.IsValid());
    }

    SUBCASE("Single value construction")
    {
        Range<uint64_t> range(100);
        CHECK_EQ(range.start, 100);
        CHECK_FALSE(range.end.has_value());
        CHECK(range.IsOpenEnded());
    }

    SUBCASE("Start and end construction")
    {
        Range<uint64_t> range(100, 200);
        CHECK_EQ(range.start, 100);
        CHECK(range.end.has_value());
        CHECK_EQ(*range.end, 200);
        CHECK_FALSE(range.IsOpenEnded());
    }
}

TEST_CASE("Range - Contains")
{
    SUBCASE("Open-ended range")
    {
        Range<uint64_t> range(100);
        CHECK_FALSE(range.Contains(50));
        CHECK_FALSE(range.Contains(99));
        CHECK(range.Contains(100));
        CHECK(range.Contains(101));
        CHECK(range.Contains(1000000));
    }

    SUBCASE("Bounded range")
    {
        Range<uint64_t> range(100, 200);
        CHECK_FALSE(range.Contains(50));
        CHECK_FALSE(range.Contains(99));
        CHECK(range.Contains(100)); // Inclusive start
        CHECK(range.Contains(150));
        CHECK(range.Contains(200)); // Inclusive end
        CHECK_FALSE(range.Contains(201));
    }

    SUBCASE("Single point range")
    {
        Range<uint64_t> range(100, 100);
        CHECK_FALSE(range.Contains(99));
        CHECK(range.Contains(100));
        CHECK_FALSE(range.Contains(101));
    }
}

TEST_CASE("Range - Validity")
{
    SUBCASE("Valid ranges")
    {
        CHECK(Range<uint64_t>(100, 200).IsValid());
        CHECK(Range<uint64_t>(100, 100).IsValid());
        CHECK(Range<uint64_t>(100).IsValid());
    }

    SUBCASE("Invalid range - start > end")
    {
        Range<uint64_t> range(200, 100);
        CHECK_FALSE(range.IsValid());
    }
}

TEST_CASE("Range - uint8_t type")
{
    Range<uint8_t> range(10, 20);
    CHECK_FALSE(range.Contains(9));
    CHECK(range.Contains(10));
    CHECK(range.Contains(15));
    CHECK(range.Contains(20));
    CHECK_FALSE(range.Contains(21));
}

// ============================================================================
// RangeSet Tests
// ============================================================================

TEST_CASE("RangeSet - Basic Operations")
{
    SUBCASE("Empty range set matches all")
    {
        RangeSet<uint64_t> ranges;
        CHECK(ranges.IsEmpty());
        CHECK(ranges.Contains(0));
        CHECK(ranges.Contains(100));
        CHECK(ranges.Contains(UINT64_MAX));
    }

    SUBCASE("Single range")
    {
        RangeSet<uint64_t> ranges;
        ranges.Add(100, 200);
        CHECK_FALSE(ranges.IsEmpty());
        CHECK_EQ(ranges.Size(), 1);
        CHECK_FALSE(ranges.Contains(50));
        CHECK(ranges.Contains(100));
        CHECK(ranges.Contains(150));
        CHECK(ranges.Contains(200));
        CHECK_FALSE(ranges.Contains(300));
    }

    SUBCASE("Multiple non-overlapping ranges")
    {
        RangeSet<uint64_t> ranges;
        ranges.Add(100, 200);
        ranges.Add(400, 500);
        ranges.Add(700, 800);

        CHECK_EQ(ranges.Size(), 3);

        // Before first range
        CHECK_FALSE(ranges.Contains(50));

        // First range
        CHECK(ranges.Contains(100));
        CHECK(ranges.Contains(150));
        CHECK(ranges.Contains(200));

        // Between ranges
        CHECK_FALSE(ranges.Contains(300));

        // Second range
        CHECK(ranges.Contains(400));
        CHECK(ranges.Contains(450));
        CHECK(ranges.Contains(500));

        // Between ranges
        CHECK_FALSE(ranges.Contains(600));

        // Third range
        CHECK(ranges.Contains(700));
        CHECK(ranges.Contains(750));
        CHECK(ranges.Contains(800));

        // After all ranges
        CHECK_FALSE(ranges.Contains(900));
    }

    SUBCASE("Initializer list construction")
    {
        RangeSet<uint64_t> ranges{ { 100, 200 }, { 400, 500 } };
        CHECK_EQ(ranges.Size(), 2);
        CHECK(ranges.Contains(150));
        CHECK(ranges.Contains(450));
        CHECK_FALSE(ranges.Contains(300));
    }
}

TEST_CASE("RangeSet - Open-ended ranges")
{
    RangeSet<uint64_t> ranges;
    ranges.Add(1000, std::nullopt);

    CHECK_FALSE(ranges.Contains(999));
    CHECK(ranges.Contains(1000));
    CHECK(ranges.Contains(10000));
    CHECK(ranges.Contains(UINT64_MAX));
}

TEST_CASE("RangeSet - Clears correctly")
{
    RangeSet<uint64_t> ranges;
    ranges.Add(100, 200);
    CHECK_FALSE(ranges.IsEmpty());

    ranges.Clear();
    CHECK(ranges.IsEmpty());
    CHECK(ranges.Contains(100)); // Empty set matches all
}

// ============================================================================
// LocationFilter Tests
// ============================================================================

TEST_CASE("LocationFilter - Basic")
{
    SUBCASE("Empty filter matches all")
    {
        LocationFilter filter;
        CHECK(filter.IsEmpty());

        ObjectContext ctx(100, 0, 50, 0);
        CHECK(filter.Matches(ctx));
    }

    SUBCASE("Start location only")
    {
        LocationFilter filter({ 100, 50 });

        CHECK_FALSE(filter.Matches(ObjectContext(50, 0, 100, 0)));  // Before
        CHECK_FALSE(filter.Matches(ObjectContext(100, 0, 49, 0)));  // Same group, before
        CHECK(filter.Matches(ObjectContext(100, 0, 50, 0)));        // Exact match
        CHECK(filter.Matches(ObjectContext(100, 0, 100, 0)));       // Same group, after
        CHECK(filter.Matches(ObjectContext(200, 0, 0, 0)));         // After
    }

    SUBCASE("Start and end location")
    {
        LocationFilter filter({ 100, 50 }, messages::Location{ 200, 100 });

        CHECK_FALSE(filter.Matches(ObjectContext(50, 0, 100, 0)));  // Before start
        CHECK(filter.Matches(ObjectContext(100, 0, 50, 0)));        // At start
        CHECK(filter.Matches(ObjectContext(150, 0, 75, 0)));        // In middle
        CHECK(filter.Matches(ObjectContext(200, 0, 100, 0)));       // At end
        CHECK_FALSE(filter.Matches(ObjectContext(200, 0, 101, 0))); // After end
        CHECK_FALSE(filter.Matches(ObjectContext(300, 0, 0, 0)));   // After end
    }
}

TEST_CASE("LocationFilter - Special Filters")
{
    SUBCASE("Largest object filter")
    {
        auto filter = LocationFilter::LargestObject();
        CHECK(filter.IsLargestObject());
        CHECK_FALSE(filter.IsNextGroupStart());
    }

    SUBCASE("Next group start filter")
    {
        auto filter = LocationFilter::NextGroupStart();
        CHECK(filter.IsNextGroupStart());
        CHECK_FALSE(filter.IsLargestObject());
    }
}

// ============================================================================
// GroupFilter Tests
// ============================================================================

TEST_CASE("GroupFilter - Basic")
{
    SUBCASE("Empty filter matches all")
    {
        GroupFilter filter;
        CHECK(filter.IsEmpty());
        CHECK(filter.Matches(ObjectContext(100, 0, 50, 0)));
    }

    SUBCASE("Single group range")
    {
        GroupFilter filter;
        filter.AddRange(100, 200);

        CHECK_FALSE(filter.Matches(ObjectContext(50, 0, 0, 0)));
        CHECK(filter.Matches(ObjectContext(100, 0, 0, 0)));
        CHECK(filter.Matches(ObjectContext(150, 0, 0, 0)));
        CHECK(filter.Matches(ObjectContext(200, 0, 0, 0)));
        CHECK_FALSE(filter.Matches(ObjectContext(250, 0, 0, 0)));
    }

    SUBCASE("Multiple group ranges")
    {
        GroupFilter filter({ { 100, 200 }, { 400, 500 } });

        CHECK(filter.Matches(ObjectContext(150, 0, 0, 0)));
        CHECK(filter.Matches(ObjectContext(450, 0, 0, 0)));
        CHECK_FALSE(filter.Matches(ObjectContext(300, 0, 0, 0)));
    }
}

// ============================================================================
// SubgroupFilter Tests
// ============================================================================

TEST_CASE("SubgroupFilter - Basic")
{
    SUBCASE("Empty filter matches all")
    {
        SubgroupFilter filter;
        CHECK(filter.IsEmpty());
        CHECK(filter.Matches(ObjectContext(100, 50, 0, 0)));
    }

    SUBCASE("Single subgroup range")
    {
        SubgroupFilter filter;
        filter.AddRange(10, 20);

        CHECK_FALSE(filter.Matches(ObjectContext(100, 5, 0, 0)));
        CHECK(filter.Matches(ObjectContext(100, 10, 0, 0)));
        CHECK(filter.Matches(ObjectContext(100, 15, 0, 0)));
        CHECK(filter.Matches(ObjectContext(100, 20, 0, 0)));
        CHECK_FALSE(filter.Matches(ObjectContext(100, 25, 0, 0)));
    }
}

// ============================================================================
// ObjectIdFilter Tests
// ============================================================================

TEST_CASE("ObjectIdFilter - Basic")
{
    SUBCASE("Empty filter matches all")
    {
        ObjectIdFilter filter;
        CHECK(filter.IsEmpty());
        CHECK(filter.Matches(ObjectContext(100, 0, 50, 0)));
    }

    SUBCASE("Single object range")
    {
        ObjectIdFilter filter;
        filter.AddRange(0, 99);

        CHECK(filter.Matches(ObjectContext(100, 0, 0, 0)));
        CHECK(filter.Matches(ObjectContext(100, 0, 50, 0)));
        CHECK(filter.Matches(ObjectContext(100, 0, 99, 0)));
        CHECK_FALSE(filter.Matches(ObjectContext(100, 0, 100, 0)));
    }

    SUBCASE("Multiple object ranges - every 10th object")
    {
        ObjectIdFilter filter({
            { 0, 0 },
            { 10, 10 },
            { 20, 20 },
            { 30, 30 },
        });

        CHECK(filter.Matches(ObjectContext(100, 0, 0, 0)));
        CHECK_FALSE(filter.Matches(ObjectContext(100, 0, 5, 0)));
        CHECK(filter.Matches(ObjectContext(100, 0, 10, 0)));
        CHECK_FALSE(filter.Matches(ObjectContext(100, 0, 15, 0)));
        CHECK(filter.Matches(ObjectContext(100, 0, 20, 0)));
    }
}

// ============================================================================
// PriorityFilter Tests
// ============================================================================

TEST_CASE("PriorityFilter - Basic")
{
    SUBCASE("Empty filter matches all")
    {
        PriorityFilter filter;
        CHECK(filter.IsEmpty());
        CHECK(filter.Matches(ObjectContext(100, 0, 50, 128)));
    }

    SUBCASE("Single priority range - high priority only")
    {
        PriorityFilter filter;
        filter.AddRange(0, 63); // Lower value = higher priority

        CHECK(filter.Matches(ObjectContext(100, 0, 0, 0)));
        CHECK(filter.Matches(ObjectContext(100, 0, 0, 32)));
        CHECK(filter.Matches(ObjectContext(100, 0, 0, 63)));
        CHECK_FALSE(filter.Matches(ObjectContext(100, 0, 0, 64)));
        CHECK_FALSE(filter.Matches(ObjectContext(100, 0, 0, 128)));
    }

    SUBCASE("Multiple priority ranges")
    {
        PriorityFilter filter({
            { 0, 31 },   // High priority
            { 192, 255 } // Low priority
        });

        CHECK(filter.Matches(ObjectContext(100, 0, 0, 16)));   // High priority
        CHECK_FALSE(filter.Matches(ObjectContext(100, 0, 0, 64)));  // Medium priority
        CHECK(filter.Matches(ObjectContext(100, 0, 0, 200))); // Low priority
    }
}

// ============================================================================
// ExtensionFilter Tests
// ============================================================================

TEST_CASE("ExtensionFilter - Basic")
{
    SUBCASE("Empty filter matches all")
    {
        ExtensionFilter filter;
        CHECK(filter.IsEmpty());
        CHECK(filter.Matches(ObjectContext()));
    }

    SUBCASE("Single extension type filter")
    {
        ExtensionFilter filter;
        RangeSet<uint64_t> value_ranges;
        value_ranges.Add(100, 200);
        filter.AddTypeFilter(0x10, std::move(value_ranges));

        // No extensions - should not match
        ObjectContext ctx_no_ext;
        CHECK_FALSE(filter.Matches(ctx_no_ext));

        // With matching extension
        TestExtensions ext;
        ext[0x10] = { { 150, 0, 0, 0, 0, 0, 0, 0 } };
        std::optional<TestExtensions> opt_ext = ext;
        ObjectContext ctx_match(100, 0, 50, 0, &opt_ext, nullptr);
        CHECK(filter.Matches(ctx_match));

        // With non-matching extension value
        ext[0x10] = { { 50, 0, 0, 0, 0, 0, 0, 0 } };
        opt_ext = ext;
        ObjectContext ctx_no_match(100, 0, 50, 0, &opt_ext, nullptr);
        CHECK_FALSE(filter.Matches(ctx_no_match));
    }
}

// ============================================================================
// TrackFilter Tests
// ============================================================================

TEST_CASE("TrackFilter - Basic")
{
    SUBCASE("Empty filter")
    {
        TrackFilter filter;
        CHECK(filter.IsEmpty());
    }

    SUBCASE("Track selection with max tracks = 2")
    {
        TrackFilter filter(0x10, 2, 5, 10000);

        CHECK_EQ(filter.GetExtensionType(), 0x10);
        CHECK_EQ(filter.GetMaxTracksSelected(), 2);
        CHECK_EQ(filter.GetMaxTracksDeselected(), 5);
        CHECK_EQ(filter.GetMaxTimeSelected().count(), 10000);

        // Create extensions with different values
        TestExtensions ext1, ext2, ext3;
        ext1[0x10] = { { 100, 0, 0, 0, 0, 0, 0, 0 } };
        ext2[0x10] = { { 200, 0, 0, 0, 0, 0, 0, 0 } };
        ext3[0x10] = { { 50, 0, 0, 0, 0, 0, 0, 0 } };

        std::optional<TestExtensions> opt_ext1 = ext1;
        std::optional<TestExtensions> opt_ext2 = ext2;
        std::optional<TestExtensions> opt_ext3 = ext3;

        // Track 1 with value 100
        ObjectContext ctx1(100, 0, 0, 0, &opt_ext1, nullptr);
        CHECK(filter.EvaluateTrackSelection(1, ctx1));

        // Track 2 with value 200 - should be selected (top 2)
        ObjectContext ctx2(100, 0, 0, 0, &opt_ext2, nullptr);
        CHECK(filter.EvaluateTrackSelection(2, ctx2));

        // Track 3 with value 50 - should NOT be selected (not in top 2)
        ObjectContext ctx3(100, 0, 0, 0, &opt_ext3, nullptr);
        CHECK_FALSE(filter.EvaluateTrackSelection(3, ctx3));

        // Track 1 should still be selected
        CHECK(filter.IsTrackSelected(1));
        CHECK(filter.IsTrackSelected(2));
        CHECK_FALSE(filter.IsTrackSelected(3));

        CHECK_EQ(filter.GetSelectedTrackCount(), 2);
    }
}

// ============================================================================
// SubscriptionFilter Composite Tests
// ============================================================================

TEST_CASE("SubscriptionFilter - Empty matches all")
{
    SubscriptionFilter filter;
    CHECK(filter.IsEmpty());

    ObjectContext ctx(100, 50, 25, 128);
    CHECK(filter.Matches(ctx));
}

TEST_CASE("SubscriptionFilter - Single filter type")
{
    SUBCASE("Group filter only")
    {
        SubscriptionFilter filter;
        GroupFilter group_filter;
        group_filter.AddRange(100, 200);
        filter.SetGroupFilter(std::move(group_filter));

        CHECK(filter.Matches(ObjectContext(150, 0, 0, 0)));
        CHECK_FALSE(filter.Matches(ObjectContext(50, 0, 0, 0)));
    }

    SUBCASE("Priority filter only")
    {
        SubscriptionFilter filter;
        PriorityFilter priority_filter;
        priority_filter.AddRange(0, 63);
        filter.SetPriorityFilter(std::move(priority_filter));

        CHECK(filter.Matches(ObjectContext(100, 0, 0, 32)));
        CHECK_FALSE(filter.Matches(ObjectContext(100, 0, 0, 128)));
    }
}

TEST_CASE("SubscriptionFilter - Combined filters (AND semantics)")
{
    SubscriptionFilter filter;

    // Group filter: 100-200
    GroupFilter group_filter;
    group_filter.AddRange(100, 200);
    filter.SetGroupFilter(std::move(group_filter));

    // Object filter: 0-99
    ObjectIdFilter object_filter;
    object_filter.AddRange(0, 99);
    filter.SetObjectFilter(std::move(object_filter));

    // Priority filter: 0-63 (high priority)
    PriorityFilter priority_filter;
    priority_filter.AddRange(0, 63);
    filter.SetPriorityFilter(std::move(priority_filter));

    // All conditions met
    CHECK(filter.Matches(ObjectContext(150, 0, 50, 32)));

    // Group out of range
    CHECK_FALSE(filter.Matches(ObjectContext(50, 0, 50, 32)));

    // Object out of range
    CHECK_FALSE(filter.Matches(ObjectContext(150, 0, 150, 32)));

    // Priority out of range
    CHECK_FALSE(filter.Matches(ObjectContext(150, 0, 50, 128)));
}

TEST_CASE("SubscriptionFilter - All filter types")
{
    SubscriptionFilter filter;

    // Location filter
    filter.SetLocationFilter(LocationFilter({ 100, 0 }, messages::Location{ 200, 100 }));

    // Group filter
    GroupFilter group_filter;
    group_filter.AddRange(100, 200);
    filter.SetGroupFilter(std::move(group_filter));

    // Subgroup filter
    SubgroupFilter subgroup_filter;
    subgroup_filter.AddRange(0, 10);
    filter.SetSubgroupFilter(std::move(subgroup_filter));

    // Object filter
    ObjectIdFilter object_filter;
    object_filter.AddRange(0, 50);
    filter.SetObjectFilter(std::move(object_filter));

    // Priority filter
    PriorityFilter priority_filter;
    priority_filter.AddRange(0, 127);
    filter.SetPriorityFilter(std::move(priority_filter));

    CHECK_FALSE(filter.IsEmpty());

    // All conditions met
    CHECK(filter.Matches(ObjectContext(150, 5, 25, 64)));

    // Location too early
    CHECK_FALSE(filter.Matches(ObjectContext(50, 5, 25, 64)));

    // Subgroup out of range
    CHECK_FALSE(filter.Matches(ObjectContext(150, 20, 25, 64)));

    // Object out of range
    CHECK_FALSE(filter.Matches(ObjectContext(150, 5, 75, 64)));

    // Priority out of range
    CHECK_FALSE(filter.Matches(ObjectContext(150, 5, 25, 200)));
}

// ============================================================================
// Serialization Tests
// ============================================================================

TEST_CASE("LocationFilter - Serialization")
{
    SUBCASE("Empty filter")
    {
        LocationFilter filter;
        Bytes buffer;
        buffer << filter;

        LocationFilter restored;
        BytesSpan span(buffer);
        // Skip the parameter type
        UintVar type_var(span);
        span = span.subspan(type_var.size());
        span >> restored;

        CHECK(restored.IsEmpty());
    }

    SUBCASE("Start only")
    {
        LocationFilter filter({ 100, 50 });
        Bytes buffer;
        buffer << filter;

        LocationFilter restored;
        BytesSpan span(buffer);
        UintVar type_var(span);
        span = span.subspan(type_var.size());
        span >> restored;

        CHECK_EQ(restored.GetStart().group, 100);
        CHECK_EQ(restored.GetStart().object, 50);
    }

    SUBCASE("Start and end")
    {
        LocationFilter filter({ 100, 50 }, messages::Location{ 200, 100 });
        Bytes buffer;
        buffer << filter;

        LocationFilter restored;
        BytesSpan span(buffer);
        UintVar type_var(span);
        span = span.subspan(type_var.size());
        span >> restored;

        CHECK_EQ(restored.GetStart().group, 100);
        CHECK_EQ(restored.GetStart().object, 50);
        CHECK(restored.GetEnd().has_value());
        CHECK_EQ(restored.GetEnd()->group, 200);
        CHECK_EQ(restored.GetEnd()->object, 100);
    }
}

TEST_CASE("GroupFilter - Serialization")
{
    GroupFilter filter;
    filter.AddRange(100, 200);
    filter.AddRange(400, 500);

    Bytes buffer;
    buffer << filter;

    GroupFilter restored;
    BytesSpan span(buffer);
    UintVar type_var(span);
    span = span.subspan(type_var.size());
    span >> restored;

    CHECK(restored.Matches(ObjectContext(150, 0, 0, 0)));
    CHECK(restored.Matches(ObjectContext(450, 0, 0, 0)));
    CHECK_FALSE(restored.Matches(ObjectContext(300, 0, 0, 0)));
}

TEST_CASE("PriorityFilter - Serialization")
{
    PriorityFilter filter;
    filter.AddRange(0, 63);
    filter.AddRange(192, 255);

    Bytes buffer;
    buffer << filter;

    PriorityFilter restored;
    BytesSpan span(buffer);
    UintVar type_var(span);
    span = span.subspan(type_var.size());
    span >> restored;

    CHECK(restored.Matches(ObjectContext(0, 0, 0, 32)));
    CHECK(restored.Matches(ObjectContext(0, 0, 0, 200)));
    CHECK_FALSE(restored.Matches(ObjectContext(0, 0, 0, 128)));
}

TEST_CASE("TrackFilter - Serialization")
{
    TrackFilter filter(0x10, 5, 10, 30000);

    Bytes buffer;
    buffer << filter;

    TrackFilter restored;
    BytesSpan span(buffer);
    UintVar type_var(span);
    span = span.subspan(type_var.size());
    span >> restored;

    CHECK_EQ(restored.GetExtensionType(), 0x10);
    CHECK_EQ(restored.GetMaxTracksSelected(), 5);
    CHECK_EQ(restored.GetMaxTracksDeselected(), 10);
    CHECK_EQ(restored.GetMaxTimeSelected().count(), 30000);
}

// ============================================================================
// ObjectContext Construction Tests
// ============================================================================

TEST_CASE("ObjectContext - Basic construction")
{
    ObjectContext ctx(100, 5, 50, 64);

    CHECK_EQ(ctx.group_id, 100);
    CHECK_EQ(ctx.object_id, 50);
    CHECK_EQ(ctx.subgroup_id, 5);
    CHECK_EQ(ctx.priority, 64);
}

TEST_CASE("ObjectContext - With extensions")
{
    TestExtensions ext;
    ext[0x10] = { { 100, 0, 0, 0, 0, 0, 0, 0 } };
    std::optional<TestExtensions> opt_ext = ext;

    ObjectContext ctx(100, 5, 50, 64, &opt_ext, nullptr);

    CHECK_EQ(ctx.group_id, 100);
    CHECK_EQ(ctx.object_id, 50);
    CHECK_EQ(ctx.subgroup_id, 5);
    CHECK_EQ(ctx.priority, 64);
    CHECK(ctx.extensions != nullptr);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_CASE("RangeSet - Performance with many ranges")
{
    RangeSet<uint64_t> ranges;

    // Add 100 non-overlapping ranges
    for (uint64_t i = 0; i < 100; ++i) {
        ranges.Add(i * 1000, i * 1000 + 500);
    }

    CHECK_EQ(ranges.Size(), 100);

    // Verify containment checks work correctly
    CHECK(ranges.Contains(0));
    CHECK(ranges.Contains(50000 + 250));
    CHECK(ranges.Contains(99000 + 250));
    CHECK_FALSE(ranges.Contains(50000 + 750));
    CHECK_FALSE(ranges.Contains(100000));
}

TEST_CASE("SubscriptionFilter - Complex filter evaluation performance")
{
    SubscriptionFilter filter;

    // Set up a simple group filter
    GroupFilter group_filter;
    group_filter.AddRange(100, 200);
    filter.SetGroupFilter(std::move(group_filter));

    // Run a few evaluations
    int match_count = 0;
    for (uint64_t g = 0; g < 300; g += 50) {
        if (filter.Matches(ObjectContext(g, 0, 0, 0))) {
            ++match_count;
        }
    }

    // Verify expected matches (100, 150, 200 should match)
    CHECK_EQ(match_count, 3);
}

// ============================================================================
// ExtensionFilter Serialization Tests
// ============================================================================

TEST_CASE("ExtensionFilter - Serialization")
{
    SUBCASE("Empty extension filter")
    {
        ExtensionFilter filter;
        Bytes buffer;
        buffer << filter;

        ExtensionFilter restored;
        BytesSpan span(buffer);
        UintVar type_var(span);
        span = span.subspan(type_var.size());
        span >> restored;

        CHECK(restored.IsEmpty());
    }

    SUBCASE("Single extension type with ranges")
    {
        ExtensionFilter filter;
        RangeSet<uint64_t> value_ranges;
        value_ranges.Add(100, 200);
        filter.AddTypeFilter(0x10, std::move(value_ranges));

        Bytes buffer;
        buffer << filter;

        ExtensionFilter restored;
        BytesSpan span(buffer);
        UintVar type_var(span);
        span = span.subspan(type_var.size());
        span >> restored;

        CHECK_FALSE(restored.IsEmpty());
        CHECK_EQ(restored.GetTypeFilters().size(), 1);
        CHECK_EQ(restored.GetTypeFilters()[0].extension_type, 0x10);
    }
}

// ============================================================================
// SubscriptionFilter Combined Serialization Tests
// ============================================================================

TEST_CASE("SubscriptionFilter - Serialization with multiple filter types")
{
    SUBCASE("Combined group and priority filters")
    {
        SubscriptionFilter original;

        // Set group filter
        GroupFilter group_filter;
        group_filter.AddRange(100, 200);
        group_filter.AddRange(500, 600);
        original.SetGroupFilter(std::move(group_filter));

        // Set priority filter
        PriorityFilter priority_filter;
        priority_filter.AddRange(0, 63);
        original.SetPriorityFilter(std::move(priority_filter));

        // Verify original works
        CHECK(original.Matches(ObjectContext(150, 0, 0, 32)));
        CHECK_FALSE(original.Matches(ObjectContext(150, 0, 0, 128)));
        CHECK_FALSE(original.Matches(ObjectContext(300, 0, 0, 32)));

        // Convert to parameters
        std::vector<quicr::messages::Parameter> params;
        AppendFilterParameters(original, params);

        // Should have 2 parameters (group + priority)
        CHECK_EQ(params.size(), 2);

        // Parse back
        SubscriptionFilter restored = CreateFilterFromParameters(params);

        // Verify restored filter works the same way
        CHECK(restored.Matches(ObjectContext(150, 0, 0, 32)));
        CHECK_FALSE(restored.Matches(ObjectContext(150, 0, 0, 128)));
        CHECK_FALSE(restored.Matches(ObjectContext(300, 0, 0, 32)));
    }

    SUBCASE("All object filter types combined")
    {
        SubscriptionFilter original;

        // Location filter
        original.SetLocationFilter(LocationFilter({ 100, 0 }, quicr::messages::Location{ 500, 100 }));

        // Group filter
        GroupFilter group_filter;
        group_filter.AddRange(100, 500);
        original.SetGroupFilter(std::move(group_filter));

        // Subgroup filter
        SubgroupFilter subgroup_filter;
        subgroup_filter.AddRange(0, 10);
        original.SetSubgroupFilter(std::move(subgroup_filter));

        // Object filter
        ObjectIdFilter object_filter;
        object_filter.AddRange(0, 50);
        original.SetObjectFilter(std::move(object_filter));

        // Priority filter
        PriorityFilter priority_filter;
        priority_filter.AddRange(0, 127);
        original.SetPriorityFilter(std::move(priority_filter));

        CHECK_FALSE(original.IsEmpty());

        // Convert to parameters
        std::vector<quicr::messages::Parameter> params;
        AppendFilterParameters(original, params);

        // Should have 5 parameters
        CHECK_EQ(params.size(), 5);

        // Parse back
        SubscriptionFilter restored = CreateFilterFromParameters(params);

        // Verify matches work
        CHECK(restored.Matches(ObjectContext(200, 5, 25, 64)));
        CHECK_FALSE(restored.Matches(ObjectContext(50, 5, 25, 64)));   // Location too early
        CHECK_FALSE(restored.Matches(ObjectContext(200, 15, 25, 64))); // Subgroup out of range
        CHECK_FALSE(restored.Matches(ObjectContext(200, 5, 75, 64)));  // Object out of range
        CHECK_FALSE(restored.Matches(ObjectContext(200, 5, 25, 200))); // Priority out of range
    }
}

// ============================================================================
// Parameter Round-Trip Tests
// ============================================================================

TEST_CASE("Parameter conversion round-trip")
{
    SUBCASE("Empty filter produces no parameters")
    {
        SubscriptionFilter filter;
        std::vector<quicr::messages::Parameter> params;
        AppendFilterParameters(filter, params);
        CHECK(params.empty());
    }

    SUBCASE("Group filter round-trip")
    {
        GroupFilter original;
        original.AddRange(100, 200);
        original.AddRange(400, std::nullopt); // Open-ended

        SubscriptionFilter filter;
        filter.SetGroupFilter(std::move(original));

        std::vector<quicr::messages::Parameter> params;
        AppendFilterParameters(filter, params);

        CHECK_EQ(params.size(), 1);
        CHECK_EQ(params[0].type, quicr::messages::ParameterType::kGroupFilter);

        SubscriptionFilter restored = CreateFilterFromParameters(params);

        // Verify behavior
        CHECK(restored.Matches(ObjectContext(150, 0, 0, 0)));
        CHECK_FALSE(restored.Matches(ObjectContext(50, 0, 0, 0)));
        CHECK_FALSE(restored.Matches(ObjectContext(300, 0, 0, 0)));
        CHECK(restored.Matches(ObjectContext(400, 0, 0, 0)));
        CHECK(restored.Matches(ObjectContext(1000000, 0, 0, 0))); // Open-ended
    }

    SUBCASE("Priority filter round-trip")
    {
        PriorityFilter original;
        original.AddRange(0, 31);    // High priority
        original.AddRange(224, 255); // Low priority

        SubscriptionFilter filter;
        filter.SetPriorityFilter(std::move(original));

        std::vector<quicr::messages::Parameter> params;
        AppendFilterParameters(filter, params);

        CHECK_EQ(params.size(), 1);
        CHECK_EQ(params[0].type, quicr::messages::ParameterType::kPriorityFilter);

        SubscriptionFilter restored = CreateFilterFromParameters(params);

        // Verify behavior
        CHECK(restored.Matches(ObjectContext(0, 0, 0, 16)));   // High priority
        CHECK_FALSE(restored.Matches(ObjectContext(0, 0, 0, 128))); // Mid priority
        CHECK(restored.Matches(ObjectContext(0, 0, 0, 240)));  // Low priority
    }

    SUBCASE("Track filter round-trip")
    {
        TrackFilter original(0xABCD, 5, 10, 30000);

        SubscriptionFilter filter;
        filter.SetTrackFilter(std::move(original));

        std::vector<quicr::messages::Parameter> params;
        AppendFilterParameters(filter, params);

        CHECK_EQ(params.size(), 1);
        CHECK_EQ(params[0].type, quicr::messages::ParameterType::kTrackFilter);

        SubscriptionFilter restored = CreateFilterFromParameters(params);

        const auto& track_filter = restored.GetTrackFilter();
        CHECK_EQ(track_filter.GetExtensionType(), 0xABCD);
        CHECK_EQ(track_filter.GetMaxTracksSelected(), 5);
        CHECK_EQ(track_filter.GetMaxTracksDeselected(), 10);
        CHECK_EQ(track_filter.GetMaxTimeSelected().count(), 30000);
    }

    SUBCASE("Location filter round-trip")
    {
        LocationFilter original({ 100, 50 }, quicr::messages::Location{ 200, 100 });

        SubscriptionFilter filter;
        filter.SetLocationFilter(std::move(original));

        std::vector<quicr::messages::Parameter> params;
        AppendFilterParameters(filter, params);

        CHECK_EQ(params.size(), 1);
        CHECK_EQ(params[0].type, quicr::messages::ParameterType::kLocationFilter);

        SubscriptionFilter restored = CreateFilterFromParameters(params);

        const auto& loc_filter = restored.GetLocationFilter();
        CHECK_EQ(loc_filter.GetStart().group, 100);
        CHECK_EQ(loc_filter.GetStart().object, 50);
        CHECK(loc_filter.GetEnd().has_value());
        CHECK_EQ(loc_filter.GetEnd()->group, 200);
        CHECK_EQ(loc_filter.GetEnd()->object, 100);
    }
}
