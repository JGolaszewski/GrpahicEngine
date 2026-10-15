#include "runtime/include/Result.h"
#include "runtime/include/events/Event.h"

#include <gtest/gtest.h>

#include <string>

namespace
{

class TestEvent final : public Engine::Events::Event
{
  public:
    static auto GetStaticType() -> Engine::Events::EventType
    {
        return Engine::Events::EventType::KeyPressed;
    }
    auto GetType() const -> Engine::Events::EventType override
    {
        return GetStaticType();
    }
};

class OtherTestEvent final : public Engine::Events::Event
{
  public:
    static auto GetStaticType() -> Engine::Events::EventType
    {
        return Engine::Events::EventType::KeyReleased;
    }
    auto GetType() const -> Engine::Events::EventType override
    {
        return GetStaticType();
    }
};

TEST(EventDispatcherTests, DispatchesMatchingTypeAndPropagatesHandledState)
{
    TestEvent event;
    Engine::Events::EventDispatcher dispatcher(event);
    int callbackCount = 0;

    EXPECT_TRUE(dispatcher.Dispatch<TestEvent>([&callbackCount](TestEvent &) {
        ++callbackCount;
        return true;
    }));
    EXPECT_EQ(callbackCount, 1);
    EXPECT_TRUE(event.Handled);
}

TEST(EventDispatcherTests, IgnoresNonmatchingTypeWithoutCallingCallback)
{
    TestEvent event;
    Engine::Events::EventDispatcher dispatcher(event);
    int callbackCount = 0;

    EXPECT_FALSE(dispatcher.Dispatch<OtherTestEvent>([&callbackCount](OtherTestEvent &) {
        ++callbackCount;
        return true;
    }));
    EXPECT_EQ(callbackCount, 0);
    EXPECT_FALSE(event.Handled);
}

TEST(EventDispatcherTests, DoesNotClearAlreadyHandledEvent)
{
    TestEvent event;
    event.Handled = true;
    Engine::Events::EventDispatcher dispatcher(event);

    EXPECT_TRUE(dispatcher.Dispatch<TestEvent>([](TestEvent &) { return false; }));
    EXPECT_TRUE(event.Handled);
}

TEST(ResultTests, CarriesSuccessfulValueAndErrorMessage)
{
    Engine::Result<int> success = 42;
    ASSERT_TRUE(success.has_value());
    EXPECT_EQ(*success, 42);

    Engine::Result<int> failure = std::unexpected(Engine::MakeError("failed"));
    ASSERT_FALSE(failure.has_value());
    EXPECT_EQ(failure.error().Message, "failed");
}

} // namespace
