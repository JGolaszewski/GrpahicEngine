#pragma once

#include "runtime/include/CoreExport.h"
#include "runtime/include/events/Event.h"

#include <stdint.h>

namespace Engine {

class ENGINE_API IWindow {
public:
  virtual ~IWindow() = default;

  virtual void* GetNativeHandle() = 0;
  virtual void* GetEventSourceHandle() const = 0;

  virtual bool ShouldClose() const = 0;
  virtual void PollEvents() = 0;
  virtual void SetEventCallback(Events::EventCallbackFn callback) = 0;
  virtual void SetCursorVisible(bool visible) = 0;

  virtual uint32_t GetWidth() const = 0;
  virtual uint32_t GetHeight() const = 0;
};

} // namespace Engine
