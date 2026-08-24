#pragma once
#include <vector>
#include <DirectXMath.h>
#include "renderer/RenderPacket.h"

// Forward declarations
namespace Engine { class Scene; class Viewport; }
namespace Renderer { class Camera; }

namespace Renderer {

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

		Engine::Scene* TargetScene = nullptr;
		nvrhi::IFramebuffer* TargetFramebuffer = nullptr;

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

		void Clear() {
			Queues.Clear();
		}
	};
}
