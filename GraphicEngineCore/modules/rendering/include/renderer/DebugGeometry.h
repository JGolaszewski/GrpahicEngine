#pragma once

#include "runtime/include/CoreExport.h"
#include "scene/include/SceneIds.h"
#include "rendering/include/renderer/DebugRenderSettings.h"

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine
{
class Scene;
} // namespace Engine

namespace Engine::Renderer
{
class SpotLight;

/// Position and color for a line or triangle in debug geometry.
struct ENGINE_API DebugVertex
{
    DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};    ///< World-space vertex position.
    DirectX::XMFLOAT4 Color = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Linear RGBA vertex color.
};

/// World-space spotlight data used to draw an editor light gizmo.
struct ENGINE_API DebugLightTarget
{
    Engine::LightId Id = Engine::InvalidLightId;       ///< Persistent scene light identifier.
    DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};   ///< World-space light position.
    DirectX::XMFLOAT3 Direction = {0.0f, -1.0f, 0.0f}; ///< Normalized world-space light direction.
    float Range = 1.0f;                                ///< Maximum world-space influence distance.
    float OuterAngleRadians = DirectX::XM_PIDIV4;      ///< Outer cone angle in radians.
};

/// CPU-side line, triangle, and light-gizmo geometry for debug rendering.
class ENGINE_API DebugGeometry
{
  public:
    /// Removes all debug primitives and light targets.
    void Clear();

    /// Appends a two-vertex line segment.
    /// @param start World-space start position.
    /// @param end World-space end position.
    /// @param color Linear RGBA line color.
    void AddLine(const DirectX::XMFLOAT3 &start, const DirectX::XMFLOAT3 &end,
                 const DirectX::XMFLOAT4 &color);

    /// Appends a three-vertex triangle.
    /// @param a First world-space vertex.
    /// @param b Second world-space vertex.
    /// @param c Third world-space vertex.
    /// @param color Linear RGBA triangle color.
    void AddTriangle(const DirectX::XMFLOAT3 &a, const DirectX::XMFLOAT3 &b, const DirectX::XMFLOAT3 &c,
                     const DirectX::XMFLOAT4 &color);

    /// Adds spotlight data for a renderer-built gizmo.
    /// @param target Light identifier, position, direction, range, and cone angle.
    void AddLightTarget(const DebugLightTarget &target);

    /// Returns the expanded line-list vertices.
    /// @return Read-only line vertex list.
    const std::vector<DebugVertex> &GetLineVertices() const noexcept
    {
        return m_LineVertices;
    }
    /// Returns the expanded triangle-list vertices.
    /// @return Read-only triangle vertex list.
    const std::vector<DebugVertex> &GetTriangleVertices() const noexcept
    {
        return m_TriangleVertices;
    }
    /// Returns the spotlight targets to visualize.
    /// @return Read-only light target list.
    const std::vector<DebugLightTarget> &GetLightTargets() const noexcept
    {
        return m_LightTargets;
    }

    /// Reports whether line and triangle geometry is empty.
    /// @return True when no line or triangle vertices were added.
    bool Empty() const noexcept
    {
        return m_LineVertices.empty() && m_TriangleVertices.empty();
    }

  private:
    std::vector<DebugVertex> m_LineVertices;
    std::vector<DebugVertex> m_TriangleVertices;
    std::vector<DebugLightTarget> m_LightTargets;
};

/// Builds common editor debug primitives from scene state and view settings.
class ENGINE_API DebugGeometryBuilder
{
  public:
    /// Creates light and construction-plane geometry for a scene.
    /// @param scene Scene whose lights may be visualized.
    /// @param settings Debug drawing options and selection state.
    /// @param geometry Destination geometry; existing contents are preserved.
    static void BuildScene(const Engine::Scene &scene, const DebugRenderSettings &settings,
                           DebugGeometry &geometry);

  private:
    static void AddSpotLight(const Engine::LightId id, const SpotLight &light, bool selected,
                             DebugGeometry &geometry);
    static void AddWireframePlane(DebugGeometry &geometry);
};
} // namespace Engine::Renderer

#ifdef _MSC_VER
#pragma warning(pop)
#endif
