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
    virtual void Execute() = 0;

    // Reverse the action. Called on Undo.
    virtual void Undo() = 0;
};
