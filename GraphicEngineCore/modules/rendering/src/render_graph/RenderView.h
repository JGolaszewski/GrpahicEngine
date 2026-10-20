#pragma once
#include "rendering/include/renderer/CameraView.h"
#include "rendering/include/renderer/DebugGeometry.h"
#include "rendering/include/renderer/Light.h"
#include "rendering/include/renderer/ViewportBinding.h"
#include "rendering/src/RenderPacket.h"
#include "rendering/src/render_graph/RGResources.h"
#include "scene/include/ArchetypeWorld.h"
#include <DirectXMath.h>
#include <vector>

// Forward declarations
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine {
class Scene;
class Viewport;
} // namespace Engine
namespace Engine::Renderer {
enum class RGAttachmentLoadOp { Load, Clear, Discard };
enum class RGAttachmentStoreOp { Store, Discard };

struct RGViewportAttachment {
  RGResourceID Resource;
  nvrhi::TextureSubresourceSet Subresources = nvrhi::AllSubresources;
  RGAttachmentLoadOp Load = RGAttachmentLoadOp::Clear;
  RGAttachmentStoreOp Store = RGAttachmentStoreOp::Store;
  nvrhi::Color ClearColor = nvrhi::Color(0.15f, 0.15f, 0.15f, 1.0f);
  bool IsDepth = false;
};

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

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4324) // DirectX SIMD members require this alignment; RenderView is not a GPU buffer type.
#endif
class ENGINE_API RenderView {
public:
  enum class Type { Main, Shadow };

  Type ViewType = Type::Main;

  RenderQueues Queues;
  std::vector<DirectionalLightData> Lights;
  DebugRenderSettings DebugSettings;
  DebugGeometry DebugGeometryData;

  Engine::Scene* TargetScene = nullptr;
  Engine::EntityHandle ShadowLight = Engine::InvalidEntityHandle; ///< Entity owning this shadow view's light.
  nvrhi::IFramebuffer* TargetFramebuffer = nullptr;
  nvrhi::IFramebuffer* DisplayFramebuffer = nullptr;
  nvrhi::TextureHandle DisplayColorTexture;
  RGResourceID DisplayColorResource;
  Engine::ViewportBinding::Mode RenderMode = Engine::ViewportBinding::Mode::Editor;
  Engine::EnvironmentLightingSettings Environment;
  std::vector<RGViewportAttachment> OutputAttachments;
  DirectX::XMFLOAT3 CameraPosition = {0.0f, 0.0f, 0.0f};
  nvrhi::BufferHandle CameraBuffer;
  nvrhi::BufferHandle LightBuffer;

  DirectX::XMMATRIX ViewMatrix = DirectX::XMMatrixIdentity();
  DirectX::XMMATRIX ProjectionMatrix = DirectX::XMMatrixIdentity();
  DirectX::XMMATRIX ViewProjection = DirectX::XMMatrixIdentity();

  uint32_t ViewportWidth = 0;
  uint32_t ViewportHeight = 0;

  DirectX::XMVECTOR FrustumPlanes[6] = {DirectX::XMVectorZero(), DirectX::XMVectorZero(), DirectX::XMVectorZero(),
                                        DirectX::XMVectorZero(), DirectX::XMVectorZero(), DirectX::XMVectorZero()};

  void Initialize(const Engine::Scene* scene, const Engine::Renderer::CameraView& camera,
                  const Engine::Viewport* viewport);

  void ExtractFrustumPlanes();
  bool IsSphereVisible(const DirectX::XMFLOAT3& center, float radius) const;
  void Clear() {
    Queues.Clear();
    Lights.clear();
    DebugGeometryData.Clear();
  }
};
#ifdef _MSC_VER
#  pragma warning(pop)
#endif
} // namespace Engine::Renderer

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
