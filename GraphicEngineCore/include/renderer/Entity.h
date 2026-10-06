#pragma once
#include "CoreExport.h"
#include <DirectXMath.h>
#include <memory>
#include <optional>
#include <nvrhi/nvrhi.h>
#include <string>
#include <vector>
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include "components/Transform.h"

namespace Renderer {
	struct Submesh {
		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<Material> material;
		DirectX::XMFLOAT4X4 LocalTransform = {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f
		};
		DirectX::XMFLOAT3 BoundsMin{};
		DirectX::XMFLOAT3 BoundsMax{};
		int32_t MaterialSlot = -1;
	};

	class ENGINE_API Entity {
	public:
		Entity(std::shared_ptr<Mesh> m, std::shared_ptr<Material> mat)
			: mesh(std::move(m)), material(std::move(mat)) {
		}

		Components::Transform transform;
		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<Material> material;
		std::vector<Submesh> Submeshes;
		std::string Name;
		std::optional<std::string> ModelGuid;
		std::optional<std::string> MaterialGuid;
		std::optional<std::string> PrimitiveType;

		bool ForceWireframe = false;
		bool DisableDepthTest = false;
		bool CastShadows = true;
		bool ReceiveShadows = true;

		nvrhi::RasterCullMode CullMode = nvrhi::RasterCullMode::Back;
	};

}
