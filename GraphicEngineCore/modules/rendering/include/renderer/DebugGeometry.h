#pragma once

#include "rendering/include/renderer/DebugRenderSettings.h"
#include "runtime/include/CoreExport.h"
#include "scene/include/components/EcsComponents.h"

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // These engine DLL types intentionally expose same-toolchain member types.
#endif

namespace Engine {
class Scene;
} // namespace Engine

namespace Engine::Renderer {
/// Position and color for one line or triangle vertex.
struct ENGINE_API DebugVertex {
  DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f}; ///< World-space position.
  DirectX::XMFLOAT4 Color = {1.0f, 1.0f, 1.0f, 1.0f}; ///< Linear RGBA vertex color.
};

/// World-space bounds and direction used to draw a light gizmo.
struct ENGINE_API DebugLightTarget {
  Engine::LightId Id = Engine::InvalidLightId; ///< Scene light identity, or invalid when unavailable.
  DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f}; ///< World-space light position.
  DirectX::XMFLOAT3 Direction = {0.0f, -1.0f, 0.0f}; ///< World-space emission direction.
  float Range = 1.0f; ///< Maximum influence distance.
  float OuterAngleRadians = DirectX::XM_PIDIV4; ///< Outer cone angle for a spotlight.
};

/// Accumulates debug lines, triangles, and light-gizmo targets for a render view.
class ENGINE_API DebugGeometry {
public:
  /// Clears all accumulated debug geometry.
  void Clear();

  /// Adds a line segment.
  /// @param start World-space start position.
  /// @param end World-space end position.
  /// @param color Linear RGBA line color.
  void AddLine(const DirectX::XMFLOAT3& start, const DirectX::XMFLOAT3& end, const DirectX::XMFLOAT4& color);

  /// Adds a triangle.
  /// @param a First world-space vertex.
  /// @param b Second world-space vertex.
  /// @param c Third world-space vertex.
  /// @param color Linear RGBA vertex color shared by the triangle.
  void AddTriangle(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, const DirectX::XMFLOAT3& c,
                   const DirectX::XMFLOAT4& color);

  /// Adds a light target for later gizmo rendering.
  /// @param target Light identity and world-space gizmo parameters.
  void AddLightTarget(const DebugLightTarget& target);

  /// Gets the line-list vertices.
  /// @return Vertices remain valid until geometry is changed or cleared.
  const std::vector<DebugVertex>& GetLineVertices() const noexcept { return m_LineVertices; }
  /// Gets the triangle-list vertices.
  /// @return Vertices remain valid until geometry is changed or cleared.
  const std::vector<DebugVertex>& GetTriangleVertices() const noexcept { return m_TriangleVertices; }
  /// Gets the light targets.
  /// @return Targets remain valid until geometry is changed or cleared.
  const std::vector<DebugLightTarget>& GetLightTargets() const noexcept { return m_LightTargets; }

  /// Checks whether line and triangle vertex lists are empty.
  /// @return True when there are no lines or triangles.
  bool Empty() const noexcept { return m_LineVertices.empty() && m_TriangleVertices.empty(); }

private:
  std::vector<DebugVertex> m_LineVertices;
  std::vector<DebugVertex> m_TriangleVertices;
  std::vector<DebugLightTarget> m_LightTargets;
};

/// Builds debug geometry from scene state and editor render settings.
class ENGINE_API DebugGeometryBuilder {
public:
  /// Builds debug geometry from a scene and the requested debug settings.
  /// @param scene Scene whose visible objects supply debug geometry.
  /// @param settings Visibility and selection settings for debug elements.
  /// @param geometry Destination accumulator, cleared before population.
  static void BuildScene(const Engine::Scene& scene, const DebugRenderSettings& settings, DebugGeometry& geometry);

private:
  static void AddSpotLight(Engine::EntityHandle entity, const Engine::Ecs::SpotLight& light,
                           const Engine::Ecs::WorldTransform& transform, bool selected, DebugGeometry& geometry);
  static void AddCameraFrustum(const Engine::Ecs::Camera& camera, const Engine::Ecs::WorldTransform& transform,
                               DebugGeometry& geometry);
  static void AddWireframePlane(DebugGeometry& geometry);
};
} // namespace Engine::Renderer

#ifdef _MSC_VER
#  pragma warning(pop)
#endif
