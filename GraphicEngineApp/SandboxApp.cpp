#include "SandboxApp.h"
#include "SandboxApp.h"
#include "UIWin.h"

#include "renderer/Light.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <string>

#if defined(_DEBUG)
namespace {
	DirectX::XMFLOAT4 QuaternionFromTo(const DirectX::XMFLOAT3& from, const DirectX::XMFLOAT3& to) {
		using namespace DirectX;

		XMVECTOR fromV = XMVector3Normalize(XMLoadFloat3(&from));
		XMVECTOR toV = XMVector3Normalize(XMLoadFloat3(&to));
		float dot = XMVectorGetX(XMVector3Dot(fromV, toV));

		dot = std::clamp(dot, -1.0f, 1.0f);
		if(dot > 0.9999f) {
			return DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		}

		if(dot < -0.9999f) {
			XMVECTOR axis = XMVector3Cross(fromV, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
			if(XMVectorGetX(XMVector3LengthSq(axis)) < 1e-6f) {
				axis = XMVector3Cross(fromV, XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f));
			}

			axis = XMVector3Normalize(axis);
			XMVECTOR q = XMQuaternionRotationAxis(axis, DirectX::XM_PI);
			DirectX::XMFLOAT4 result;
			XMStoreFloat4(&result, q);
			return result;
		}

		XMVECTOR axis = XMVector3Normalize(XMVector3Cross(fromV, toV));
		XMVECTOR q = XMQuaternionRotationAxis(axis, std::acos(dot));
		XMFLOAT4 result;
		XMStoreFloat4(&result, XMQuaternionNormalize(q));
		return result;
	}

	void ApplySpotlightGizmoTransform(Renderer::Entity* gizmo, const Renderer::SpotLight& light) {
		if(!gizmo) {
			return;
		}

		const DirectX::XMFLOAT3 position = light.GetPosition();
		DirectX::XMFLOAT3 direction = light.GetDirection();
		const float len = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
		if(len > 0.0001f) {
			direction.x /= len;
			direction.y /= len;
			direction.z /= len;
		} else {
			direction = DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f);
		}

		const float outerAngleRad = light.GetOuterAngleDegrees() * 0.017453292519943295769f;
		const float range = std::max(light.GetRange(), 0.01f);
		const float radius = std::tan(outerAngleRad) * range;

		gizmo->transform.SetPosition(position.x, position.y, position.z);
		gizmo->transform.SetScale(radius, radius, range);
		gizmo->transform.Rotation = QuaternionFromTo(DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f), direction);
	}
}
#endif


void SandboxApp::OnInit() {
	ImGui::SetCurrentContext(GetImGuiContext());

	auto* device = GetRenderer()->GetDevice();
	auto* factory = GetShaderFactory();

   m_DefaultVS = factory->GetShader("shaders/color_vs.dxbc", nvrhi::ShaderType::Vertex);
	m_DefaultPS = factory->GetShader("shaders/color_ps.dxbc", nvrhi::ShaderType::Pixel);

	auto* fb = GetRenderer()->GetDefaultFramebuffer();
	float width = (float)fb->getFramebufferInfo().width;
	float height = (float)fb->getFramebufferInfo().height;

	float aspect = width / (height > 0.0f ? height : 1.0f);

    auto main_camera = std::make_shared<Renderer::Camera>(m_CameraFov, aspect, m_CameraNearPlane, m_CameraFarPlane);
	main_camera->m_Transform.SetPosition(0, 0, -5);

   cameras.clear();
	cameras.push_back(main_camera);
	SetActiveCamera(main_camera);

    m_Cubes.clear();
	m_ObjectEulerDegrees.clear();
   m_Lights.clear();

   const DirectX::XMFLOAT3 objectPositions[2] = {
		DirectX::XMFLOAT3(-3.2f, 0.6f, 2.0f),
		DirectX::XMFLOAT3(3.2f, 0.3f, 2.3f)
	};

	const DirectX::XMFLOAT3 lightPositions[2] = {
		DirectX::XMFLOAT3(-3.2f, 1.8f, -0.4f),
		DirectX::XMFLOAT3(3.2f, 4.8f, -5.2f)
	};

	const float lightRanges[2] = { 8.0f, 20.0f };
	const DirectX::XMFLOAT2 coneAngles[2] = {
		DirectX::XMFLOAT2(20.0f, 32.0f),
		DirectX::XMFLOAT2(30.0f, 46.0f)
	};

	for(int i = 0; i < 2; ++i) {
		const DirectX::XMFLOAT3 lightDirection(
			objectPositions[i].x - lightPositions[i].x,
			objectPositions[i].y - lightPositions[i].y,
			objectPositions[i].z - lightPositions[i].z
		);

		Renderer::SpotLight light(lightDirection, DirectX::XMFLOAT3(1.0f, 0.96f, 0.9f), 8.5f);
		light.SetUsePosition(true);
		light.SetPosition(lightPositions[i]);
		light.SetDirection(lightDirection);
		light.SetRange(lightRanges[i]);
		light.SetConeAngles(coneAngles[i].x, coneAngles[i].y);
		light.SetCastShadows(true);
		light.SetShadowBias(0.0015f);
		light.SetShadowNormalBias(0.0025f);
		m_Lights.push_back(light);
	}

	GetSceneRenderer()->ClearLights();
	for(const auto& light : m_Lights) {
		GetSceneRenderer()->AddLight(light);
	}

	auto* cmd = GetCommandList();
	
	cmd->open();

  auto casterObject = Renderer::EntityBuilder(device, cmd)
		.Cube()
		.Material(m_DefaultVS, m_DefaultPS)
        .Position(objectPositions[0].x, objectPositions[0].y, objectPositions[0].z)
		.Scale(0.9f, 0.9f, 0.9f)
		.Color(0.85f, 0.35f, 0.25f, 1.0f)
		.Emissive(0.0f, 0.0f, 0.0f)
		.Metallic(0.2f)
		.Roughness(0.55f)
		.Build();

	casterObject->CastShadows = true;
	casterObject->ReceiveShadows = false;
	m_Cubes.push_back(std::move(casterObject));

  auto receiverObject = Renderer::EntityBuilder(device, cmd)
		.Sphere()
		.Material(m_DefaultVS, m_DefaultPS)
     .Position(objectPositions[1].x, objectPositions[1].y, objectPositions[1].z)
		.Scale(1.2f, 1.2f, 1.2f)
		.Color(0.75f, 0.78f, 0.84f, 1.0f)
		.Emissive(0.0f, 0.0f, 0.0f)
		.Metallic(0.5f)
		.Roughness(0.35f)
		.Build();

	receiverObject->CastShadows = false;
	receiverObject->ReceiveShadows = true;
	m_Cubes.push_back(std::move(receiverObject));

	cmd->close();
	device->executeCommandList(cmd);
	m_ObjectEulerDegrees.assign(m_Cubes.size(), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));

#if defined(_DEBUG)
 cmd->open();
	m_DebugLightGizmos.clear();
	m_DebugLightGizmos.reserve(m_Lights.size());
	for(size_t i = 0; i < m_Lights.size(); ++i) {
		auto gizmo = Renderer::EntityBuilder(device, cmd)
			.Cone(20)
           .Material(m_DefaultVS, m_DefaultPS)
			.Color(0.0f, 0.0f, 0.0f, 1.0f)
			.Emissive(0.95f, 0.75f, 0.25f)
			.Metallic(0.0f)
			.Roughness(1.0f)
			.Build();

		gizmo->ForceWireframe = true;
		gizmo->DisableDepthTest = true;
      gizmo->CastShadows = false;
		gizmo->ReceiveShadows = false;
		gizmo->CullMode = nvrhi::RasterCullMode::None;
		ApplySpotlightGizmoTransform(gizmo.get(), m_Lights[i]);

		m_DebugLightGizmos.push_back(std::move(gizmo));
	}

	m_DebugWireframePlane = Renderer::EntityBuilder(device, cmd)
		.Plane()
       .Material(m_DefaultVS, m_DefaultPS)
		.Position(0.0f, -2.0f, 0.0f)
		.Scale(20.0f, 1.0f, 20.0f)
		.Color(0.0f, 0.0f, 0.0f, 1.0f)
		.Emissive(0.25f, 0.6f, 0.95f)
		.Metallic(0.0f)
		.Roughness(1.0f)
		.Build();

	m_DebugWireframePlane->ForceWireframe = true;
	m_DebugWireframePlane->DisableDepthTest = false;
	m_DebugWireframePlane->CastShadows = false;
	m_DebugWireframePlane->ReceiveShadows = false;
	m_DebugWireframePlane->CullMode = nvrhi::RasterCullMode::None;
  cmd->close();
	device->executeCommandList(cmd);
#endif
}

void SandboxApp::OnUpdate(float deltaTime) {
   auto keyDown = [this](Engine::Events::KeyCode key) {
		return m_KeyDown[static_cast<size_t>(key)];
	};

	auto mouseDown = [this](Engine::Events::MouseButton button) {
		return m_MouseButtonDown[static_cast<size_t>(button)];
	};

	m_Timer += deltaTime;

	auto camera = GetActiveCamera();
	if(camera) {
		ImGuiIO& io = ImGui::GetIO();

     const bool rightMouseDown = mouseDown(Engine::Events::MouseButton::Right);
		if(GetWindow() && rightMouseDown != m_CursorHidden) {
			GetWindow()->SetCursorVisible(!rightMouseDown);
			m_CursorHidden = rightMouseDown;
		}

		if(!io.WantCaptureKeyboard) {
			const float moveSpeed = 8.0f;
			const float step = moveSpeed * deltaTime;

			float moveForward = 0.0f;
			float moveRight = 0.0f;
			float moveUp = 0.0f;

          if(keyDown(Engine::Events::KeyCode::W)) moveForward += 1.0f;
			if(keyDown(Engine::Events::KeyCode::S)) moveForward -= 1.0f;
			if(keyDown(Engine::Events::KeyCode::D)) moveRight += 1.0f;
			if(keyDown(Engine::Events::KeyCode::A)) moveRight -= 1.0f;
            if(keyDown(Engine::Events::KeyCode::E)) moveUp += 1.0f;
			if(keyDown(Engine::Events::KeyCode::Q)) moveUp -= 1.0f;

          const float sinYaw = std::sin(m_Yaw);
			const float cosYaw = std::cos(m_Yaw);

			auto& position = camera->m_Transform.Position;
			position.x += (sinYaw * moveForward + cosYaw * moveRight) * step;
			position.z += (cosYaw * moveForward - sinYaw * moveRight) * step;
			position.y += moveUp * step;
		}

		if(rightMouseDown && !io.WantCaptureMouse) {
			const float sensitivity = 0.0035f;
			m_Yaw += m_MouseDeltaX * sensitivity;
         m_Pitch += m_MouseDeltaY * sensitivity;
			m_Pitch = std::clamp(m_Pitch, -1.5f, 1.5f);

			camera->m_Transform.SetRotation(m_Pitch, m_Yaw, 0.0f);
		}
	}

	m_MouseDeltaX = 0.0f;
	m_MouseDeltaY = 0.0f;

   GetSceneRenderer()->ClearLights();
	for(const auto& light : m_Lights) {
		GetSceneRenderer()->AddLight(light);
	}

#if defined(_DEBUG)
	const size_t gizmoCount = std::min(m_DebugLightGizmos.size(), m_Lights.size());
	for(size_t i = 0; i < gizmoCount; ++i) {
		ApplySpotlightGizmoTransform(m_DebugLightGizmos[i].get(), m_Lights[i]);
	}
#endif
}

void SandboxApp::OnEvent(Engine::Events::Event& event) {
	Engine::Events::EventDispatcher dispatcher(event);

	dispatcher.Dispatch<Engine::Events::KeyPressedEvent>([this](const auto& keyEvent) {
        m_KeyDown[static_cast<size_t>(keyEvent.Key)] = true;

		return false;
	});

	dispatcher.Dispatch<Engine::Events::KeyReleasedEvent>([this](const auto& keyEvent) {
        m_KeyDown[static_cast<size_t>(keyEvent.Key)] = false;

		return false;
	});

	dispatcher.Dispatch<Engine::Events::MouseButtonPressedEvent>([this](const auto& mouseButtonEvent) {
      m_MouseButtonDown[static_cast<size_t>(mouseButtonEvent.Button)] = true;

		return false;
	});

	dispatcher.Dispatch<Engine::Events::MouseButtonReleasedEvent>([this](const auto& mouseButtonEvent) {
      m_MouseButtonDown[static_cast<size_t>(mouseButtonEvent.Button)] = false;

		return false;
	});

	dispatcher.Dispatch<Engine::Events::MouseMovedEvent>([this](const auto& mouseMovedEvent) {
		m_MouseDeltaX += static_cast<float>(mouseMovedEvent.DeltaX);
		m_MouseDeltaY += static_cast<float>(mouseMovedEvent.DeltaY);
		return false;
	});

	dispatcher.Dispatch<Engine::Events::MouseScrolledEvent>([this](const auto& mouseScrolledEvent) {
		if(ImGui::GetIO().WantCaptureMouse) {
			return false;
		}

		auto camera = GetActiveCamera();
		if(!camera) {
			return false;
		}

		constexpr float zoomSensitivity = 0.05f;
		const float minFov = DirectX::XMConvertToRadians(20.0f);
		const float maxFov = DirectX::XMConvertToRadians(90.0f);

		m_CameraFov -= static_cast<float>(mouseScrolledEvent.YOffset) * zoomSensitivity;
		m_CameraFov = std::clamp(m_CameraFov, minFov, maxFov);

		const uint32_t width = GetWindow() ? GetWindow()->GetWidth() : 1;
		const uint32_t height = GetWindow() ? GetWindow()->GetHeight() : 1;
		const float aspect = static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);

		camera->m_Projection = DirectX::XMMatrixPerspectiveFovLH(m_CameraFov, aspect, m_CameraNearPlane, m_CameraFarPlane);
		return true;
	});
}

void SandboxApp::OnRender() {
	for(size_t i = 0; i < m_Cubes.size(); i++) {
		GetSceneRenderer()->Submit(m_Cubes[i].get());
	}

#if defined(_DEBUG)
	if(m_ShowDebugLightGizmos) {
		for(size_t i = 0; i < m_DebugLightGizmos.size(); ++i) {
			GetSceneRenderer()->Submit(m_DebugLightGizmos[i].get());
		}
	}

	if(m_ShowDebugWireframePlane && m_DebugWireframePlane) {
		GetSceneRenderer()->Submit(m_DebugWireframePlane.get());
	}
#endif
}

void SandboxApp::OnUIRender() {
	statWindow();

 if(m_SelectedObjectIndex >= static_cast<int>(m_Cubes.size())) {
		m_SelectedObjectIndex = -1;
		if(m_SelectedItemType == SelectedItemType::Object) {
			m_SelectedItemType = SelectedItemType::None;
		}
	}

	if(m_ObjectEulerDegrees.size() < m_Cubes.size()) {
		m_ObjectEulerDegrees.resize(m_Cubes.size(), DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
	}

	if(m_SelectedLightIndex >= static_cast<int>(m_Lights.size())) {
		m_SelectedLightIndex = -1;
		if(m_SelectedItemType == SelectedItemType::Light) {
			m_SelectedItemType = SelectedItemType::None;
		}
	}

	if(m_SelectedCameraIndex >= static_cast<int>(cameras.size())) {
		m_SelectedCameraIndex = -1;
		if(m_SelectedItemType == SelectedItemType::Camera) {
			m_SelectedItemType = SelectedItemType::None;
		}
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	const ImVec2 padding(12.0f, 12.0f);
    const float panelRightX = viewport->WorkPos.x + viewport->WorkSize.x - padding.x;
	const float panelTopY = viewport->WorkPos.y + padding.y;
	const float panelBottomY = viewport->WorkPos.y + viewport->WorkSize.y - padding.y;
    const float maxPanelWidth = std::max(260.0f, viewport->WorkSize.x - (padding.x * 2.0f));
	m_SidePanelWidth = std::clamp(m_SidePanelWidth, 260.0f, maxPanelWidth);

	const float resizeHandleHalfWidth = 5.0f;
	const float panelLeftX = panelRightX - m_SidePanelWidth;
	ImGuiIO& io = ImGui::GetIO();
	const bool isHoveringResizeHandle =
		io.MousePos.x >= (panelLeftX - resizeHandleHalfWidth) &&
		io.MousePos.x <= (panelLeftX + resizeHandleHalfWidth) &&
		io.MousePos.y >= panelTopY &&
		io.MousePos.y <= panelBottomY;

	if(isHoveringResizeHandle || m_IsResizingSidePanel) {
		ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
	}

	if(isHoveringResizeHandle && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
		m_IsResizingSidePanel = true;
	}

	if(m_IsResizingSidePanel) {
		if(ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
			m_SidePanelWidth = std::clamp(panelRightX - io.MousePos.x, 260.0f, maxPanelWidth);
		} else {
			m_IsResizingSidePanel = false;
		}
	}

	const float panelWidth = m_SidePanelWidth;
	const float panelHeight = viewport->WorkSize.y - (padding.y * 2.0f);

	ImGui::SetNextWindowPos(
        ImVec2(panelRightX, panelTopY),
		ImGuiCond_Always,
      ImVec2(1.0f, 0.0f)
	);
  ImGui::SetNextWindowSize(ImVec2(panelWidth, panelHeight), ImGuiCond_Always);
	ImGui::Begin("Panel Sceny", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
	ImGui::Separator();

	ImGui::TextUnformatted("Scena");
	const float sceneListHeight = ImGui::GetContentRegionAvail().y * 0.35f;
	ImGui::BeginChild("##SceneList", ImVec2(0.0f, sceneListHeight), true);

	if(ImGui::CollapsingHeader("Obiekty", ImGuiTreeNodeFlags_DefaultOpen)) {
		for(size_t i = 0; i < m_Cubes.size(); ++i) {
			const std::string label = "Obiekt " + std::to_string(i);
			const bool selected = m_SelectedItemType == SelectedItemType::Object && m_SelectedObjectIndex == static_cast<int>(i);
			if(ImGui::Selectable(label.c_str(), selected)) {
				m_SelectedItemType = SelectedItemType::Object;
				m_SelectedObjectIndex = static_cast<int>(i);
				m_SelectedLightIndex = -1;
               m_SelectedCameraIndex = -1;
			}
		}

		if(m_Cubes.empty()) {
			ImGui::TextDisabled("Brak aktywnych obiektow");
		}
	}

	if(ImGui::CollapsingHeader("Swiatla", ImGuiTreeNodeFlags_DefaultOpen)) {
		for(size_t i = 0; i < m_Lights.size(); ++i) {
			const std::string label = "Swiatlo " + std::to_string(i);
			const bool selected = m_SelectedItemType == SelectedItemType::Light && m_SelectedLightIndex == static_cast<int>(i);
			if(ImGui::Selectable(label.c_str(), selected)) {
				m_SelectedItemType = SelectedItemType::Light;
				m_SelectedLightIndex = static_cast<int>(i);
				m_SelectedObjectIndex = -1;
               m_SelectedCameraIndex = -1;
			}
		}

		if(m_Lights.empty()) {
			ImGui::TextDisabled("Brak swiatel");
		}
	}

	if(ImGui::CollapsingHeader("Kamery", ImGuiTreeNodeFlags_DefaultOpen)) {
        for(size_t i = 0; i < cameras.size(); ++i) {
			const bool isActiveCamera = cameras[i] == GetActiveCamera();
			const std::string label = isActiveCamera ? "Kamera " + std::to_string(i) + " (aktywna)" : "Kamera " + std::to_string(i);
			const bool selected = m_SelectedItemType == SelectedItemType::Camera && m_SelectedCameraIndex == static_cast<int>(i);
			if(ImGui::Selectable(label.c_str(), selected)) {
				m_SelectedItemType = SelectedItemType::Camera;
				m_SelectedCameraIndex = static_cast<int>(i);
				m_SelectedObjectIndex = -1;
				m_SelectedLightIndex = -1;
			}
		}

		if(cameras.empty()) {
			ImGui::TextDisabled("Brak kamer");
		}
	}
   ImGui::EndChild();

    ImGui::Spacing();
	ImGui::TextUnformatted("Inspektor");
 const float addButtonReservedHeight = ImGui::GetFrameHeightWithSpacing() + 8.0f;
	const float inspectorHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y - addButtonReservedHeight);
	ImGui::BeginChild("##Inspector", ImVec2(0.0f, inspectorHeight), true);
	if(m_SelectedItemType == SelectedItemType::Object && m_SelectedObjectIndex >= 0 && m_SelectedObjectIndex < static_cast<int>(m_Cubes.size())) {
		auto* selected = m_Cubes[static_cast<size_t>(m_SelectedObjectIndex)].get();

       ImGui::Text("Obiekt %d", m_SelectedObjectIndex);
		ImGui::Separator();

		float position[3] = {
			selected->transform.Position.x,
			selected->transform.Position.y,
			selected->transform.Position.z
		};
		if(ImGui::DragFloat3("Pozycja", position, 0.05f)) {
			selected->transform.SetPosition(position[0], position[1], position[2]);
		}

		float scale[3] = {
			selected->transform.Scale.x,
			selected->transform.Scale.y,
			selected->transform.Scale.z
		};
		if(ImGui::DragFloat3("Skala", scale, 0.05f, 0.01f, 100.0f)) {
			selected->transform.SetScale(scale[0], scale[1], scale[2]);
		}

       DirectX::XMFLOAT3& guiEulerDegrees = m_ObjectEulerDegrees[static_cast<size_t>(m_SelectedObjectIndex)];
		float rotationDegrees[3] = {
			guiEulerDegrees.x,
			guiEulerDegrees.y,
			guiEulerDegrees.z
		};
		if(ImGui::DragFloat3("Rotacja (Roll/Pitch/Yaw) [deg]", rotationDegrees, 0.25f, -360.0f, 360.0f)) {
          guiEulerDegrees = { rotationDegrees[0], rotationDegrees[1], rotationDegrees[2] };
			const float rollRad = DirectX::XMConvertToRadians(rotationDegrees[0]);
			const float pitchRad = DirectX::XMConvertToRadians(rotationDegrees[1]);
			const float yawRad = DirectX::XMConvertToRadians(rotationDegrees[2]);
			selected->transform.SetRotation(pitchRad, yawRad, rollRad);
		}

		ImGui::Checkbox("Wireframe", &selected->ForceWireframe);
		ImGui::Checkbox("Bez testu glebokosci", &selected->DisableDepthTest);
		ImGui::Checkbox("Rzuca cienie", &selected->CastShadows);
		ImGui::Checkbox("Odbiera cienie", &selected->ReceiveShadows);

		if(selected->material) {
			ImGui::Separator();
			ImGui::TextUnformatted("Material");
			Renderer::MaterialProperties properties = selected->material->GetProperties();

			float baseColor[4] = {
				properties.BaseColor.x,
				properties.BaseColor.y,
				properties.BaseColor.z,
				properties.BaseColor.w
			};
			float emissive[3] = {
				properties.Emissive.x,
				properties.Emissive.y,
				properties.Emissive.z
			};
			float metallic = properties.Metallic;
			float roughness = properties.Roughness;

			bool materialChanged = false;
			materialChanged |= ImGui::ColorEdit4("Kolor bazowy", baseColor);
			materialChanged |= ImGui::ColorEdit3("Emisja", emissive);
			materialChanged |= ImGui::SliderFloat("Metalicznosc", &metallic, 0.0f, 1.0f);
			materialChanged |= ImGui::SliderFloat("Chropowatosc", &roughness, 0.0f, 1.0f);

			if(materialChanged) {
				properties.BaseColor = { baseColor[0], baseColor[1], baseColor[2], baseColor[3] };
				properties.Emissive = { emissive[0], emissive[1], emissive[2] };
				properties.Metallic = metallic;
				properties.Roughness = roughness;

				auto* cmd = GetCommandList();
				auto* device = GetRenderer()->GetDevice();
				cmd->open();
				selected->material->SetProperties(properties, cmd);
				cmd->close();
				device->executeCommandList(cmd);
			}
		}
	} else if(m_SelectedItemType == SelectedItemType::Light && m_SelectedLightIndex >= 0 && m_SelectedLightIndex < static_cast<int>(m_Lights.size())) {
		auto& light = m_Lights[static_cast<size_t>(m_SelectedLightIndex)];

		ImGui::Text("Swiatlo %d", m_SelectedLightIndex);
		ImGui::Separator();

		DirectX::XMFLOAT3 pos = light.GetPosition();
		float position[3] = { pos.x, pos.y, pos.z };
		if(ImGui::DragFloat3("Pozycja", position, 0.05f)) {
          light.SetPosition(DirectX::XMFLOAT3(position[0], position[1], position[2]));
		}

     DirectX::XMFLOAT3 dir = light.GetDirection();
		float direction[3] = { dir.x, dir.y, dir.z };
      if(ImGui::DragFloat3("Kierunek", direction, 0.05f)) {
			light.SetDirection(DirectX::XMFLOAT3(direction[0], direction[1], direction[2]));
		}

       float inner_ang = light.GetInnerAngleDegrees();
		float outer_ang = light.GetOuterAngleDegrees();
		float cone_angles[2] = { inner_ang, outer_ang };
        if(ImGui::DragFloat2("Katy", cone_angles, 0.05f)) {
			light.SetConeAngles(cone_angles[0], cone_angles[1]);
		}

       DirectX::XMFLOAT3 color = light.GetColor();
		float lightColor[3] = { color.x, color.y, color.z };
       if(ImGui::ColorEdit3("Kolor", lightColor)) {
			light.SetColor(DirectX::XMFLOAT3(lightColor[0], lightColor[1], lightColor[2]));
		}

		float intensity = light.GetIntensity();
		if(ImGui::DragFloat("Intensywnosc", &intensity, 0.05f, 0.0f, 100.0f)) {
			light.SetIntensity(intensity);
		}

		float range = light.GetRange();
		if(ImGui::DragFloat("Zasieg", &range, 0.05f, 0.1f, 100.0f)) {
			light.SetRange(range);
		}

        bool castShadows = light.GetCastShadows();
		if(ImGui::Checkbox("Rzuca cienie", &castShadows)) {
			light.SetCastShadows(castShadows);
		}
    } else if(m_SelectedItemType == SelectedItemType::Camera && m_SelectedCameraIndex >= 0 && m_SelectedCameraIndex < static_cast<int>(cameras.size())) {
		auto selectedCamera = cameras[static_cast<size_t>(m_SelectedCameraIndex)];

		ImGui::Text("Kamera %d", m_SelectedCameraIndex);
		ImGui::Separator();

		const bool isActiveCamera = selectedCamera == GetActiveCamera();
		if(isActiveCamera) {
			ImGui::TextDisabled("Aktywna kamera");
		} else if(ImGui::Button("Ustaw jako aktywna")) {
			SetActiveCamera(selectedCamera);
		}

		if(selectedCamera) {
			float position[3] = {
				selectedCamera->m_Transform.Position.x,
				selectedCamera->m_Transform.Position.y,
				selectedCamera->m_Transform.Position.z
			};

			if(ImGui::DragFloat3("Pozycja", position, 0.05f)) {
				selectedCamera->m_Transform.SetPosition(position[0], position[1], position[2]);
			}

            float fovDeg = DirectX::XMConvertToDegrees(m_CameraFov);
			const bool fovChanged = ImGui::SliderFloat("FOV", &fovDeg, 20.0f, 90.0f, "%.1f deg");
			if(fovChanged) {
				m_CameraFov = DirectX::XMConvertToRadians(fovDeg);
			}

         bool projectionChanged = fovChanged;
			projectionChanged |= ImGui::DragFloat("Near", &m_CameraNearPlane, 0.01f, 0.01f, m_CameraFarPlane - 0.01f);
			projectionChanged |= ImGui::DragFloat("Far", &m_CameraFarPlane, 0.1f, m_CameraNearPlane + 0.01f, 1000.0f);

            if(projectionChanged) {
				m_CameraNearPlane = std::max(0.01f, m_CameraNearPlane);
				m_CameraFarPlane = std::max(m_CameraNearPlane + 0.01f, m_CameraFarPlane);

				const uint32_t width = GetWindow() ? GetWindow()->GetWidth() : 1;
				const uint32_t height = GetWindow() ? GetWindow()->GetHeight() : 1;
				const float aspect = static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
				selectedCamera->m_Projection = DirectX::XMMatrixPerspectiveFovLH(m_CameraFov, aspect, m_CameraNearPlane, m_CameraFarPlane);
			}
		}
	} else {
       ImGui::TextDisabled("Wybierz element z listy sceny.");
	}

#if defined(_DEBUG)
	ImGui::Spacing();
	if(ImGui::CollapsingHeader("Debug Render")) {
		ImGui::Checkbox("Pokaz wireframe spotlight", &m_ShowDebugLightGizmos);
		ImGui::Checkbox("Pokaz wireframe plane", &m_ShowDebugWireframePlane);
	}
#endif
	ImGui::EndChild();

	if(ImGui::Button("Add", ImVec2(-1.0f, 0.0f))) {
		ImGui::OpenPopup("Dodaj obiekt do sceny");
	}

	if(ImGui::BeginPopupModal("Dodaj obiekt do sceny", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
		static const char* shapeItems[] = { "Cube", "Sphere", "Plane", "Cone" };
		int selectedShape = static_cast<int>(m_SpawnShape);
		if(ImGui::Combo("Typ", &selectedShape, shapeItems, IM_ARRAYSIZE(shapeItems))) {
			m_SpawnShape = static_cast<SpawnShapeType>(selectedShape);
		}

		float spawnPosition[3] = { m_SpawnPosition.x, m_SpawnPosition.y, m_SpawnPosition.z };
		if(ImGui::DragFloat3("Pozycja startowa", spawnPosition, 0.05f)) {
			m_SpawnPosition = { spawnPosition[0], spawnPosition[1], spawnPosition[2] };
		}

		float spawnScale[3] = { m_SpawnScale.x, m_SpawnScale.y, m_SpawnScale.z };
		if(ImGui::DragFloat3("Skala startowa", spawnScale, 0.05f, 0.05f, 100.0f)) {
			m_SpawnScale = { spawnScale[0], spawnScale[1], spawnScale[2] };
		}

		float spawnColor[4] = { m_SpawnColor.x, m_SpawnColor.y, m_SpawnColor.z, m_SpawnColor.w };
		if(ImGui::ColorEdit4("Kolor startowy", spawnColor)) {
			m_SpawnColor = { spawnColor[0], spawnColor[1], spawnColor[2], spawnColor[3] };
		}

		float spawnEmissive[3] = { m_SpawnEmissive.x, m_SpawnEmissive.y, m_SpawnEmissive.z };
		if(ImGui::ColorEdit3("Emisja startowa", spawnEmissive)) {
			m_SpawnEmissive = { spawnEmissive[0], spawnEmissive[1], spawnEmissive[2] };
		}

		ImGui::SliderFloat("Metalicznosc startowa", &m_SpawnMetallic, 0.0f, 1.0f);
		ImGui::SliderFloat("Chropowatosc startowa", &m_SpawnRoughness, 0.0f, 1.0f);

		if(ImGui::Button("Dodaj", ImVec2(140.0f, 0.0f))) {
			auto* device = GetRenderer()->GetDevice();
			auto* cmd = GetCommandList();

			DirectX::XMFLOAT3 spawnPosition = m_SpawnPosition;
			auto intersectsExisting = [this](const DirectX::XMFLOAT3& position) {
				for(const auto& entity : m_Cubes) {
					if(!entity) {
						continue;
					}

					const auto& existingPos = entity->transform.Position;
					const float dx = existingPos.x - position.x;
					const float dy = existingPos.y - position.y;
					const float dz = existingPos.z - position.z;
					const float distanceSq = dx * dx + dy * dy + dz * dz;
					if(distanceSq < 4.0f) {
						return true;
					}
				}

				return false;
			};

			for(int i = 0; i < 32 && intersectsExisting(spawnPosition); ++i) {
				spawnPosition.x += 2.5f;
			}

			cmd->open();
			Renderer::EntityBuilder builder(device, cmd);

			switch(m_SpawnShape) {
				case SpawnShapeType::Cube:
					builder.Cube();
					break;
				case SpawnShapeType::Sphere:
					builder.Sphere();
					break;
				case SpawnShapeType::Plane:
					builder.Plane();
					break;
				case SpawnShapeType::Cone:
					builder.Cone(20);
					break;
			}

			auto createdEntity = builder
				.Material(m_DefaultVS, m_DefaultPS)
              .Position(spawnPosition.x, spawnPosition.y, spawnPosition.z)
				.Scale(m_SpawnScale.x, m_SpawnScale.y, m_SpawnScale.z)
				.Color(m_SpawnColor.x, m_SpawnColor.y, m_SpawnColor.z, m_SpawnColor.w)
				.Emissive(m_SpawnEmissive.x, m_SpawnEmissive.y, m_SpawnEmissive.z)
				.Metallic(m_SpawnMetallic)
				.Roughness(m_SpawnRoughness)
				.Build();

			cmd->close();
			device->executeCommandList(cmd);

			m_Cubes.push_back(std::move(createdEntity));
          m_ObjectEulerDegrees.push_back(DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
			m_SelectedItemType = SelectedItemType::Object;
			m_SelectedObjectIndex = static_cast<int>(m_Cubes.size() - 1);
			m_SelectedLightIndex = -1;
			m_SpawnPosition = spawnPosition;
			m_SpawnPosition.x += 2.5f;

			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if(ImGui::Button("Anuluj", ImVec2(140.0f, 0.0f))) {
			ImGui::CloseCurrentPopup();
		}

		ImGui::EndPopup();
	}

	ImGui::End();

}

void SandboxApp::OnResize(uint32_t width, uint32_t height) {
	auto camera = GetActiveCamera();
	if(!camera || width == 0 || height == 0) {
		return;
	}

	const float aspect = static_cast<float>(width) / static_cast<float>(height);
 camera->m_Projection = DirectX::XMMatrixPerspectiveFovLH(m_CameraFov, aspect, m_CameraNearPlane, m_CameraFarPlane);
}
