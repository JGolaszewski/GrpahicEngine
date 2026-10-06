#pragma once
#include <vector>
#include <DirectXMath.h>
#include "renderer/RenderPacket.h"
#include "renderer/DebugGeometry.h"
#include "renderer/Light.h"

// Forward declarations
namespace Engine { class Scene; class Viewport; }
namespace Renderer { class Camera; }

namespace Renderer {
	struct CameraGpuData {
		DirectX::XMFLOAT4 Position;
	};
	static_assert(sizeof(CameraGpuData) == 16);

	struct RenderQueues {
		std::vector<RenderPacket> Opaque;
		std::vector<RenderPacket> Masked;
		std::vector<RenderPacket> Transparent;
		std::vector<RenderPacket> Skybox;
		std::vector<RenderPacket> UI;

		void Clear() {
			Opaque.clear();
			Masked.clear();
			Transparent.clear();
			Skybox.clear();
			UI.clear();
		}
	};

	class ENGINE_API RenderView {
	public:
		enum class Type {
			Main,
			Shadow
		};

		Type ViewType = Type::Main;

		RenderQueues Queues;
		std::vector<DirectionalLightData> Lights;
		DebugRenderSettings DebugSettings;
		DebugGeometry DebugGeometryData;

		Engine::Scene* TargetScene = nullptr;
		nvrhi::IFramebuffer* TargetFramebuffer = nullptr;
		DirectX::XMFLOAT3 CameraPosition = { 0.0f, 0.0f, 0.0f };
		nvrhi::BufferHandle CameraBuffer;

		DirectX::XMMATRIX ViewMatrix = DirectX::XMMatrixIdentity();
		DirectX::XMMATRIX ProjectionMatrix = DirectX::XMMatrixIdentity();
		DirectX::XMMATRIX ViewProjection = DirectX::XMMatrixIdentity();

		uint32_t ViewportWidth = 0;
		uint32_t ViewportHeight = 0;

		DirectX::XMVECTOR FrustumPlanes[6] = {
			DirectX::XMVectorZero(), DirectX::XMVectorZero(), DirectX::XMVectorZero(),
			DirectX::XMVectorZero(), DirectX::XMVectorZero(), DirectX::XMVectorZero()
		};

		void Initialize(const Renderer::Camera* camera, const Engine::Viewport* viewport);
	
		void ExtractFrustumPlanes();
		bool IsSphereVisible(const DirectX::XMFLOAT3& center, float radius) const;

		void Clear() {
			Queues.Clear();
			Lights.clear();
			DebugGeometryData.Clear();
		}
	 };
}
