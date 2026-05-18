#include <doctest/doctest.h>

#include <quicr/detail/tick_service.h>
#include <quicr/detail/time_queue.h>

using namespace quicr;

struct TestTickService : public TickService
{
    TestTickService() = default;
    virtual ~TestTickService() = default;

    TickType Microseconds() const override { return ticks; }
    TickType Milliseconds() const override { return ticks; }

    TickType ticks = 0;
};

static auto tick_manager = std::make_shared<TestTickService>();

TEST_CASE("TimeQueue, Construction")
{
    REQUIRE_NOTHROW(TimeQueue<int>(10, 1, tick_manager));
    REQUIRE_THROWS(TimeQueue<int>(10, 1, nullptr));
    REQUIRE_THROWS(TimeQueue<int>(0, 1, tick_manager));
}

TEST_CASE("TimeQueue, PushAndExpire")
{
    TimeQueue<int> tq(10, 1, tick_manager);

    TimeQueueElement<int> elem;

    tq.Push(123, 2);
    tq.Front(elem);
    REQUIRE_EQ(elem.value, 123);

    tick_manager->ticks++;
    tq.Front(elem);
    REQUIRE_EQ(elem.value, 123);

    tick_manager->ticks++;
    tq.Front(elem);
    REQUIRE_FALSE(elem.has_value);
}

TEST_CASE("TimeQueue, PushAndPop")
{
    TimeQueue<int> tq(10, 1, tick_manager);

    tq.Push(123, 1);
    REQUIRE_EQ(tq.PopFront().value, 123);

    TimeQueueElement<int> elem;
    tq.Front(elem);
    REQUIRE_FALSE(elem.has_value);
}

TEST_CASE("TimeQueue, PushAndExpireBeforePop")
{
    TimeQueue<int> tq(10, 1, tick_manager);

    tq.Push(123, 1);

    tick_manager->ticks++;
    REQUIRE_FALSE(tq.PopFront().has_value);
}

TEST_CASE("TimeQueue, PushAndPopSequential")
{
    TimeQueue<int> tq(10, 1, tick_manager);

    for (int i = 0; i < 10; ++i) {
        tq.Push(i, 1);
    }

    size_t popped = 0;
    TimeQueueElement<int> elem;
    for (tq.PopFront(elem); elem.has_value; tq.PopFront(elem)) {
        REQUIRE_EQ(elem.value, popped++);
    }

    REQUIRE_EQ(popped, 10);
}

TEST_CASE("TimeQueue, PushAndPopSequentialButExpireSome")
{
    TimeQueue<int> tq(10, 1, tick_manager);

    for (int i = 0; i < 10; ++i) {
        tq.Push(i, i + 1);
    }

    size_t popped = 0;
    size_t expected_value = 0;

    TimeQueueElement<int> elem;
    for (tq.PopFront(elem); elem.has_value; tq.PopFront(elem)) {
        ++popped;
        CHECK_EQ(elem.value, expected_value);

        expected_value += 3;
        tick_manager->ticks += 3;
    }

    REQUIRE_EQ(popped, 4);
}

TEST_CASE("TimeQueue, ExpireAllBeforePop")
{
    TimeQueue<int> tq(10, 1, tick_manager);

    for (int i = 0; i < 10; ++i) {
        tq.Push(i, i + 1);
    }

    size_t popped = 0;

    tick_manager->ticks = 10;

    TimeQueueElement<int> elem;
    for (tq.PopFront(elem); elem.has_value; tq.PopFront(elem)) {
        CHECK_EQ(++popped, 0);
    }

    REQUIRE_EQ(popped, 0);
}
