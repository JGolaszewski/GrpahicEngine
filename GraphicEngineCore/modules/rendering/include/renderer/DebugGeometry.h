#pragma once

#include "runtime/include/CoreExport.h"
#include "scene/include/SceneIds.h"
#include "rendering/include/renderer/DebugRenderSettings.h"

#include <DirectXMath.h>
#include <cstdint>
#include <vector>

namespace Engine
{
class Scene;
} // namespace Engine

namespace Engine::Renderer
{
class SpotLight;

struct ENGINE_API DebugVertex
{
    DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT4 Color = {1.0f, 1.0f, 1.0f, 1.0f};
};

struct ENGINE_API DebugLightTarget
{
    Engine::LightId Id = Engine::InvalidLightId;
    DirectX::XMFLOAT3 Position = {0.0f, 0.0f, 0.0f};
    DirectX::XMFLOAT3 Direction = {0.0f, -1.0f, 0.0f};
    float Range = 1.0f;
    float OuterAngleRadians = DirectX::XM_PIDIV4;
};

class ENGINE_API DebugGeometry
{
  public:
    void Clear();

    void AddLine(const DirectX::XMFLOAT3 &start, const DirectX::XMFLOAT3 &end,
                 const DirectX::XMFLOAT4 &color);
    void AddTriangle(const DirectX::XMFLOAT3 &a, const DirectX::XMFLOAT3 &b, const DirectX::XMFLOAT3 &c,
                     const DirectX::XMFLOAT4 &color);
    void AddLightTarget(const DebugLightTarget &target);

    const std::vector<DebugVertex> &GetLineVertices() const noexcept
    {
        return m_LineVertices;
    }
    const std::vector<DebugVertex> &GetTriangleVertices() const noexcept
    {
        return m_TriangleVertices;
    }
    const std::vector<DebugLightTarget> &GetLightTargets() const noexcept
    {
        return m_LightTargets;
    }

    bool Empty() const noexcept
    {
        return m_LineVertices.empty() && m_TriangleVertices.empty();
    }

  private:
    std::vector<DebugVertex> m_LineVertices;
    std::vector<DebugVertex> m_TriangleVertices;
    std::vector<DebugLightTarget> m_LightTargets;
};

class ENGINE_API DebugGeometryBuilder
{
  public:
    static void BuildScene(const Engine::Scene &scene, const DebugRenderSettings &settings,
                           DebugGeometry &geometry);

  private:
    static void AddSpotLight(const Engine::LightId id, const SpotLight &light, bool selected,
                             DebugGeometry &geometry);
    static void AddWireframePlane(DebugGeometry &geometry);
};
} // namespace Engine::Renderer
