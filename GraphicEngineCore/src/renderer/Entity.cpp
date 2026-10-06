#include "renderer/Entity.h"
#include "EntityBuilder.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace Renderer {

	EntityBuilder::EntityBuilder(nvrhi::DeviceHandle device, nvrhi::CommandListHandle commandList)
		: m_Device(device), m_CommandList(commandList) {
	}

	Renderer::EntityBuilder& EntityBuilder::Cube() {
		m_Shape = Shape::Cube;
		return *this;
	}

	Renderer::EntityBuilder& EntityBuilder::Cone(uint32_t segments) {
		m_Shape = Shape::Cone;
		m_ConeSegments = std::max<uint32_t>(3u, segments);
		return *this;
	}

	Renderer::EntityBuilder& EntityBuilder::Sphere(uint32_t stacks, uint32_t slices) {
		m_Shape = Shape::Sphere;
		m_SphereStacks = std::max<uint32_t>(2u, stacks);
		m_SphereSlices = std::max<uint32_t>(3u, slices);
		return *this;
	}

	Renderer::EntityBuilder& EntityBuilder::Plane() {
		m_Shape = Shape::Plane;
		return *this;
	}

	Renderer::EntityBuilder& EntityBuilder::SetMaterial(std::shared_ptr<Renderer::Material> material) {
		m_Material = material;
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

	std::shared_ptr<Renderer::Entity> EntityBuilder::Build() {
		if (!m_Device || !m_CommandList || !m_Material) {
			std::cerr << "ERROR: EntityBuilder requires a device, command list, and material." << std::endl;
			return nullptr;
		}

		std::vector<Renderer::Vertex> vertices;
		std::vector<uint32_t> indices;

		if (m_Shape == Shape::Cube) {
			GenerateCubeData(vertices, indices);
		}
		else if (m_Shape == Shape::Cone) {
			GenerateConeData(vertices, indices, m_ConeSegments);
		}
		else if (m_Shape == Shape::Sphere) {
			GenerateSphereData(vertices, indices, m_SphereStacks, m_SphereSlices);
		}
		else if (m_Shape == Shape::Plane) {
			GeneratePlaneData(vertices, indices);
		}
		else {
			std::cerr << "ERROR: EntityBuilder requires a shape before Build()." << std::endl;
			return nullptr;
		}

		auto mesh = std::make_shared<Renderer::Mesh>(m_Device, vertices, indices);

		mesh->UploadData(m_CommandList, vertices, indices);

		// The caller owns command-list submission. Build only records mesh uploads
		// so multiple entities can share one immediate-upload batch.
		auto entity = std::make_shared<Renderer::Entity>(mesh, m_Material);
		switch (m_Shape) {
		case Shape::Cube: entity->PrimitiveType = "cube"; break;
		case Shape::Cone: entity->PrimitiveType = "cone"; break;
		case Shape::Sphere: entity->PrimitiveType = "sphere"; break;
		case Shape::Plane: entity->PrimitiveType = "plane"; break;
		case Shape::None: break;
		}

		entity->transform.Position = m_Position;
		entity->transform.Scale = m_Scale;

		return entity;
	}

	void EntityBuilder::GenerateCubeData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
		// Zaktualizowano o poprawne współrzędne UV na każdą ścianę
		outVertices = {
			// Front
			{{ -0.5f,  0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 0.0f }},
			{{  0.5f,  0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 0.0f }},
			{{  0.5f, -0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 1.0f }},
			{{ -0.5f, -0.5f, -0.5f }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 1.0f }},
			// Back
			{{  0.5f,  0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 0.0f }},
			{{ -0.5f,  0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 0.0f }},
			{{ -0.5f, -0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 1.0f }},
			{{  0.5f, -0.5f,  0.5f }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 1.0f }},
			// Top
			{{ -0.5f,  0.5f,  0.5f }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 0.0f }},
			{{  0.5f,  0.5f,  0.5f }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 0.0f }},
			{{  0.5f,  0.5f, -0.5f }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 1.0f }},
			{{ -0.5f,  0.5f, -0.5f }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 1.0f }},
			// Bottom
			{{ -0.5f, -0.5f, -0.5f }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 0.0f }},
			{{  0.5f, -0.5f, -0.5f }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 0.0f }},
			{{  0.5f, -0.5f,  0.5f }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 1.0f }},
			{{ -0.5f, -0.5f,  0.5f }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 1.0f }},
			// Right
			{{  0.5f,  0.5f, -0.5f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f }},
			{{  0.5f,  0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f }},
			{{  0.5f, -0.5f,  0.5f }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f }},
			{{  0.5f, -0.5f, -0.5f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f }},
			// Left
			{{ -0.5f,  0.5f,  0.5f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f }},
			{{ -0.5f,  0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f }},
			{{ -0.5f, -0.5f, -0.5f }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f }},
			{{ -0.5f, -0.5f,  0.5f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f }}
		};

		for (int i = 0; i < 6; ++i) {
			uint32_t offset = i * 4;
			outIndices.push_back(offset + 0); outIndices.push_back(offset + 1); outIndices.push_back(offset + 2);
			outIndices.push_back(offset + 0); outIndices.push_back(offset + 2); outIndices.push_back(offset + 3);
		}
	}

	void EntityBuilder::GenerateConeData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices, uint32_t segments) {
		outVertices.clear();
		outIndices.clear();

		// Szczyt stożka (Apex)
		outVertices.push_back({ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0.5f, 0.0f } });

		constexpr float twoPi = 6.2831853071795864769f;
		for (uint32_t i = 0; i < segments; ++i) {
			const float u = static_cast<float>(i) / static_cast<float>(segments);
			const float t = u * twoPi;
			const float x = std::cos(t);
			const float y = std::sin(t);
			const DirectX::XMFLOAT3 normal = DirectX::XMFLOAT3(x, y, 1.0f);

			outVertices.push_back({ { x, y, 1.0f }, normal, { u, 1.0f } });
		}

		// Środek podstawy
		outVertices.push_back({ { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.5f, 0.5f } });
		const uint32_t baseCenter = static_cast<uint32_t>(outVertices.size() - 1);

		for (uint32_t i = 0; i < segments; ++i) {
			const uint32_t a = 1 + i;
			const uint32_t b = 1 + ((i + 1) % segments);

			outIndices.push_back(0);
			outIndices.push_back(a);
			outIndices.push_back(b);

			outIndices.push_back(baseCenter);
			outIndices.push_back(b);
			outIndices.push_back(a);
		}
	}

	void EntityBuilder::GenerateSphereData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices, uint32_t stacks, uint32_t slices) {
		outVertices.clear();
		outIndices.clear();

		constexpr float pi = 3.14159265358979323846f;
		constexpr float twoPi = 6.2831853071795864769f;

		for (uint32_t stack = 0; stack <= stacks; ++stack) {
			const float v = static_cast<float>(stack) / static_cast<float>(stacks);
			const float phi = v * pi;

			const float y = std::cos(phi);
			const float r = std::sin(phi);

			for (uint32_t slice = 0; slice <= slices; ++slice) {
				const float u = static_cast<float>(slice) / static_cast<float>(slices);
				const float theta = u * twoPi;

				const float x = r * std::cos(theta);
				const float z = r * std::sin(theta);

				// UV Sphere Map
				outVertices.push_back({ { x, y, z }, { x, y, z }, { u, v } });
			}
		}

		const uint32_t ringVertexCount = slices + 1;
		for (uint32_t stack = 0; stack < stacks; ++stack) {
			for (uint32_t slice = 0; slice < slices; ++slice) {
				const uint32_t i0 = stack * ringVertexCount + slice;
				const uint32_t i1 = i0 + 1;
				const uint32_t i2 = i0 + ringVertexCount;
				const uint32_t i3 = i2 + 1;

				outIndices.push_back(i0);
				outIndices.push_back(i1);
				outIndices.push_back(i2);

				outIndices.push_back(i1);
				outIndices.push_back(i3);
				outIndices.push_back(i2);
			}
		}
	}

	void EntityBuilder::GeneratePlaneData(std::vector<Renderer::Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
		outVertices = {
			{{ -0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f }},
			{{  0.5f, 0.0f, -0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f }},
			{{  0.5f, 0.0f,  0.5f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f }},
			{{ -0.5f, 0.0f,  0.5f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f }}
		};

		outIndices = {
			0, 2, 1,
			0, 3, 2
		};
	}

}
