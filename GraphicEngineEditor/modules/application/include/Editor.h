#pragma once

#include "runtime/include/Application.h"
#include "application/include/EditorEventRouter.h"
#include "runtime/include/EditorCameraController.h"
#include "application/include/EditorSceneDocument.h"
#include "commands/include/CommandHistory.h"
#include "events/include/AssetImportRequestedEvent.h"
#include "events/include/SceneCreationResult.h"
#include "events/include/SceneFileActionRequestedEvent.h"
#include "panels/include/EditorPanel.h"
#include "panels/include/EditorConsole.h"
#include "platform/include/EditorUiIntegration.h"
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <imgui.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Engine::Events
{
class Event;
}

namespace Engine::Renderer
{
class Camera;
}

namespace Engine
{
class Scene;
class Viewport;
class ViewportTarget;
} // namespace Engine

class Editor : public Engine::Application
{
  public:
    Editor() = default;
    ~Editor() override = default;

  protected:
    void OnInit() override;
    void OnRender() override;
    void OnUpdate(float deltaTime) override;
    void OnUIRender() override;
    void OnBeginUIFrame() override;
    void OnEndUIFrame() override;
    void OnShutdownUI() override;
    void OnResize(uint32_t width, uint32_t height) override;
    void OnEvent(Engine::Events::Event &event) override;

    void OnSetupViews() override;
    void OnSceneReplaced(const std::shared_ptr<Engine::Scene> &scene) override;

  private:
    enum class SelectedItemType
    {
        None,
        Object,
        Light,
        Camera
    };

    void BuildDefaultDockLayout(ImGuiID dockspaceId);
    void BuildDefaultDockLayoutIfNeeded(ImGuiID dockspaceId);

    // ------------------------------------------------------------------------
    // Event hub — panels communicate exclusively through these methods
    // ------------------------------------------------------------------------
    void ProcessPanelEvent(std::unique_ptr<Engine::Events::Event> event);
    void BroadcastEvent(Engine::Events::Event &event);

    void BroadcastSceneChanged();
    void BroadcastCameraSettings();
    void BroadcastViewportChanged();

    // ------------------------------------------------------------------------
    // Undo / Redo
    // ------------------------------------------------------------------------
    void Undo();
    void Redo();
    void RequestSceneAction(SceneFileActionRequestedEvent::Action action);
    bool SaveCurrentScene(bool choosePath);
    void DrawUnsavedChangesModal();
    void OnEntityMembershipChanged(Engine::EntityId id,
                                   const std::shared_ptr<::Engine::Renderer::Entity> &entity, bool present);
    bool AssignTexture(Engine::EntityId entityId, int32_t materialSlot,
                       AssetImportRequestedEvent::TextureSemantic semantic,
                       const std::optional<std::string> &textureGuid);
    void ReimportAsset(const std::string &guid);
    void ReassignModel(Engine::EntityId entityId, const std::string &guid);
    bool AddModelToScene(const std::string &guid, const DirectX::XMFLOAT3 &position,
                         const DirectX::XMFLOAT3 &scale, std::string &error, const std::string &name = {});
    bool CommitCreatedEntity(const std::shared_ptr<::Engine::Renderer::Entity> &entity, std::string &error);
    struct SelectionSnapshot
    {
        SelectedItemType Type = SelectedItemType::None;
        Engine::EntityId EntityId = Engine::InvalidEntityId;
        Engine::LightId LightId = Engine::InvalidLightId;
        Engine::CameraId CameraId = Engine::InvalidCameraId;
    };
    SelectionSnapshot CaptureSelection() const noexcept;
    void RestoreSelection(const SelectionSnapshot &selection);
    CommandHistory &GetCommandHistory() noexcept
    {
        return m_CommandHistory;
    }

    // ------------------------------------------------------------------------
    // Viewport Management
    // ------------------------------------------------------------------------
    void CreateViewport(uint32_t width, uint32_t height);
    void DestroyViewport();
    void *GetViewportTextureID() const;
    void UpdateProjection();
    void UpdateProjection(uint32_t width, uint32_t height);
    std::shared_ptr<::Engine::Renderer::Camera> GetActiveCamera() const;

    // ------------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------------
    std::shared_ptr<Engine::Scene> m_ActiveScene;
    std::shared_ptr<Engine::Viewport> m_EditorViewport;
    std::shared_ptr<Engine::ViewportTarget> m_EditorViewportTarget;
    Engine::CameraId m_ActiveCameraId = Engine::InvalidCameraId;

    std::vector<std::unique_ptr<EditorPanel>> m_Windows;
    EditorEventRouter m_EventRouter;
    std::vector<EditorEventRouter::Connection> m_PanelConnections;

    std::pair<float, float> m_ViewportSize = {0.0f, 0.0f};

    std::array<bool, static_cast<size_t>(Engine::Events::KeyCode::Count)> m_KeyDown{};
    std::array<bool, static_cast<size_t>(Engine::Events::MouseButton::Count)> m_MouseButtonDown{};

    float m_CameraFov = 0.785398f; // XM_PIDIV4
    float m_CameraNearPlane = 0.1f;
    float m_CameraFarPlane = 100.0f;
    bool m_CursorHidden = false;

    SelectedItemType m_SelectedItemType = SelectedItemType::None;
    Engine::EntityId m_SelectedEntityId = Engine::InvalidEntityId;
    Engine::LightId m_SelectedLightId = Engine::InvalidLightId;
    Engine::CameraId m_SelectedCameraId = Engine::InvalidCameraId;

    bool m_ViewportHovered = false;
    bool m_DefaultLayoutBuilt = false;
    bool m_EngineConsoleOpen = true;
    bool m_ShowDebugLightGizmos = true;
    bool m_ShowDebugWireframePlane = true;

    Engine::EditorCameraController m_CameraController;
    EditorUiIntegration m_UiIntegration;
    EditorConsole m_EditorConsole;

    CommandHistory m_CommandHistory;
    EditorSceneDocument m_SceneDocument;
    std::string m_FileError;
    struct PendingUnsupportedModelLoad
    {
        std::string Guid;
        DirectX::XMFLOAT3 Position{};
        DirectX::XMFLOAT3 Scale{1.0f, 1.0f, 1.0f};
        std::string Name;
        std::shared_ptr<SceneCreationResult> Result;
    };
    std::optional<PendingUnsupportedModelLoad> m_PendingUnsupportedModelLoad;
    std::string m_UnsupportedModelWarnings;
    bool m_SceneDirty = false;
    bool m_ShowUnsavedModal = false;
    SceneFileActionRequestedEvent::Action m_PendingSceneAction =
        SceneFileActionRequestedEvent::Action::NewScene;
};
