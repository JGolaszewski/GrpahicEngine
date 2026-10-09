#pragma once

#include "commands/include/Command.h"
#include "runtime/include/events/Event.h"
#include <memory>

class ICommand;

// ================================================================================
// CommandExecuteRequestedEvent.h
// ================================================================================
// Emitted by any panel that needs to push a reversible command into the
// global undo/redo history. The Editor receives this and delegates to
// CommandHistory::Execute().
// ================================================================================

class CommandExecuteRequestedEvent : public Engine::Events::Event
{
  public:
    explicit CommandExecuteRequestedEvent(std::unique_ptr<ICommand> cmd) : m_Command(std::move(cmd))
    {
    }

    static Engine::Events::EventType GetStaticType()
    {
        return Engine::Events::EventType::CommandExecuteRequested;
    }

    Engine::Events::EventType GetType() const override
    {
        return GetStaticType();
    }

    std::unique_ptr<ICommand> TakeCommand()
    {
        return std::move(m_Command);
    }

  private:
    std::unique_ptr<ICommand> m_Command;
};
