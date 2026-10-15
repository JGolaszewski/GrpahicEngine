#pragma once
#include "runtime/include/CoreExport.h"
#include "scene/include/Object.h"
#include "rendering/include/renderer/Material.h"
#include "rendering/include/renderer/Mesh.h"
#include <DirectXMath.h>
#include <memory>
#include <nvrhi/nvrhi.h>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{
struct Submesh
{
    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;
    DirectX::XMFLOAT4X4 LocalTransform = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                                          0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    DirectX::XMFLOAT3 BoundsMin{};
    DirectX::XMFLOAT3 BoundsMax{};
    int32_t MaterialSlot = -1;
    mutable bool WorldBoundsCached = false;
    mutable DirectX::XMFLOAT4X4 CachedWorldTransform{};
    mutable DirectX::XMFLOAT3 CachedBoundsMin{};
    mutable DirectX::XMFLOAT3 CachedBoundsMax{};
    mutable DirectX::XMFLOAT3 CachedWorldBoundsCenter{};
    mutable float CachedWorldBoundsRadius = 0.0f;
};

class ENGINE_API Entity : public ::Engine::Object
{
  public:
    Entity(std::shared_ptr<Mesh> m, std::shared_ptr<Material> mat)
        : mesh(std::move(m)), material(std::move(mat))
    {
    }

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

} // namespace Engine::Renderer

#ifdef _MSC_VER
#pragma warning(pop)
#endif
