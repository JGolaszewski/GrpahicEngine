#include "renderer/Entity.h"

namespace Renderer {


	EntityBuilder::EntityBuilder(nvrhi::DeviceHandle device, nvrhi::CommandListHandle commandList)
		: m_Device(device), m_CommandList(commandList) {}

	Renderer::EntityBuilder& EntityBuilder::Cube() {
		m_Shape = Shape::Cube;
		return *this;
	}

	Renderer::EntityBuilder& EntityBuilder::Material(nvrhi::ShaderHandle vs, nvrhi::ShaderHandle ps) {
		m_VS = vs;
		m_PS = ps;
		return *this;
	}

	Renderer::EntityBuilder& EntityBuilder::Color(float r, float g, float b, float a) {
		m_Color = { r, g, b, a };
		return *this;
	}

	Renderer::EntityBuilder& EntityBuilder::Scale(float x, float y, float z) {
		m_Scale = { x, y, z };
		return *this;
	}

	Renderer::EntityBuilder& EntityBuilder::Position(float x, float y, float z) {
		m_Position = { x, y, z };
		return *this;
	}

	std::unique_ptr<Renderer::Entity> EntityBuilder::Build() {
		std::vector<Renderer::Vertex> vertices;
		std::vector<uint32_t> indices;

		if(m_Shape == Shape::Cube) {
			GenerateCubeData(vertices, indices);
		}

		auto mesh = std::make_shared<Renderer::Mesh>(m_Device, vertices, indices);

		mesh->UploadData(m_CommandList, vertices, indices);
		m_Device->executeCommandList(m_CommandList);

		auto material = std::make_shared<Renderer::Material>(m_Device, m_VS, m_PS);
		material->SetColor(m_Color, m_CommandList);

		auto entity = std::make_unique<Renderer::Entity>(mesh, material);

		entity->transform.Position = m_Position;
		entity->transform.Scale = m_Scale;

		return entity;
	}

	void EntityBuilder::GenerateCubeData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
		outVertices = {
			{{ -0.5f,  0.5f, -0.5f }}, {{  0.5f,  0.5f, -0.5f }}, {{  0.5f, -0.5f, -0.5f }}, {{ -0.5f, -0.5f, -0.5f }},
			{{  0.5f,  0.5f,  0.5f }}, {{ -0.5f,  0.5f,  0.5f }}, {{ -0.5f, -0.5f,  0.5f }}, {{  0.5f, -0.5f,  0.5f }},
			{{ -0.5f,  0.5f,  0.5f }}, {{  0.5f,  0.5f,  0.5f }}, {{  0.5f,  0.5f, -0.5f }}, {{ -0.5f,  0.5f, -0.5f }},
			{{ -0.5f, -0.5f, -0.5f }}, {{  0.5f, -0.5f, -0.5f }}, {{  0.5f, -0.5f,  0.5f }}, {{ -0.5f, -0.5f,  0.5f }},
			{{  0.5f,  0.5f, -0.5f }}, {{  0.5f,  0.5f,  0.5f }}, {{  0.5f, -0.5f,  0.5f }}, {{  0.5f, -0.5f, -0.5f }},
			{{ -0.5f,  0.5f,  0.5f }}, {{ -0.5f,  0.5f, -0.5f }}, {{ -0.5f, -0.5f, -0.5f }}, {{ -0.5f, -0.5f,  0.5f }}
		};

		for(int i = 0; i < 6; ++i) {
			uint32_t offset = i * 4;
			outIndices.push_back(offset + 0); outIndices.push_back(offset + 1); outIndices.push_back(offset + 2);
			outIndices.push_back(offset + 0); outIndices.push_back(offset + 2); outIndices.push_back(offset + 3);
		}
	}

}