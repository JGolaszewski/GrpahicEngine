#pragma once

#include "CoreExport.h"

#include <stdint.h>

namespace Engine {

	class ENGINE_API IWindow {
	public:
		
		virtual ~IWindow() = default;

		virtual void* GetNativeHandle() = 0;

		virtual bool ShouldClose() const = 0;
		virtual void PollEvents() = 0;

		virtual void InitImGui() = 0;
		virtual void ImGuiNewFrame() = 0;

		virtual uint32_t GetWidth() const = 0;
		virtual uint32_t GetHeight() const = 0;
	};

}