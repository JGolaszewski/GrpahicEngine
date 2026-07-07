# C++ Game Engine MVP — Architecture Plan

## Overview
Build a minimum viable product in modern C++ (C++17/20) for two core engine systems: an Event Dispatching system and a Command system (Undo/Redo), with an Editor layer simulation demonstrating top-down event propagation.

## Stages

### Stage 1 — Parallel Core Subsystems (Agents run in parallel)

#### Agent 1: EventSystem_Architect
**Goal:** Produce the Event System header-only code.

**Deliverables:**
1. `Event` base class with `bool Handled = false;` and pure virtual `GetEventType()`
2. `enum class EventType` for type safety without RTTI/macros
3. `EventDispatcher` wrapper class taking `Event&`, with `template<typename T> Dispatch(auto func)` that:
   - Checks if the event type matches `T`
   - Calls `func(static_cast<T&>(e))`
   - ORs the callback return value into `Handled` via `|=`
   - Returns `true` if dispatched, `false` otherwise
4. Two concrete events:
   - `MouseClickEvent` with x, y, button fields
   - `KeyPressedEvent` with keyCode field

**Constraints:** No RTTI, no macros, no heavy dependencies, modern C++ templates, type-safe.

**Output format:** Complete, well-commented C++ code ready to be split into headers later.

---

#### Agent 2: CommandSystem_Architect
**Goal:** Produce the Command System header-only code.

**Deliverables:**
1. `ICommand` interface with `virtual void Execute() = 0;` and `virtual void Undo() = 0;`
2. `CommandHistory` class managing two stacks (or vectors) for undo/redo:
   - `Execute(std::unique_ptr<ICommand> cmd)` — executes and pushes to undo stack, clears redo stack
   - `Undo()` — pops from undo, undoes, pushes to redo
   - `Redo()` — pops from redo, re-executes, pushes to undo
   - `CanUndo()` / `CanRedo()` for query
   - Clear methods as needed
3. Concrete `MoveEntityCommand` demonstrating:
   - Constructor taking `entityID`, `oldPosition`, `newPosition`
   - `Execute()` moves entity to new position
   - `Undo()` reverts entity to old position
   - (Use a simple `EntityManager` stub or direct position storage for MVP)

**Constraints:** Use `std::unique_ptr<ICommand>` for ownership, no raw pointers, no global state, stack-based undo/redo.

**Output format:** Complete, well-commented C++ code ready to be split into headers later.

---

### Stage 2 — Integration Layer (depends on Stage 1 outputs)

#### Agent 3: EditorLayer_Integrator
**Goal:** Write the simulated EditorLayer demonstrating event flow and command usage.

**Deliverables:**
1. `EditorLayer` class with `OnEvent(Event& e)` method
2. Inside `OnEvent`, demonstrate top-down flow:
   - First: `EventDispatcher` intercepts for `InspectorPanel`
   - Only if `!e.Handled`, pass to `ViewportPanel`
3. Simulate `InspectorPanel` and `ViewportPanel` as lightweight inner structs/classes with simple `OnEvent` methods
4. Show a usage example in `main()` or `SimulateEditor()` function:
   - Create an `EditorLayer`
   - Dispatch a `MouseClickEvent` and show how it flows through panels
   - Create a `MoveEntityCommand`, execute via `CommandHistory`, then undo/redo

**Constraints:** Must use the exact class names and interfaces from Stage 1 outputs. No global singletons.

**Output format:** Complete, well-commented C++ code.

---

### Stage 3 — Integration
Orchestrator merges all three outputs into a single coherent file or structured header blocks. Ensures consistent naming, no duplicates, and clean separation of concerns. Final code compiles as a standalone C++17/20 program with a `main()` demonstration.

## Integration Notes
- All code uses `#pragma once` or include guards (prefer `#pragma once` for simplicity in MVP)
- Use `std::` consistently, no `using namespace std;` at global scope
- Prefer `std::unique_ptr` and `std::move` for ownership
- Use `constexpr` where appropriate
- Keep code structured so it can be split into `Event.h`, `EventDispatcher.h`, `Command.h`, `CommandHistory.h`, `EditorLayer.h`, etc.
