#pragma once

// ================================================================================
// ICommand.h
// ================================================================================
// Core contract for every reversible editor action. All user operations that
// must support Undo/Redo derive from this interface.
// ================================================================================

class ICommand {
public:
  virtual ~ICommand() = default;

  // Perform the action. Called on first execution AND on Redo.
  /// @return True when the state transition completed and may enter history.
  virtual auto Execute() -> bool = 0;

  // Reverse the action. Called on Undo.
  /// @return True when the state transition completed and may enter the redo stack.
  virtual auto Undo() -> bool = 0;
};
