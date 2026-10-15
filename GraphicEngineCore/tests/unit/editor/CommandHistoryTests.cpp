#include "commands/include/CommandHistory.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>

namespace {

class ValueCommand final : public ICommand {
public:
  ValueCommand(int& value, int nextValue, int& destructions)
      : m_Value(value), m_PreviousValue(value), m_NextValue(nextValue), m_Destructions(destructions) {}

  ValueCommand(const ValueCommand&) = delete;
  auto operator=(const ValueCommand&) -> ValueCommand& = delete;
  ValueCommand(ValueCommand&&) = delete;
  auto operator=(ValueCommand&&) -> ValueCommand& = delete;

  ~ValueCommand() override { ++m_Destructions.get(); }

  void Execute() override { m_Value.get() = m_NextValue; }
  void Undo() override { m_Value.get() = m_PreviousValue; }

private:
  std::reference_wrapper<int> m_Value;
  int m_PreviousValue;
  int m_NextValue;
  std::reference_wrapper<int> m_Destructions;
};

TEST(CommandHistoryTests, ExecutesUndoesAndRedoesInOrder) {
  CommandHistory history;
  int value = 3;
  int destructions = 0;

  history.Execute(std::make_unique<ValueCommand>(value, 7, destructions));
  EXPECT_EQ(value, 7);
  EXPECT_EQ(history.GetUndoStackSize(), 1U);
  EXPECT_EQ(history.GetRedoStackSize(), 0U);

  history.Undo();
  EXPECT_EQ(value, 3);
  EXPECT_FALSE(history.CanUndo());
  EXPECT_TRUE(history.CanRedo());

  history.Redo();
  EXPECT_EQ(value, 7);
  EXPECT_TRUE(history.CanUndo());
  EXPECT_FALSE(history.CanRedo());
  EXPECT_EQ(destructions, 0);
}

TEST(CommandHistoryTests, NewCommandInvalidatesRedoAndClearReleasesOwnedCommands) {
  CommandHistory history;
  int value = 0;
  int destructions = 0;

  history.Execute(std::make_unique<ValueCommand>(value, 1, destructions));
  history.Undo();
  ASSERT_TRUE(history.CanRedo());
  history.Execute(std::make_unique<ValueCommand>(value, 2, destructions));

  EXPECT_FALSE(history.CanRedo());
  EXPECT_EQ(destructions, 1);
  history.Clear();
  EXPECT_FALSE(history.CanUndo());
  EXPECT_EQ(destructions, 2);
}

TEST(CommandHistoryTests, EmptyCallsAndNullCommandsLeaveHistoryUnchanged) {
  CommandHistory history;
  int value = 0;
  int destructions = 0;

  history.Undo();
  history.Redo();
  history.Execute(nullptr);

  EXPECT_EQ(value, 0);
  EXPECT_FALSE(history.CanUndo());
  EXPECT_FALSE(history.CanRedo());
  EXPECT_EQ(history.GetUndoStackSize(), 0U);
  EXPECT_EQ(history.GetRedoStackSize(), 0U);
  EXPECT_EQ(destructions, 0);
}

} // namespace
