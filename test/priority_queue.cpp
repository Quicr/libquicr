#include <doctest/doctest.h>

#include <cstring>
#include <memory>
#include <quicr/detail/priority_queue.h>

using namespace quicr;
using namespace std::string_literals;

static auto tick_service = std::make_shared<timeq::threaded_tick_service>();

TEST_CASE("Priority Queue Push/Pop - one group")
{

    PriorityQueue<std::vector<uint8_t>> pq(30000, 1, tick_service, 150);

    std::vector<uint8_t> data(1000, 0);

    for (size_t i = 0; i < 500; ++i) {
        std::memcpy(data.data(), &i, sizeof(i));
        pq.Push(static_cast<int>(i / 15), data, 2000);
    }

    for (size_t i = 0; i < 500; ++i) {
        CHECK(pq.Empty() == false);
        const auto [value, expired] = pq.PopFront();
        CHECK(value.has_value() == true);
        CHECK(expired == 0);

        size_t num = 0;

        std::memcpy(&num, elem.value.value().data(), sizeof(num));
        CHECK(num == i);
    }

    CHECK_EQ(pq.Empty(), true);
}

TEST_CASE("Priority Queue Push/Pop - multi-group")
{
    PriorityQueue<std::vector<uint8_t>> pq(30000, 1, tick_service, 150);
    std::vector<uint8_t> data(1000, 0);

    for (size_t i = 0; i < 500; ++i) {
        std::memcpy(data.data(), &i, sizeof(i));
        pq.Push(static_cast<int>(i / 20), data, 2000);
    }

    for (size_t i = 0; i < 500; ++i) {
        CHECK(pq.Empty() == false);

        auto elem = pq.PopFront();
        CHECK(elem.value.has_value() == true);
        CHECK(elem.expired == 0);

        size_t num = 0;
        std::memcpy(&num, elem.value.value().data(), sizeof(num));
        CHECK(num == i);
    }

    CHECK_EQ(pq.Empty(), true);
}
