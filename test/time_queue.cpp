#include <doctest/doctest.h>

#include <timeq/tick_service.h>
#include <timeq/time_queue.h>

using namespace timeq;

struct TestTickService : public tick_service
{
    TestTickService() = default;
    ~TestTickService() override = default;

    std::chrono::microseconds get() const override { return ticks; }

    std::chrono::microseconds ticks{ 0 };
};

static auto tick_manager = std::make_shared<TestTickService>();

TEST_CASE("TimeQueue, Construction")
{
    REQUIRE_NOTHROW(time_queue<int>(10, 1, tick_manager));
    REQUIRE_THROWS(time_queue<int>(10, 1, nullptr));
    REQUIRE_THROWS(time_queue<int>(0, 1, tick_manager));
}

TEST_CASE("TimeQueue, PushAndExpire")
{
    time_queue<int> tq(10, 1, tick_manager);

    tq.push(123, 2);
    REQUIRE_EQ(tq.front().value.value(), 123);

    tick_manager->ticks += std::chrono::milliseconds(1);
    REQUIRE_EQ(tq.front().value.value(), 123);

    tick_manager->ticks += std::chrono::milliseconds(1);
    REQUIRE_FALSE(tq.front().value.has_value());
}

TEST_CASE("TimeQueue, PushAndPop")
{
    time_queue<int> tq(10, 1, tick_manager);

    tq.push(123, 1);
    REQUIRE_EQ(tq.pop_front().value.value(), 123);

    auto elem = tq.front();
    REQUIRE_FALSE(elem.value.has_value());
}

TEST_CASE("TimeQueue, PushAndExpireBeforePop")
{
    time_queue<int> tq(10, 1, tick_manager);

    tq.push(123, 1);

    tick_manager->ticks += std::chrono::milliseconds(1);
    REQUIRE_FALSE(tq.pop_front().value.has_value());
}

TEST_CASE("TimeQueue, PushAndPopSequential")
{
    time_queue<int> tq(10, 1, tick_manager);

    for (int i = 0; i < 10; ++i) {
        tq.push(i, 1);
    }

    size_t popped = 0;
    for (auto elem = tq.pop_front(); elem.value.has_value(); elem = tq.pop_front()) {
        REQUIRE_EQ(elem.value.value(), popped++);
    }

    REQUIRE_EQ(popped, 10);
}

TEST_CASE("TimeQueue, PushAndPopSequentialButExpireSome")
{
    time_queue<int> tq(10, 1, tick_manager);

    for (int i = 0; i < 10; ++i) {
        tq.push(i, i + 1);
    }

    size_t popped = 0;
    size_t expected_value = 0;
    size_t expected_expired = 0;

    for (auto elem = tq.pop_front(); elem.value.has_value(); elem = tq.pop_front()) {
        ++popped;
        CHECK_EQ(elem.value.value(), expected_value);
        CHECK_EQ(elem.expired, expected_expired);

        expected_value += 4;
        expected_expired = 3;

        tick_manager->ticks += std::chrono::milliseconds(4);
        tq.update();
    }

    REQUIRE_EQ(popped, 3);
}

TEST_CASE("TimeQueue, ExpireAllBeforePop")
{
    time_queue<int> tq(10, 1, tick_manager);

    for (int i = 0; i < 10; ++i) {
        tq.push(i, i + 1);
    }

    tick_manager->ticks = std::chrono::milliseconds(10);

    REQUIRE_FALSE(tq.empty());

    for (auto elem = tq.pop_front(); elem.value.has_value();) {
        FAIL("Expected all elements to expire");
    }

    REQUIRE(tq.empty());
}
