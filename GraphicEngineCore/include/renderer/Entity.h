#pragma once
#include "CoreExport.h"
#include <DirectXMath.h>
#include <memory>
#include <nvrhi/nvrhi.h>
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include "components/Transform.h"

namespace Renderer {

	class ENGINE_API Entity {
	public:
		Entity(std::shared_ptr<Mesh> m, std::shared_ptr<Material> mat)
			: mesh(std::move(m)), material(std::move(mat)) {
		}

		Components::Transform transform;
		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<Material> material;

		bool ForceWireframe = false;
		bool DisableDepthTest = false;
		bool CastShadows = true;
		bool ReceiveShadows = true;

		nvrhi::RasterCullMode CullMode = nvrhi::RasterCullMode::Back;
	};

	class ENGINE_API EntityBuilder {
	public:
		EntityBuilder(nvrhi::DeviceHandle device, nvrhi::CommandListHandle commandList);

		EntityBuilder& Cube();
		EntityBuilder& Cone(uint32_t segments = 24);
		EntityBuilder& Sphere(uint32_t stacks = 16, uint32_t slices = 24);
		EntityBuilder& Plane();

		EntityBuilder& SetMaterial(std::shared_ptr<Renderer::Material> material);

		EntityBuilder& Scale(float x, float y, float z);
		EntityBuilder& Position(float x, float y, float z);

		std::shared_ptr<Renderer::Entity> Build();

	private:
		nvrhi::DeviceHandle m_Device;
		nvrhi::CommandListHandle m_CommandList;

		enum class Shape { None, Cube, Cone, Sphere, Plane };

		Shape m_Shape = Shape::None;
		uint32_t m_ConeSegments = 24;
		uint32_t m_SphereStacks = 16;
		uint32_t m_SphereSlices = 24;

		std::shared_ptr<Renderer::Material> m_Material;

		DirectX::XMFLOAT3 m_Position = { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT3 m_Scale = { 1.0f, 1.0f, 1.0f };

		void GenerateCubeData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices);
		void GenerateConeData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices, uint32_t segments);
		void GenerateSphereData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices, uint32_t stacks, uint32_t slices);
		void GeneratePlaneData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices);
	};

}