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

  auto Execute() -> bool override {
    m_Value.get() = m_NextValue;
    return true;
  }
  auto Undo() -> bool override {
    m_Value.get() = m_PreviousValue;
    return true;
  }

private:
  std::reference_wrapper<int> m_Value;
  int m_PreviousValue;
  int m_NextValue;
  std::reference_wrapper<int> m_Destructions;
};

class FailingCommand final : public ICommand {
public:
  auto Execute() -> bool override { return false; }
  auto Undo() -> bool override { return false; }
};

class RedoFailCommand final : public ICommand {
public:
  auto Execute() -> bool override { return ++m_Executions == 1; }
  auto Undo() -> bool override { return true; }

private:
  size_t m_Executions = 0;
};

class UndoFailCommand final : public ICommand {
public:
  auto Execute() -> bool override { return true; }
  auto Undo() -> bool override { return false; }
};

TEST(CommandHistoryTests, ExecutesUndoesAndRedoesInOrder) {
  CommandHistory history;
  int value = 3;
  int destructions = 0;

  ASSERT_TRUE(history.Execute(std::make_unique<ValueCommand>(value, 7, destructions)));
  EXPECT_EQ(value, 7);
  EXPECT_EQ(history.GetUndoStackSize(), 1U);
  EXPECT_EQ(history.GetRedoStackSize(), 0U);

  ASSERT_TRUE(history.Undo());
  EXPECT_EQ(value, 3);
  EXPECT_FALSE(history.CanUndo());
  EXPECT_TRUE(history.CanRedo());

  ASSERT_TRUE(history.Redo());
  EXPECT_EQ(value, 7);
  EXPECT_TRUE(history.CanUndo());
  EXPECT_FALSE(history.CanRedo());
  EXPECT_EQ(destructions, 0);
}

TEST(CommandHistoryTests, NewCommandInvalidatesRedoAndClearReleasesOwnedCommands) {
  CommandHistory history;
  int value = 0;
  int destructions = 0;

  ASSERT_TRUE(history.Execute(std::make_unique<ValueCommand>(value, 1, destructions)));
  ASSERT_TRUE(history.Undo());
  ASSERT_TRUE(history.CanRedo());
  ASSERT_TRUE(history.Execute(std::make_unique<ValueCommand>(value, 2, destructions)));

  EXPECT_FALSE(history.CanRedo());
  EXPECT_EQ(destructions, 1);
  history.Clear();
  EXPECT_FALSE(history.CanUndo());
  EXPECT_EQ(destructions, 2);
}

TEST(CommandHistoryTests, FailedTransitionsPreserveStateAndHistoryPosition) {
  CommandHistory history;
  int value = 4;
  int destructions = 0;
  ASSERT_TRUE(history.Execute(std::make_unique<ValueCommand>(value, 8, destructions)));
  EXPECT_FALSE(history.Execute(std::make_unique<FailingCommand>()));
  EXPECT_EQ(history.GetUndoStackSize(), 1U);
  ASSERT_TRUE(history.Undo());
  EXPECT_EQ(value, 4);
  ASSERT_TRUE(history.Redo());
  EXPECT_EQ(value, 8);
  ASSERT_TRUE(history.Execute(std::make_unique<RedoFailCommand>()));
  ASSERT_TRUE(history.Undo());
  EXPECT_FALSE(history.Redo());
  EXPECT_EQ(history.GetRedoStackSize(), 1U);
  EXPECT_EQ(history.GetUndoStackSize(), 1U);
  CommandHistory undoFailure;
  ASSERT_TRUE(undoFailure.Execute(std::make_unique<UndoFailCommand>()));
  EXPECT_FALSE(undoFailure.Undo());
  EXPECT_EQ(undoFailure.GetUndoStackSize(), 1U);
  EXPECT_EQ(undoFailure.GetRedoStackSize(), 0U);
}

TEST(CommandHistoryTests, EmptyCallsAndNullCommandsLeaveHistoryUnchanged) {
  CommandHistory history;
  int value = 0;
  int destructions = 0;

  EXPECT_FALSE(history.Undo());
  EXPECT_FALSE(history.Redo());
  EXPECT_FALSE(history.Execute(nullptr));

  EXPECT_EQ(value, 0);
  EXPECT_FALSE(history.CanUndo());
  EXPECT_FALSE(history.CanRedo());
  EXPECT_EQ(history.GetUndoStackSize(), 0U);
  EXPECT_EQ(history.GetRedoStackSize(), 0U);
  EXPECT_EQ(destructions, 0);
}

} // namespace
