#include "InspectorPanel.h"
#include "renderer/Entity.h"
#include "renderer/Light.h"
#include "renderer/Camera.h"
#include "renderer/Material.h"
#include "commands/TransformEntityCommand.h"

#include <imgui.h>

#include <cmath>

// ================================================================================
// InspectorPanel::OnEvent
// ================================================================================
// Receives EntitySelectedEvent / EntityDeselectedEvent to update the inspected
// item. Also intercepts input events (e.g. Delete key) to clear selection.
// ================================================================================

void InspectorPanel::OnEvent(Engine::Events::Event& event) {
    Engine::Events::EventDispatcher dispatcher(event);

    // Input event: Delete key clears the current selection.
    dispatcher.Dispatch<Engine::Events::KeyPressedEvent>([this](const auto& keyEvent) {
        if (keyEvent.Key == Engine::Events::KeyCode::Delete &&
            m_SelectedItemType != EntitySelectedEvent::ItemType::None) {
            EmitEvent(std::make_unique<EntityDeselectedEvent>());
            return true; // Consumed — stop propagation
        }
        return false;
    });

    // State event: new item selected.
    dispatcher.Dispatch<EntitySelectedEvent>([this](const auto& e) {
        m_SelectedItemType = e.GetItemType();
        m_SelectedObjectIndex = e.GetObjectIndex();
        m_SelectedLightIndex = e.GetLightIndex();
        m_SelectedCameraIndex = e.GetCameraIndex();

        m_SelectedEntity = e.GetEntity();
        m_SelectedLight = e.GetLight();
        m_SelectedCamera = e.GetCamera();

        m_SelectedEulerDegrees = { 0.0f, 0.0f, 0.0f };
        return false;
    });

    // State event: selection cleared.
    dispatcher.Dispatch<EntityDeselectedEvent>([this](const auto&) {
        m_SelectedItemType = EntitySelectedEvent::ItemType::None;
        m_SelectedObjectIndex = -1;
        m_SelectedLightIndex = -1;
        m_SelectedCameraIndex = -1;
        m_SelectedEntity = nullptr;
        m_SelectedLight = nullptr;
        m_SelectedCamera = nullptr;
        return false;
    });

    // State event: camera settings updated (e.g. from Editor init).
    dispatcher.Dispatch<CameraSettingsChangedEvent>([this](const auto& e) {
        m_CameraFov = e.GetFov();
        m_CameraNearPlane = e.GetNearPlane();
        m_CameraFarPlane = e.GetFarPlane();
        return false;
    });
}

void InspectorPanel::Draw() {
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);

    switch (m_SelectedItemType) {
        case EntitySelectedEvent::ItemType::Object:
            if (m_SelectedEntity) {
                DrawObjectInspector();
            }
            break;

        case EntitySelectedEvent::ItemType::Light:
            if (m_SelectedLight) {
                DrawLightInspector();
            }
            break;

        case EntitySelectedEvent::ItemType::Camera:
            if (m_SelectedCamera) {
                DrawCameraInspector();
            }
            break;

        default:
            ImGui::TextDisabled("Select an item from the scene list.");
            break;
    }

#if defined(_DEBUG)
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Debug Render")) {
        bool showGizmos = m_ShowDebugLightGizmos;
        if (ImGui::Checkbox("Show wireframe spotlight", &showGizmos)) {
            m_ShowDebugLightGizmos = showGizmos;
            EmitEvent(std::make_unique<DebugRenderToggledEvent>(
                DebugRenderToggledEvent::Toggle::LightGizmos, showGizmos
            ));
        }
        bool showPlane = m_ShowDebugWireframePlane;
        if (ImGui::Checkbox("Show wireframe plane", &showPlane)) {
            m_ShowDebugWireframePlane = showPlane;
            EmitEvent(std::make_unique<DebugRenderToggledEvent>(
                DebugRenderToggledEvent::Toggle::WireframePlane, showPlane
            ));
        }
    }
#endif

    ImGui::End();
}

void InspectorPanel::DrawObjectInspector() {
    if (!m_SelectedEntity) {
        return;
    }

    ImGui::Text("Object %d", m_SelectedObjectIndex);
    ImGui::Separator();

    // ------------------------------------------------------------------------
    // Position — with Undo/Redo integration via CommandExecuteRequestedEvent
    // ------------------------------------------------------------------------
    DirectX::XMFLOAT3 currentPos = m_SelectedEntity->transform.Position;
    float position[3] = { currentPos.x, currentPos.y, currentPos.z };

    if (ImGui::DragFloat3("Position", position, 0.05f)) {
        m_SelectedEntity->transform.SetPosition(position[0], position[1], position[2]);
    }

    if (ImGui::IsItemActivated()) {
        m_PositionBeforeEdit = currentPos;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        DirectX::XMFLOAT3 newPos = m_SelectedEntity->transform.Position;
        if (newPos.x != m_PositionBeforeEdit.x ||
            newPos.y != m_PositionBeforeEdit.y ||
            newPos.z != m_PositionBeforeEdit.z) {
            EmitEvent(std::make_unique<CommandExecuteRequestedEvent>(
                std::make_unique<TransformEntityCommand>(
                    m_SelectedEntity, m_PositionBeforeEdit, newPos
                )
            ));
        }
    }

    float scale[3] = {
        m_SelectedEntity->transform.Scale.x,
        m_SelectedEntity->transform.Scale.y,
        m_SelectedEntity->transform.Scale.z
    };
    if (ImGui::DragFloat3("Scale", scale, 0.05f, 0.01f, 100.0f)) {
        m_SelectedEntity->transform.SetScale(scale[0], scale[1], scale[2]);
    }

    float rotationDegrees[3] = {
        m_SelectedEulerDegrees.x,
        m_SelectedEulerDegrees.y,
        m_SelectedEulerDegrees.z
    };
    if (ImGui::DragFloat3("Rotation (Roll/Pitch/Yaw) [deg]", rotationDegrees, 0.25f, -360.0f, 360.0f)) {
        m_SelectedEulerDegrees = { rotationDegrees[0], rotationDegrees[1], rotationDegrees[2] };
        const float rollRad = DirectX::XMConvertToRadians(rotationDegrees[0]);
        const float pitchRad = DirectX::XMConvertToRadians(rotationDegrees[1]);
        const float yawRad = DirectX::XMConvertToRadians(rotationDegrees[2]);
        m_SelectedEntity->transform.SetRotation(pitchRad, yawRad, rollRad);
    }

    ImGui::Checkbox("Wireframe", &m_SelectedEntity->ForceWireframe);
    ImGui::Checkbox("Disable depth test", &m_SelectedEntity->DisableDepthTest);
    ImGui::Checkbox("Cast shadows", &m_SelectedEntity->CastShadows);
    ImGui::Checkbox("Receive shadows", &m_SelectedEntity->ReceiveShadows);

    if (!m_SelectedEntity->material) {
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Material");

    Renderer::MaterialProperties properties = m_SelectedEntity->material->GetProperties();

    float baseColor[4] = {
        properties.BaseColor.x, properties.BaseColor.y,
        properties.BaseColor.z, properties.BaseColor.w
    };
    float emissive[3] = {
        properties.Emissive.x, properties.Emissive.y, properties.Emissive.z
    };
    float metallic = properties.Metallic;
    float roughness = properties.Roughness;

    bool materialChanged = false;
    materialChanged |= ImGui::ColorEdit4("Base color", baseColor);
    materialChanged |= ImGui::ColorEdit3("Emission", emissive);
    materialChanged |= ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f);
    materialChanged |= ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);

    if (materialChanged) {
        properties.BaseColor = { baseColor[0], baseColor[1], baseColor[2], baseColor[3] };
        properties.Emissive = { emissive[0], emissive[1], emissive[2] };
        properties.Metallic = metallic;
        properties.Roughness = roughness;

        EmitEvent(std::make_unique<MaterialChangedEvent>(m_SelectedEntity, properties));
    }
}

void InspectorPanel::DrawLightInspector() {
    if (!m_SelectedLight) {
        return;
    }

    ImGui::Text("Light %d", m_SelectedLightIndex);
    ImGui::Separator();

    DirectX::XMFLOAT3 pos = m_SelectedLight->GetPosition();
    float position[3] = { pos.x, pos.y, pos.z };
    if (ImGui::DragFloat3("Position", position, 0.05f)) {
        m_SelectedLight->SetPosition(DirectX::XMFLOAT3(position[0], position[1], position[2]));
    }

    DirectX::XMFLOAT3 dir = m_SelectedLight->GetDirection();
    float direction[3] = { dir.x, dir.y, dir.z };
    if (ImGui::DragFloat3("Direction", direction, 0.05f)) {
        m_SelectedLight->SetDirection(DirectX::XMFLOAT3(direction[0], direction[1], direction[2]));
    }

    float innerAngle = m_SelectedLight->GetInnerAngleDegrees();
    float outerAngle = m_SelectedLight->GetOuterAngleDegrees();
    float coneAngles[2] = { innerAngle, outerAngle };
    if (ImGui::DragFloat2("Angles", coneAngles, 0.05f)) {
        m_SelectedLight->SetConeAngles(coneAngles[0], coneAngles[1]);
    }

    DirectX::XMFLOAT3 color = m_SelectedLight->GetColor();
    float lightColor[3] = { color.x, color.y, color.z };
    if (ImGui::ColorEdit3("Color", lightColor)) {
        m_SelectedLight->SetColor(DirectX::XMFLOAT3(lightColor[0], lightColor[1], lightColor[2]));
    }

    float intensity = m_SelectedLight->GetIntensity();
    if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 100.0f)) {
        m_SelectedLight->SetIntensity(intensity);
    }

    float range = m_SelectedLight->GetRange();
    if (ImGui::DragFloat("Range", &range, 0.05f, 0.1f, 100.0f)) {
        m_SelectedLight->SetRange(range);
    }

    bool castShadows = m_SelectedLight->GetCastShadows();
    if (ImGui::Checkbox("Cast shadows", &castShadows)) {
        m_SelectedLight->SetCastShadows(castShadows);
    }
}

void InspectorPanel::DrawCameraInspector() {
    if (!m_SelectedCamera) {
        return;
    }

    ImGui::Text("Camera %d", m_SelectedCameraIndex);
    ImGui::Separator();

    float position[3] = {
        m_SelectedCamera->m_Transform.Position.x,
        m_SelectedCamera->m_Transform.Position.y,
        m_SelectedCamera->m_Transform.Position.z
    };
    if (ImGui::DragFloat3("Position", position, 0.05f)) {
        m_SelectedCamera->m_Transform.SetPosition(position[0], position[1], position[2]);
    }

    float fovDeg = DirectX::XMConvertToDegrees(m_CameraFov);
    const bool fovChanged = ImGui::SliderFloat("FOV", &fovDeg, 20.0f, 90.0f, "%.1f deg");
    if (fovChanged) {
        m_CameraFov = DirectX::XMConvertToRadians(fovDeg);
    }

    bool projectionChanged = fovChanged;
    projectionChanged |= ImGui::DragFloat("Near", &m_CameraNearPlane, 0.01f, 0.01f, m_CameraFarPlane - 0.01f);
    projectionChanged |= ImGui::DragFloat("Far", &m_CameraFarPlane, 0.1f, m_CameraNearPlane + 0.01f, 1000.0f);

    if (projectionChanged) {
        m_CameraNearPlane = (std::max)(0.01f, m_CameraNearPlane);
        m_CameraFarPlane = (std::max)(m_CameraNearPlane + 0.01f, m_CameraFarPlane);

        EmitEvent(std::make_unique<CameraSettingsChangedEvent>(
            m_CameraFov, m_CameraNearPlane, m_CameraFarPlane
        ));
    }
}
