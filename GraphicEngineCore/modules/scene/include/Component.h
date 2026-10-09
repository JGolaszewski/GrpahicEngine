#pragma once

#include "runtime/include/CoreExport.h"

namespace Engine::Components {
class ENGINE_API Component {
public:
  virtual ~Component() = default;
  virtual void Update(float deltaTime) { (void)deltaTime; }
};
} // namespace Engine::Components
