#pragma once
#pragma once

#include "Application.h"
#include "renderer/Mesh.h"
#include "renderer/Material.h"
#include "renderer/Entity.h"
#include "renderer/Camera.h"
#include "renderer/Light.h"

#include <array>
#include <memory>
#include <vector>

class SandboxApp : public Engine::Application {
public:
	SandboxApp() = default;
	~SandboxApp() override = default;

protected:
	void OnInit() override;
	void OnRender() override;
	void OnUpdate(float deltaTime) override;
	void OnUIRender() override;
	void OnResize(uint32_t width, uint32_t height) override;
	void OnEvent(Engine::Events::Event& event) override;

private:
	std::vector<std::unique_ptr<Renderer::Entity>> m_Cubes;
    std::vector<DirectX::XMFLOAT3> m_ObjectEulerDegrees;
	std::vector<DirectX::XMFLOAT3> m_RotationSpeeds;
	float m_Timer = 0.0f;
 std::vector<Renderer::SpotLight> m_Lights;
  std::array<bool, static_cast<size_t>(Engine::Events::KeyCode::Count)> m_KeyDown{};
	std::array<bool, static_cast<size_t>(Engine::Events::MouseButton::Count)> m_MouseButtonDown{};
	float m_MouseDeltaX = 0.0f;
	float m_MouseDeltaY = 0.0f;
	float m_Yaw = 0.0f;
	float m_Pitch = 0.0f;
    float m_CameraFov = DirectX::XM_PIDIV4;
	float m_CameraNearPlane = 0.1f;
	float m_CameraFarPlane = 100.0f;
	bool m_CursorHidden = false;
 enum class SelectedItemType {
		None,
		Object,
       Light,
		Camera
	};
	SelectedItemType m_SelectedItemType = SelectedItemType::None;
	int m_SelectedObjectIndex = -1;
	int m_SelectedLightIndex = -1;
    int m_SelectedCameraIndex = -1;
	float m_SidePanelWidth = 380.0f;
	bool m_IsResizingSidePanel = false;
	nvrhi::ShaderHandle m_DefaultVS;
	nvrhi::ShaderHandle m_DefaultPS;

	enum class SpawnShapeType {
		Cube,
		Sphere,
		Plane,
		Cone
	};
	SpawnShapeType m_SpawnShape = SpawnShapeType::Cube;
   DirectX::XMFLOAT3 m_SpawnPosition = { 0.0f, 0.0f, 8.0f };
	DirectX::XMFLOAT3 m_SpawnScale = { 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT4 m_SpawnColor = { 0.95f, 0.95f, 0.95f, 1.0f };
	DirectX::XMFLOAT3 m_SpawnEmissive = { 0.0f, 0.0f, 0.0f };
	float m_SpawnMetallic = 0.0f;
	float m_SpawnRoughness = 0.8f;

#if defined(_DEBUG)
	bool m_ShowDebugLightGizmos = true;
  bool m_ShowDebugWireframePlane = true;
	std::vector<std::unique_ptr<Renderer::Entity>> m_DebugLightGizmos;
  std::unique_ptr<Renderer::Entity> m_DebugWireframePlane;
#endif

	std::vector<std::shared_ptr<Renderer::Camera>> cameras;
};