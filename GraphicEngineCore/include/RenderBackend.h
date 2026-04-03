#pragma once

#include "CoreExport.h"
#include "Window.h"

#include <nvrhi/nvrhi.h>

namespace Engine {

	class ENGINE_API IRenderBackend {
	public:
		virtual ~IRenderBackend() = default;

		virtual bool Initialize(IWindow* window) = 0;
		virtual void Present() = 0;
		virtual void Resize(int width, int height) = 0;

		virtual nvrhi::IDevice* GetDevice() const = 0;
		virtual nvrhi::IFramebuffer* GetDefaultFramebuffer() const = 0;

		virtual void InitImGui(IWindow* window) = 0;
		virtual void BeginImGuiFrame() = 0;
		virtual void RenderImGui() = 0;
	};
}