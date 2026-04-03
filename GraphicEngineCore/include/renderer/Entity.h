#pragma once

#include "CoreExport.h"

#include <DirectXMath.h>
#include <memory>
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include "components/Transform.h"

namespace Renderer {
	class ENGINE_API Entity {
	public:
		Entity(std::shared_ptr<Mesh> m, std::shared_ptr<Material> mat)
			: mesh(std::move(m)), material(std::move(mat)) {}

		Components::Transform transform;
		std::shared_ptr<Mesh> mesh;
		std::shared_ptr<Material> material;
	};

	class ENGINE_API EntityBuilder {
	public:
		EntityBuilder(nvrhi::DeviceHandle device, nvrhi::CommandListHandle commandList);

		EntityBuilder& Cube();

		EntityBuilder& Material(nvrhi::ShaderHandle vs, nvrhi::ShaderHandle ps);
		EntityBuilder& Color(float r, float g, float b, float a);
		EntityBuilder& Scale(float x, float y, float z);
		EntityBuilder& Position(float x, float y, float z);

		std::unique_ptr<Renderer::Entity> Build();

	private:
		nvrhi::DeviceHandle m_Device;
		nvrhi::CommandListHandle m_CommandList;

		enum class Shape { None, Cube };
		Shape m_Shape = Shape::None;

		nvrhi::ShaderHandle m_VS;
		nvrhi::ShaderHandle m_PS;
		DirectX::XMFLOAT4 m_Color = { 1.0f, 1.0f, 1.0f, 1.0f };

		DirectX::XMFLOAT3 m_Position = { 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT3 m_Scale = { 1.0f, 1.0f, 1.0f };

		void GenerateCubeData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices);
	};

}