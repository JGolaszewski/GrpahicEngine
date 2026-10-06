#include "InspectorPanel.h"
#include "renderer/Entity.h"
#include "renderer/Light.h"
#include "renderer/Camera.h"
#include "renderer/Material.h"
#include "commands/TransformEntityCommand.h"
#include "commands/ChangeMaterialPropertiesCommand.h"
#include "events/AssetRegistryChangedEvent.h"
#include "events/MaterialTextureAssignmentRequestedEvent.h"

#include <imgui.h>

#include <algorithm>
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
            if (m_SelectedItemType == EntitySelectedEvent::ItemType::Object) {
                EmitEvent(std::make_unique<RemoveEntityRequestedEvent>(m_SelectedEntityId));
            } else {
                EmitEvent(std::make_unique<EntityDeselectedEvent>());
            }
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
		m_SelectedEntityId = e.GetEntityId();
		m_SelectedLightId = e.GetLightId();
		m_SelectedCameraId = e.GetCameraId();

        m_SelectedEntity = e.GetEntity();
        m_SelectedLight = e.GetLight();
        m_SelectedCamera = e.GetCamera();
        m_MaterialEditActive = false;
		m_SelectedMaterialSlot = -1;
		if (const auto entity = m_SelectedEntity.lock(); entity && entity->ModelGuid) {
			for (const auto& submesh : entity->Submeshes) {
				if (submesh.MaterialSlot >= 0) { m_SelectedMaterialSlot = submesh.MaterialSlot; break; }
			}
		}

        m_SelectedEulerDegrees = { 0.0f, 0.0f, 0.0f };
        return false;
    });

    // State event: selection cleared.
    dispatcher.Dispatch<EntityDeselectedEvent>([this](const auto&) {
        m_SelectedItemType = EntitySelectedEvent::ItemType::None;
        m_SelectedObjectIndex = -1;
        m_SelectedLightIndex = -1;
        m_SelectedCameraIndex = -1;
		m_SelectedEntityId = Engine::InvalidEntityId;
		m_SelectedLightId = Engine::InvalidLightId;
		m_SelectedCameraId = Engine::InvalidCameraId;
		m_SelectedEntity.reset();
		m_SelectedLight.reset();
        m_SelectedCamera.reset();
		m_SelectedMaterialSlot = -1;
        m_MaterialEditActive = false;
        return false;
    });

    // State event: camera settings updated (e.g. from Editor init).
    dispatcher.Dispatch<CameraSettingsChangedEvent>([this](const auto& e) {
        m_CameraFov = e.GetFov();
        m_CameraNearPlane = e.GetNearPlane();
        m_CameraFarPlane = e.GetFarPlane();
        return false;
    });
	dispatcher.Dispatch<AssetRegistryChangedEvent>([this](const auto& e) {
		m_Textures = e.GetTextures();
		return false;
	});
}

void InspectorPanel::Draw() {
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);

    switch (m_SelectedItemType) {
        case EntitySelectedEvent::ItemType::Object:
            if (!m_SelectedEntity.expired()) {
                DrawObjectInspector();
            }
            break;

        case EntitySelectedEvent::ItemType::Light:
            if (!m_SelectedLight.expired()) {
                DrawLightInspector();
            }
            break;

        case EntitySelectedEvent::ItemType::Camera:
            if (!m_SelectedCamera.expired()) {
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
    const auto selectedEntity = m_SelectedEntity.lock();
    if (!selectedEntity) {
        return;
    }

    ImGui::Text("Object %d", m_SelectedObjectIndex);
    ImGui::Separator();
    bool sceneChanged = false;

    // ------------------------------------------------------------------------
    // Position — with Undo/Redo integration via CommandExecuteRequestedEvent
    // ------------------------------------------------------------------------
    DirectX::XMFLOAT3 currentPos = selectedEntity->transform.Position;
    float position[3] = { currentPos.x, currentPos.y, currentPos.z };

    if (ImGui::DragFloat3("Position", position, 0.05f)) {
        selectedEntity->transform.SetPosition(position[0], position[1], position[2]);
    }

    if (ImGui::IsItemActivated()) {
        m_PositionBeforeEdit = currentPos;
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        DirectX::XMFLOAT3 newPos = selectedEntity->transform.Position;
        if (newPos.x != m_PositionBeforeEdit.x ||
            newPos.y != m_PositionBeforeEdit.y ||
            newPos.z != m_PositionBeforeEdit.z) {
            EmitEvent(std::make_unique<CommandExecuteRequestedEvent>(
                std::make_unique<TransformEntityCommand>(
                    selectedEntity, m_PositionBeforeEdit, newPos
                )
            ));
        }
    }

    float scale[3] = {
        selectedEntity->transform.Scale.x,
        selectedEntity->transform.Scale.y,
        selectedEntity->transform.Scale.z
    };
    if (ImGui::DragFloat3("Scale", scale, 0.05f, 0.01f, 100.0f)) {
        selectedEntity->transform.SetScale(scale[0], scale[1], scale[2]);
        sceneChanged = true;
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
        selectedEntity->transform.SetRotation(pitchRad, yawRad, rollRad);
        sceneChanged = true;
    }

    sceneChanged |= ImGui::Checkbox("Wireframe", &selectedEntity->ForceWireframe);
    sceneChanged |= ImGui::Checkbox("Disable depth test", &selectedEntity->DisableDepthTest);
    sceneChanged |= ImGui::Checkbox("Cast shadows", &selectedEntity->CastShadows);
    sceneChanged |= ImGui::Checkbox("Receive shadows", &selectedEntity->ReceiveShadows);

	if (selectedEntity->ModelGuid) {
		ImGui::Text("Model GUID: %s", selectedEntity->ModelGuid->c_str());
		std::vector<int32_t> slots;
		for (const auto& submesh : selectedEntity->Submeshes) {
			if (submesh.MaterialSlot >= 0 && std::find(slots.begin(), slots.end(), submesh.MaterialSlot) == slots.end())
				slots.push_back(submesh.MaterialSlot);
		}
		std::sort(slots.begin(), slots.end());
		if (!slots.empty()) {
			const std::string slotLabel = "Material slot " + std::to_string(m_SelectedMaterialSlot);
			if (ImGui::BeginCombo("Material Slot", slotLabel.c_str())) {
				for (int32_t slot : slots) {
					const std::string label = "Material slot " + std::to_string(slot);
					if (ImGui::Selectable(label.c_str(), slot == m_SelectedMaterialSlot)) {
						m_SelectedMaterialSlot = slot;
						m_MaterialEditActive = false;
					}
				}
				ImGui::EndCombo();
			}
		}
	}
	auto selectedMaterial = selectedEntity->material;
	if (selectedEntity->ModelGuid && m_SelectedMaterialSlot >= 0) {
		const auto found = std::find_if(selectedEntity->Submeshes.begin(), selectedEntity->Submeshes.end(),
			[this](const auto& submesh) { return submesh.MaterialSlot == m_SelectedMaterialSlot; });
		selectedMaterial = found == selectedEntity->Submeshes.end() ? nullptr : found->material;
	}

    if (!selectedMaterial) {
        if (sceneChanged) EmitEvent(std::make_unique<SceneModifiedEvent>());
        return;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Material");

    DrawTextureAssignment("Base color map", selectedMaterial, AssetImportRequestedEvent::TextureSemantic::BaseColor);
	DrawTextureAssignment("Normal map", selectedMaterial, AssetImportRequestedEvent::TextureSemantic::Normal);

	Renderer::MaterialProperties properties = selectedMaterial->GetProperties();
    const Renderer::MaterialProperties materialPropertiesBeforeFrame = properties;
    bool materialEditFinished = false;
    const auto trackMaterialWidget = [this, &materialPropertiesBeforeFrame, &materialEditFinished]() {
        if (ImGui::IsItemActivated()) {
            m_MaterialBeforeEdit = materialPropertiesBeforeFrame;
            m_MaterialEditActive = true;
        }
        materialEditFinished |= ImGui::IsItemDeactivatedAfterEdit();
    };

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
    trackMaterialWidget();
    materialChanged |= ImGui::ColorEdit3("Emission", emissive);
    trackMaterialWidget();
    materialChanged |= ImGui::SliderFloat("Metallic", &metallic, 0.0f, 1.0f);
    trackMaterialWidget();
    materialChanged |= ImGui::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
    trackMaterialWidget();

    static constexpr const char* brdfModelNames[] = { "Legacy", "GGX", "GTR" };
    int selectedBrdfModel = static_cast<uint32_t>(properties.Model) <= static_cast<uint32_t>(Renderer::BrdfModel::GTR)
        ? static_cast<int>(properties.Model)
        : static_cast<int>(Renderer::BrdfModel::Legacy);
    if (ImGui::Combo("Light model", &selectedBrdfModel, brdfModelNames, IM_ARRAYSIZE(brdfModelNames))) {
        properties.Model = static_cast<Renderer::BrdfModel>(selectedBrdfModel);
        materialChanged = true;
    }
    trackMaterialWidget();

    if (properties.Model == Renderer::BrdfModel::GTR) {
        materialChanged |= ImGui::SliderFloat("GTR gamma", &properties.GtrGamma, 1.0f, 8.0f, "%.2f");
        trackMaterialWidget();
    }

    if (properties.Model != Renderer::BrdfModel::Legacy) {
        int specularMode = static_cast<int>(properties.SpecularMode);
        static constexpr const char* specularModeNames[] = { "Index of refraction", "Explicit F0" };
        if (ImGui::Combo("Dielectric specular", &specularMode, specularModeNames, IM_ARRAYSIZE(specularModeNames))) {
            properties.SpecularMode = static_cast<Renderer::DielectricSpecularMode>(specularMode);
            materialChanged = true;
        }
        trackMaterialWidget();

        if (properties.SpecularMode == Renderer::DielectricSpecularMode::IndexOfRefraction) {
            materialChanged |= ImGui::SliderFloat("Index of refraction", &properties.Ior, 1.0f, 3.0f, "%.2f");
            trackMaterialWidget();
        } else {
            float specularF0[3] = { properties.SpecularF0.x, properties.SpecularF0.y, properties.SpecularF0.z };
            if (ImGui::ColorEdit3("Dielectric F0", specularF0)) {
                properties.SpecularF0 = { specularF0[0], specularF0[1], specularF0[2] };
                materialChanged = true;
            }
            trackMaterialWidget();
        }
    }

    if (materialChanged) {
        properties.BaseColor = { baseColor[0], baseColor[1], baseColor[2], baseColor[3] };
        properties.Emissive = { emissive[0], emissive[1], emissive[2] };
        properties.Metallic = metallic;
        properties.Roughness = roughness;

		EmitEvent(std::make_unique<MaterialChangedEvent>(m_SelectedEntityId, properties, m_SelectedMaterialSlot));
    }
	if (materialEditFinished && m_MaterialEditActive) {
		if (materialChanged) {
			EmitEvent(std::make_unique<CommandExecuteRequestedEvent>(
				std::make_unique<ChangeMaterialPropertiesCommand>(selectedMaterial,
					m_MaterialBeforeEdit, properties)));
		}
		m_MaterialEditActive = false;
	}
	if (sceneChanged) EmitEvent(std::make_unique<SceneModifiedEvent>());
}

void InspectorPanel::DrawTextureAssignment(const char* label,
	const std::shared_ptr<Renderer::Material>& material, AssetImportRequestedEvent::TextureSemantic semantic) {
	const auto& reference = semantic == AssetImportRequestedEvent::TextureSemantic::BaseColor
		? material->GetBaseColorTextureGuid() : material->GetNormalTextureGuid();
	std::string preview = "Imported/default";
	if (reference) {
		const auto asset = std::find_if(m_Textures.begin(), m_Textures.end(), [&](const auto& item) {
			return item.guid == *reference;
		});
		preview = asset == m_Textures.end() ? "Missing texture: " + *reference : asset->name;
	}
	if (ImGui::BeginCombo(label, preview.c_str())) {
		const bool defaultSelected = !reference;
		if (ImGui::Selectable("Imported/default", defaultSelected)) {
			EmitEvent(std::make_unique<MaterialTextureAssignmentRequestedEvent>(
				m_SelectedEntityId, m_SelectedMaterialSlot, semantic, std::nullopt));
		}
		for (const auto& texture : m_Textures) {
			const bool selected = reference && *reference == texture.guid;
			if (ImGui::Selectable(texture.name.c_str(), selected)) {
				EmitEvent(std::make_unique<MaterialTextureAssignmentRequestedEvent>(
					m_SelectedEntityId, m_SelectedMaterialSlot, semantic, texture.guid));
			}
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n%s", texture.guid.c_str(), texture.sourcePath.generic_string().c_str());
		}
		ImGui::EndCombo();
	}
	if (reference && std::none_of(m_Textures.begin(), m_Textures.end(), [&](const auto& item) { return item.guid == *reference; })) {
		ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f), "Missing texture reference: %s", reference->c_str());
	}
	ImGui::SameLine();
	if (ImGui::SmallButton((std::string("Import##") + label).c_str())) {
		EmitEvent(std::make_unique<AssetImportRequestedEvent>(AssetImportRequestedEvent::AssetType::Texture,
			m_SelectedEntityId, m_SelectedMaterialSlot, semantic));
	}
}

void InspectorPanel::DrawLightInspector() {
    const auto selectedLight = m_SelectedLight.lock();
    if (!selectedLight) {
        return;
    }

    ImGui::Text("Light %d", m_SelectedLightIndex);
    ImGui::Separator();
    bool sceneChanged = false;

    DirectX::XMFLOAT3 pos = selectedLight->GetPosition();
    float position[3] = { pos.x, pos.y, pos.z };
    if (ImGui::DragFloat3("Position", position, 0.05f)) {
        selectedLight->SetPosition(DirectX::XMFLOAT3(position[0], position[1], position[2]));
        sceneChanged = true;
    }

    DirectX::XMFLOAT3 dir = selectedLight->GetDirection();
    float direction[3] = { dir.x, dir.y, dir.z };
    if (ImGui::DragFloat3("Direction", direction, 0.05f)) {
        selectedLight->SetDirection(DirectX::XMFLOAT3(direction[0], direction[1], direction[2]));
        sceneChanged = true;
    }

    float innerAngle = selectedLight->GetInnerAngleDegrees();
    float outerAngle = selectedLight->GetOuterAngleDegrees();
    float coneAngles[2] = { innerAngle, outerAngle };
    if (ImGui::DragFloat2("Angles", coneAngles, 0.05f)) {
        selectedLight->SetConeAngles(coneAngles[0], coneAngles[1]);
        sceneChanged = true;
    }

    DirectX::XMFLOAT3 color = selectedLight->GetColor();
    float lightColor[3] = { color.x, color.y, color.z };
    if (ImGui::ColorEdit3("Color", lightColor)) {
        selectedLight->SetColor(DirectX::XMFLOAT3(lightColor[0], lightColor[1], lightColor[2]));
        sceneChanged = true;
    }

    float intensity = selectedLight->GetIntensity();
    if (ImGui::DragFloat("Intensity", &intensity, 0.05f, 0.0f, 100.0f)) {
        selectedLight->SetIntensity(intensity);
        sceneChanged = true;
    }

    float range = selectedLight->GetRange();
    if (ImGui::DragFloat("Range", &range, 0.05f, 0.1f, 100.0f)) {
        selectedLight->SetRange(range);
        sceneChanged = true;
    }

    bool castShadows = selectedLight->GetCastShadows();
    if (ImGui::Checkbox("Cast shadows", &castShadows)) {
        selectedLight->SetCastShadows(castShadows);
        sceneChanged = true;
    }
	if (sceneChanged) EmitEvent(std::make_unique<SceneModifiedEvent>());
}

void InspectorPanel::DrawCameraInspector() {
    const auto selectedCamera = m_SelectedCamera.lock();
    if (!selectedCamera) {
        return;
    }

    ImGui::Text("Camera %d", m_SelectedCameraIndex);
    ImGui::Separator();

    float position[3] = {
        selectedCamera->m_Transform.Position.x,
        selectedCamera->m_Transform.Position.y,
        selectedCamera->m_Transform.Position.z
    };
    if (ImGui::DragFloat3("Position", position, 0.05f)) {
        selectedCamera->m_Transform.SetPosition(position[0], position[1], position[2]);
        EmitEvent(std::make_unique<SceneModifiedEvent>());
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
