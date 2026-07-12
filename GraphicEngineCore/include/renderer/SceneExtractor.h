#pragma once

#include "CoreExport.h"

namespace Renderer {

	// TODO: Add Linear Allocator
	class ENGINE_API SceneExtractor {

		static void RenderSystem::ExtractView(Scene* scene, Camera* camera, std::vector<RenderPacket>& packets);

		static void RenderSystem::ExtractDirectionalLight(Scene* scene, Renderer::DirectionalLightData& light);

	}
}