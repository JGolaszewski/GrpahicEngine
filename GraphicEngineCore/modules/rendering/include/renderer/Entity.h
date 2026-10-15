#pragma once
#include "runtime/include/CoreExport.h"
#include "scene/include/components/Transform.h"
#include "rendering/include/renderer/RenderState.h"
#include <DirectXMath.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine::Renderer
{
class Material;
class Mesh;

/// Mesh/material pair and local bounds for one imported model part.
struct Submesh
{
    std::shared_ptr<Mesh> mesh;         ///< GPU mesh used to draw this part.
    std::shared_ptr<Material> material; ///< Material assigned to this part.
    DirectX::XMFLOAT4X4 LocalTransform = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f}; ///< Local-to-entity transform.
    DirectX::XMFLOAT3 BoundsMin{};                       ///< Minimum local-space bounds.
    DirectX::XMFLOAT3 BoundsMax{};                       ///< Maximum local-space bounds.
    int32_t MaterialSlot = -1;                           ///< Source material slot, or -1 when unassigned.
    mutable bool WorldBoundsCached = false;              ///< Whether transformed bounds are current.
    mutable DirectX::XMFLOAT4X4 CachedWorldTransform{};  ///< Last combined world transform used for bounds.
    mutable DirectX::XMFLOAT3 CachedBoundsMin{};         ///< Cached minimum world-space bound.
    mutable DirectX::XMFLOAT3 CachedBoundsMax{};         ///< Cached maximum world-space bound.
    mutable DirectX::XMFLOAT3 CachedWorldBoundsCenter{}; ///< Cached world-space bounding-sphere center.
    mutable float CachedWorldBoundsRadius = 0.0f;        ///< Cached world-space bounding-sphere radius.
};

/// Renderable object composed of one mesh or multiple imported submeshes.
class ENGINE_API Entity
{
  public:
    /// Creates an entity with its primary mesh and material.
    /// @param m Primary GPU mesh.
    /// @param mat Primary material.
    Entity(std::shared_ptr<Mesh> m, std::shared_ptr<Material> mat)
        : mesh(std::move(m)), material(std::move(mat))
    {
    }

    ::Engine::Components::Transform transform; ///< Entity world transform.
    std::shared_ptr<Mesh> mesh;                ///< Primary mesh for single-mesh entities.
    std::shared_ptr<Material> material;        ///< Primary material for single-mesh entities.
    std::vector<Submesh> Submeshes;            ///< Imported mesh parts with independent materials and bounds.
    std::string Name;                          ///< User-facing scene object name.
    std::optional<std::string> ModelGuid;      ///< Source model asset identifier, when imported.
    std::optional<std::string> MaterialGuid;   ///< Standalone material asset identifier, when assigned.
    std::optional<std::string> PrimitiveType;  ///< Primitive shape name for procedural entities.

    bool ForceWireframe = false;   ///< Whether polygon fill is forced to wireframe.
    bool DisableDepthTest = false; ///< Whether depth testing is disabled for this entity.
    bool CastShadows = true;       ///< Whether the entity is submitted to shadow passes.
    bool ReceiveShadows = true;    ///< Whether the entity receives shadowing.

    FaceCullMode CullMode = FaceCullMode::Back; ///< Face-culling policy for the entity.
};

} // namespace Engine::Renderer

#ifdef _MSC_VER
#pragma warning(pop)
#endif
