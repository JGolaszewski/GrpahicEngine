#include "commands/include/CommandHistory.h"

// ================================================================================
// CommandHistory.cpp
// ================================================================================
// ================================================================================

auto CommandHistory::Execute(std::unique_ptr<ICommand> cmd) -> bool {
  if (!cmd) {
    return false;
  }

  // Execute first so the command is in a valid state before we own it.
  if (!cmd->Execute()) {
    return false;
  }

  // Take ownership and push onto the undo stack.
  m_UndoStack.push_back(std::move(cmd));

  // New command invalidates the redo timeline.
  if (!m_RedoStack.empty()) {
    m_RedoStack.clear();
  }
  return true;
}

auto CommandHistory::Undo() -> bool {
  if (!CanUndo()) {
    return false;
  }

  std::unique_ptr<ICommand> cmd = std::move(m_UndoStack.back());
  m_UndoStack.pop_back();

  if (!cmd->Undo()) {
    m_UndoStack.push_back(std::move(cmd));
    return false;
  }

  m_RedoStack.push_back(std::move(cmd));
  return true;
}

auto CommandHistory::Redo() -> bool {
  if (!CanRedo()) {
    return false;
  }

  std::unique_ptr<ICommand> cmd = std::move(m_RedoStack.back());
  m_RedoStack.pop_back();

  if (!cmd->Execute()) {
    m_RedoStack.push_back(std::move(cmd));
    return false;
  }

  m_UndoStack.push_back(std::move(cmd));
  return true;
}

auto CommandHistory::CanUndo() const noexcept -> bool { return !m_UndoStack.empty(); }

auto CommandHistory::CanRedo() const noexcept -> bool { return !m_RedoStack.empty(); }

auto CommandHistory::GetUndoStackSize() const noexcept -> size_t { return m_UndoStack.size(); }

auto CommandHistory::GetRedoStackSize() const noexcept -> size_t { return m_RedoStack.size(); }

void CommandHistory::Clear() {
  m_UndoStack.clear();
  m_RedoStack.clear();
}
