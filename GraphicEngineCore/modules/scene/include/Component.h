#pragma once

#include "runtime/include/CoreExport.h"

namespace Engine::Components
{
/// Base class for behavior attached to scene objects.
class ENGINE_API Component
{
  public:
    /// Releases component resources through the polymorphic interface.
    virtual ~Component() = default;

    /// Updates component behavior for one frame.
    /// @param deltaTime Elapsed frame time in seconds.
    virtual void Update(float deltaTime)
    {
        (void)deltaTime;
    }
};
} // namespace Engine::Components
