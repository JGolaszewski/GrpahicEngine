#pragma once

// ================================================================================
// CommandHistory.h
// ================================================================================
// Manages two internal stacks (vectors) to implement standard Undo/Redo semantics.
// Owns the lifetime of all commands via std::unique_ptr.
// ================================================================================

#include "commands/include/Command.h"
#include <memory>
#include <vector>

class CommandHistory {
public:
  CommandHistory() = default;
  ~CommandHistory() = default;

  // Non-copyable because of unique_ptr members.
  CommandHistory(const CommandHistory&) = delete;
  CommandHistory& operator=(const CommandHistory&) = delete;

  // Moveable. The stacks transfer ownership naturally.
  CommandHistory(CommandHistory&&) noexcept = default;
  CommandHistory& operator=(CommandHistory&&) noexcept = default;

  // ------------------------------------------------------------------------
  // Execute
  // ------------------------------------------------------------------------
  // Takes ownership of the command, executes it, and pushes it onto the undo
  // stack. Any existing redo history is invalidated because a new command
  // branches the timeline.
  // ------------------------------------------------------------------------
  void Execute(std::unique_ptr<ICommand> cmd);

  // ------------------------------------------------------------------------
  // Undo
  // ------------------------------------------------------------------------
  // Pops the most recent command from the undo stack, calls Undo(), and
  // transfers it to the redo stack.
  // ------------------------------------------------------------------------
  void Undo();

  // ------------------------------------------------------------------------
  // Redo
  // ------------------------------------------------------------------------
  // Pops the most recent command from the redo stack, re-executes it, and
  // transfers it back to the undo stack.
  // ------------------------------------------------------------------------
  void Redo();

  // Queries
  [[nodiscard]] bool CanUndo() const noexcept;
  [[nodiscard]] bool CanRedo() const noexcept;
  [[nodiscard]] size_t GetUndoStackSize() const noexcept;
  [[nodiscard]] size_t GetRedoStackSize() const noexcept;

  // Destroys all commands and releases both stacks.
  void Clear();

private:
  // Undo stack: commands that have been executed, ordered from oldest to newest.
  std::vector<std::unique_ptr<ICommand>> m_UndoStack;

  // Redo stack: commands that have been undone, ordered from newest to oldest.
  std::vector<std::unique_ptr<ICommand>> m_RedoStack;
};
