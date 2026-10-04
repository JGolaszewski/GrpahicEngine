#pragma once

#include "Application.h"
#include "panels/EditorPanel.h"
#include "EditorCameraController.h"
#include <imgui.h>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <utility>
#include "commands/CommandHistory.h"

namespace Engine::Events {
	class Event;
}

namespace Renderer {
	class Camera;
}

namespace Engine {
	class Scene;
	class Viewport;
	class ViewportTarget;
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

	void OnSetupViews() override;

private:
	enum class SelectedItemType {
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
	void BroadcastEvent(Engine::Events::Event& event);

	void BroadcastSceneChanged();
	void BroadcastCameraSettings();
	void BroadcastViewportChanged();

	// ------------------------------------------------------------------------
	// Undo / Redo
	// ------------------------------------------------------------------------
	void Undo();
	void Redo();
	CommandHistory& GetCommandHistory() noexcept { return m_CommandHistory; }

	// ------------------------------------------------------------------------
	// Viewport Management
	// ------------------------------------------------------------------------
	void CreateViewport(uint32_t width, uint32_t height);
	void DestroyViewport();
	void* GetViewportTextureID() const;
	void UpdateProjection();
	void UpdateProjection(uint32_t width, uint32_t height);
	std::shared_ptr<Renderer::Camera> GetActiveCamera() const;

	// ------------------------------------------------------------------------
	// State
	// ------------------------------------------------------------------------
	std::shared_ptr<Engine::Scene>    m_ActiveScene;
	std::shared_ptr<Engine::Viewport> m_EditorViewport;
	std::shared_ptr<Engine::ViewportTarget> m_EditorViewportTarget;
	Engine::CameraId m_ActiveCameraId = Engine::InvalidCameraId;

	std::vector<std::unique_ptr<EditorPanel>> m_Windows;

	std::pair<float, float> m_ViewportSize = { 0.0f, 0.0f };

	std::array<bool, static_cast<size_t>(Engine::Events::KeyCode::Count)>     m_KeyDown{};
	std::array<bool, static_cast<size_t>(Engine::Events::MouseButton::Count)> m_MouseButtonDown{};

	float m_CameraFov = 0.785398f; // XM_PIDIV4
	float m_CameraNearPlane = 0.1f;
	float m_CameraFarPlane = 100.0f;
	bool  m_CursorHidden = false;

	SelectedItemType m_SelectedItemType = SelectedItemType::None;
	Engine::EntityId m_SelectedEntityId = Engine::InvalidEntityId;
	Engine::LightId m_SelectedLightId = Engine::InvalidLightId;
	Engine::CameraId m_SelectedCameraId = Engine::InvalidCameraId;

	bool                      m_ViewportHovered = false;
	bool                      m_DefaultLayoutBuilt = false;
	bool                      m_ShowDebugLightGizmos = true;
	bool                      m_ShowDebugWireframePlane = true;

	Engine::EditorCameraController m_CameraController;

	CommandHistory m_CommandHistory;
};
