// SPDX-FileCopyrightText: Copyright (c) 2024 Cisco Systems
// SPDX-License-Identifier: BSD-2-Clause

#include <doctest/doctest.h>

#include "quicr/detail/data_storage.h"

TEST_CASE("DataStorage Construct")
{
    CHECK_NOTHROW(quicr::DataStorage<>::Create());

    auto storage = quicr::DataStorage<>::Create();
    CHECK_NOTHROW(quicr::DataStorage<>::Create());
}

TEST_CASE("DataStorage Push")
{
    auto buffer = quicr::DataStorage<>::Create();

    uint64_t value = 0;
    auto bytes = quicr::AsBytes(value);
    CHECK_NOTHROW(buffer->Push(bytes));
}

TEST_CASE("DataStorage Read")
{
    auto buffer = quicr::DataStorage<>::Create();

    uint64_t value = 0x0102030405060708;
    CHECK_NOTHROW(buffer->Push(quicr::AsBytes(value)));

    std::vector<uint8_t> v(buffer->begin(), buffer->end());
    CHECK_EQ(v.size(), 8);
    CHECK_EQ(v, std::vector<uint8_t>{ 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01 });
}

TEST_CASE("DataStorage Iterators")
{
    auto buffer = quicr::DataStorage<>::Create();

    uint64_t value = 0x0102030405060708;
    CHECK_NOTHROW(buffer->Push(quicr::AsBytes(value)));

    {
        auto non_overflow_it = buffer->begin() + sizeof(uint32_t);
        CHECK_EQ(*non_overflow_it, 0x04);

        CHECK_NOTHROW(buffer->Push(quicr::AsBytes(value)));

        auto overflow_it = buffer->begin() + sizeof(uint32_t) + sizeof(uint64_t);
        CHECK_EQ(*overflow_it, 0x04);
    }

    // FIXME: - is broken for
    // {
    //     auto non_overflow_it = std::prev(buffer->end()) - sizeof(uint32_t);
    //     CHECK_EQ(*non_overflow_it, 0x05);

    //     auto overflow_it = std::prev(buffer->end()) - sizeof(uint32_t) - sizeof(uint64_t);
    //     CHECK_EQ(*overflow_it, 0x05);
    // }
}

TEST_CASE("DataStorage Multiples")
{
    auto buffer = quicr::DataStorage<>::Create();

    std::string s1 = "one";
    std::string s2 = " two";
    std::string s3 = " three";

    buffer->Push(quicr::AsBytes(s1));
    buffer->Push(quicr::AsBytes(s2));
    buffer->Push(quicr::AsBytes(s3));

    std::vector<uint8_t> v(buffer->begin(), buffer->end());
    CHECK_EQ(v.size(), s1.size() + s2.size() + s3.size());
    CHECK_EQ(v.at(5), 'w');
    CHECK_EQ(std::string{ buffer->begin(), buffer->end() }, "one two three");

    auto buffer_view = quicr::DynamicDataView(buffer, 1, 11);
    auto buffer_span = quicr::DataSpan(buffer, 1, 11);

    std::vector<uint8_t> view_v(buffer_view.begin(), buffer_view.end());
    CHECK_EQ(view_v.size(), 10);
    CHECK_EQ(view_v.at(4), 'w');
    CHECK_EQ(std::string{ buffer_view.begin(), buffer_view.end() }, "ne two thr");

    std::vector<uint8_t> span_v(buffer_span.begin(), buffer_span.end());
    CHECK_EQ(span_v.size(), s1.size() + s2.size() + s3.size() - 2);
    CHECK_EQ(span_v.at(4), 'w');
    CHECK_EQ(std::string{ buffer_span.begin(), buffer_span.end() }, "ne two thre");

    auto buffer_subview = buffer_view.Subview(3);
    auto buffer_subspan = buffer_span.Subspan(3);

    std::vector<uint8_t> subview_v(buffer_subview.begin(), buffer_subview.end());
    CHECK_EQ(subview_v.size(), s1.size() + s2.size() + s3.size() - 3);
    CHECK_EQ(subview_v.at(1), 't');
    CHECK_EQ(std::string{ buffer_subview.begin(), buffer_subview.end() }, " two three");

    std::vector<uint8_t> subspan_v(buffer_subspan.begin(), buffer_subspan.end());
    CHECK_EQ(subspan_v.size(), s1.size() + s2.size() + s3.size() - 5);
    CHECK_EQ(subspan_v.at(1), 'w');
    CHECK_EQ(std::string{ buffer_subspan.begin(), buffer_subspan.end() }, "two thre");
}

TEST_CASE("DataStorage Add and Remove")
{
    auto buffer = quicr::DataStorage<>::Create();

    std::string s1 = "one";
    std::string s2 = " two";
    std::string s3 = " three";

    buffer->Push(quicr::AsBytes(s1));
    buffer->Push(quicr::AsBytes(s2));

    const auto buf_view = quicr::DynamicDataView(buffer);
    CHECK_EQ(buf_view.size(), buffer->size());

    buffer->Push(quicr::AsBytes(s3));

    CHECK_EQ(buf_view.size(), buffer->size());

    auto size_before_erase = buffer->size();

    // Shouldn't remove anything since 2 is less than first element
    buffer->EraseFront(2);
    CHECK_EQ(buffer->size(), size_before_erase);

    // Should remove the first element
    buffer->EraseFront(3);
    CHECK_EQ(buffer->size(), size_before_erase - 3);

    // Should remove the next two elements, but not the third
    buffer->Push(quicr::AsBytes(s1));
    buffer->Push(quicr::AsBytes(s1));
    size_before_erase = buffer->size();
    buffer->EraseFront(11);

    CHECK_EQ(buffer->size(), size_before_erase - 10);
}
