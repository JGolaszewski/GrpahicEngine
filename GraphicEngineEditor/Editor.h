#pragma once

#include "Application.h"
#include "panels/EditorPanel.h"
#include <imgui.h>

#include "renderer/Camera.h"
#include "renderer/Entity.h"
#include "renderer/Light.h"
#include "renderer/Material.h"
#include "renderer/Mesh.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <utility>

#include <wrl/client.h>
#include <d3d11.h>

#include "commands/CommandHistory.h"

namespace Engine::Events {
    class Event;
}

class Editor : public Engine::Application {
public:
    Editor() = default;
    ~Editor() override = default;

protected:
    void OnInit() override;
    void OnRender() override;
    void OnUpdate(float deltaTime) override;
    void OnUIRender() override;
    void OnResize(uint32_t width, uint32_t height) override;
    void OnEvent(Engine::Events::Event& event) override;

    nvrhi::IFramebuffer* GetSceneRenderFramebuffer() const override;

private:
    enum class SelectedItemType {
        None,
        Object,
        Light,
        Camera
    };

    void CreateViewport(uint32_t width, uint32_t height);
    void DestroyViewport();
    void* GetViewportTextureID() const;

    void UpdateProjection();
    void UpdateProjection(uint32_t width, uint32_t height);
    void RebuildDebugGizmos();

    void BuildDefaultDockLayout(ImGuiID dockspaceId);
    void BuildDefaultDockLayoutIfNeeded(ImGuiID dockspaceId);

    // ------------------------------------------------------------------------
    // Event hub — panels communicate exclusively through these methods
    // ------------------------------------------------------------------------
    void ProcessPanelEvent(std::unique_ptr<Engine::Events::Event> event);
    void BroadcastEvent(Engine::Events::Event& event);

    void BroadcastSceneChanged();
    void BroadcastCameraSettings();
    void BroadcastViewportChanged();

    // ------------------------------------------------------------------------
    // Undo / Redo
    // ------------------------------------------------------------------------
    void Undo();
    void Redo();
    [[nodiscard]] CommandHistory& GetCommandHistory() noexcept { return m_CommandHistory; }

    std::vector<std::unique_ptr<EditorPanel>> m_Windows;

    std::vector<std::unique_ptr<Renderer::Entity>> m_Cubes;
    std::vector<Renderer::SpotLight>               m_Lights;
    std::vector<std::shared_ptr<Renderer::Camera>> m_Cameras;

    std::array<bool, static_cast<size_t>(Engine::Events::KeyCode::Count)>     m_KeyDown{};
    std::array<bool, static_cast<size_t>(Engine::Events::MouseButton::Count)> m_MouseButtonDown{};

    float m_MouseDeltaX = 0.0f;
    float m_MouseDeltaY = 0.0f;
    float m_Yaw         = 0.0f;
    float m_Pitch       = 0.0f;

    float m_CameraFov       = DirectX::XM_PIDIV4;
    float m_CameraNearPlane = 0.1f;
    float m_CameraFarPlane  = 100.0f;
    bool  m_CursorHidden    = false;

    SelectedItemType m_SelectedItemType    = SelectedItemType::None;
    int              m_SelectedObjectIndex = -1;
    int              m_SelectedLightIndex  = -1;
    int              m_SelectedCameraIndex = -1;

    nvrhi::ShaderHandle m_DefaultVS;
    nvrhi::ShaderHandle m_DefaultPS;

    nvrhi::TextureHandle      m_ViewportColorTexture;
    nvrhi::TextureHandle      m_ViewportDepthTexture;
    nvrhi::FramebufferHandle  m_ViewportFramebuffer;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_ViewportSRV;
    std::pair<float, float>   m_ViewportSize      = { 0.0f, 0.0f };
    bool                      m_ViewportHovered   = false;
    bool                      m_ViewportResized   = false;
    bool                      m_DefaultLayoutBuilt = false;

    CommandHistory m_CommandHistory;

#if defined(_DEBUG)
    bool m_ShowDebugLightGizmos    = true;
    bool m_ShowDebugWireframePlane = true;
    std::vector<std::unique_ptr<Renderer::Entity>> m_DebugLightGizmos;
    std::unique_ptr<Renderer::Entity>              m_DebugWireframePlane;
#endif
};