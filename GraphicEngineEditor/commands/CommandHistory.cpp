#include "CommandHistory.h"

// ================================================================================
// CommandHistory.cpp
// ================================================================================
// ================================================================================

void CommandHistory::Execute(std::unique_ptr<ICommand> cmd) {
    if (!cmd) {
        return;
    }

    // Execute first so the command is in a valid state before we own it.
    cmd->Execute();

    // Take ownership and push onto the undo stack.
    m_UndoStack.push_back(std::move(cmd));

    // New command invalidates the redo timeline.
    if (!m_RedoStack.empty()) {
        m_RedoStack.clear();
    }
}

void CommandHistory::Undo() {
    if (!CanUndo()) {
        return;
    }

    std::unique_ptr<ICommand> cmd = std::move(m_UndoStack.back());
    m_UndoStack.pop_back();

    cmd->Undo();

    m_RedoStack.push_back(std::move(cmd));
}

void CommandHistory::Redo() {
    if (!CanRedo()) {
        return;
    }

    std::unique_ptr<ICommand> cmd = std::move(m_RedoStack.back());
    m_RedoStack.pop_back();

    cmd->Execute();

    m_UndoStack.push_back(std::move(cmd));
}

bool CommandHistory::CanUndo() const noexcept {
    return !m_UndoStack.empty();
}

bool CommandHistory::CanRedo() const noexcept {
    return !m_RedoStack.empty();
}

size_t CommandHistory::GetUndoStackSize() const noexcept {
    return m_UndoStack.size();
}

size_t CommandHistory::GetRedoStackSize() const noexcept {
    return m_RedoStack.size();
}

void CommandHistory::Clear() {
    m_UndoStack.clear();
    m_RedoStack.clear();
}
