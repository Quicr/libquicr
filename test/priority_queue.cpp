#include <doctest/doctest.h>

#include <cstring>
#include <memory>
#include <quicr/detail/priority_queue.h>

using namespace quicr;
using namespace std::string_literals;

static auto tick_service = std::make_shared<ThreadedTickService>();

TEST_CASE("Priority Queue Push/Pop - one group")
{
    PriorityQueue<std::vector<uint8_t>> pq(30000, 1, tick_service, 150);
    std::vector<uint8_t> data(1000, 0);

    for (size_t i = 0; i < 500; ++i) {
        std::memcpy(data.data(), &i, sizeof(i));
        pq.Push(static_cast<int>(i / 15), data, 2000);
    }

    TimeQueueElement<std::vector<uint8_t>> elem;

    for (size_t i = 0; i < 500; ++i) {
        CHECK(pq.Empty() == false);
        pq.PopFront(elem);
        CHECK(elem.has_value == true);
        CHECK(elem.expired_count == 0);

        size_t num = 0;
        std::memcpy(&num, elem.value.data(), sizeof(num));
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

    TimeQueueElement<std::vector<uint8_t>> elem;

    for (size_t i = 0; i < 500; ++i) {
        CHECK(pq.Empty() == false);

        pq.PopFront(elem);
        CHECK(elem.has_value == true);
        CHECK(elem.expired_count == 0);

        size_t num = 0;
        std::memcpy(&num, elem.value.data(), sizeof(num));
        CHECK(num == i);
    }

    CHECK_EQ(pq.Empty(), true);
}
