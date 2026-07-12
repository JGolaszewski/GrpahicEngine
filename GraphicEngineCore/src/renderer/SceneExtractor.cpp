#include "SceneExtractor.h"

// TODO: Linear Allocator + std::span !!
void SceneExtractor::ExtractView(Scene* scene, Camera* camera, std::vector<RenderPacket>& packets)
{
	packets.clear();
	packets.reserve(scene->GetEntityCount());

	uint32_t visibleCount = 0;

	for (const auto& entity : scene->GetEntities()) {
		// TODO: Check if entity in camera frustum

		RenderPacket packet;
		packet.WorldMatrix = entity->transform.GetWorldMat();
		packet.VertexBuffer = entity->mesh->GetVertexBuffer();

		packets.push_back(packet);
	}

	return std::span<RenderPacket>(packets, visibleCount);
}

void SceneExtractor::ExtractDirectionalLight(Scene* scene, Renderer::DirectionalLightData& light) {

}
