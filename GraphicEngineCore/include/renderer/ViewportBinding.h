#pragma once

#include <type_traits>
#include "CoreExport.h"
#include "renderer/DebugGeometry.h"

namespace Renderer { class Camera; }
namespace Engine {
	class Viewport;
	class Scene;
}

namespace Engine {

	struct ENGINE_API ViewportBinding {
		Renderer::Camera* SourceCamera = nullptr;
		Engine::Viewport* TargetViewport = nullptr;
		Engine::Scene* TargetScene = nullptr;

		bool ClearTarget = true;
	
		// TODO: Implement render mask functionality in the future. For now, it is set to 0xFFFFFFFF to render everything.
		uint32_t RenderMask = 0xFFFFFFFF;

		Renderer::DebugRenderSettings DebugSettings;

		bool IsValid() const {
			return SourceCamera != nullptr && TargetViewport != nullptr && TargetScene != nullptr;
		}
	};
}
