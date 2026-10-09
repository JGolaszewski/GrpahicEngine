#include "panels/include/HierarchyPanel.h"

#include <algorithm>
#include <imgui.h>
#include <string>

// ================================================================================
// HierarchyPanel::OnEvent
// ================================================================================
// Listens for SceneChangedEvent (to update entity counts) and
// EntitySelectedEvent / EntityDeselectedEvent (to update selection highlight).
// ================================================================================

void HierarchyPanel::OnEvent(Engine::Events::Event &event)
{
    Engine::Events::EventDispatcher dispatcher(event);

    dispatcher.Dispatch<SceneChangedEvent>([this](const auto &e) {
        m_ObjectIds = e.GetObjectIds();
        m_LightIds = e.GetLightIds();
        m_CameraIds = e.GetCameraIds();
        return false;
    });

    dispatcher.Dispatch<EntitySelectedEvent>([this](const auto &e) {
        m_SelectedItemType = e.GetItemType();
        m_SelectedObjectIndex = e.GetObjectIndex();
        m_SelectedLightIndex = e.GetLightIndex();
        m_SelectedCameraIndex = e.GetCameraIndex();
        return false;
    });

    dispatcher.Dispatch<EntityDeselectedEvent>([this](const auto &) {
        m_SelectedItemType = EntitySelectedEvent::ItemType::None;
        m_SelectedObjectIndex = -1;
        m_SelectedLightIndex = -1;
        m_SelectedCameraIndex = -1;
        return false;
    });

    dispatcher.Dispatch<AssetRegistryChangedEvent>([this](const auto &update) {
        m_Models = update.GetModels();
        if (m_Models.empty())
            m_SelectedModelIndex = 0;
        else
            m_SelectedModelIndex =
                (std::clamp)(m_SelectedModelIndex, 0, static_cast<int>(m_Models.size()) - 1);
        return false;
    });
}

void HierarchyPanel::Draw()
{
    ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoCollapse);

    if (ImGui::CollapsingHeader("Objects", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (size_t i = 0; i < m_ObjectIds.size(); ++i)
        {
            const std::string label = "Object " + std::to_string(i);
            const bool selected = m_SelectedItemType == EntitySelectedEvent::ItemType::Object &&
                                  m_SelectedObjectIndex == static_cast<int>(i);
            if (ImGui::Selectable(label.c_str(), selected))
            {
                EmitEvent(std::make_unique<EntitySelectedEvent>(
                    EntitySelectedEvent::ItemType::Object, static_cast<int>(i), -1, -1, m_ObjectIds[i],
                    Engine::InvalidLightId, Engine::InvalidCameraId));
            }
        }

        if (m_ObjectIds.empty())
        {
            ImGui::TextDisabled("No active objects");
        }
    }

    if (ImGui::CollapsingHeader("Lights", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (size_t i = 0; i < m_LightIds.size(); ++i)
        {
            const std::string label = "Light " + std::to_string(i);
            const bool selected = m_SelectedItemType == EntitySelectedEvent::ItemType::Light &&
                                  m_SelectedLightIndex == static_cast<int>(i);
            if (ImGui::Selectable(label.c_str(), selected))
            {
                EmitEvent(std::make_unique<EntitySelectedEvent>(
                    EntitySelectedEvent::ItemType::Light, -1, static_cast<int>(i), -1,
                    Engine::InvalidEntityId, m_LightIds[i], Engine::InvalidCameraId));
            }
        }

        if (m_LightIds.empty())
        {
            ImGui::TextDisabled("No lights");
        }
    }

    if (ImGui::CollapsingHeader("Cameras", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (size_t i = 0; i < m_CameraIds.size(); ++i)
        {
            const std::string label = "Camera " + std::to_string(i);
            const bool selected = m_SelectedItemType == EntitySelectedEvent::ItemType::Camera &&
                                  m_SelectedCameraIndex == static_cast<int>(i);
            if (ImGui::Selectable(label.c_str(), selected))
            {
                EmitEvent(std::make_unique<EntitySelectedEvent>(EntitySelectedEvent::ItemType::Camera, -1, -1,
                                                                static_cast<int>(i), Engine::InvalidEntityId,
                                                                Engine::InvalidLightId, m_CameraIds[i]));
            }
        }

        if (m_CameraIds.empty())
        {
            ImGui::TextDisabled("No cameras");
        }
    }

    ImGui::Spacing();
    DrawAddObjectModal();

    ImGui::End();
}

void HierarchyPanel::DrawAddObjectModal()
{
    if (ImGui::Button("Add", ImVec2(-1.0f, 0.0f)))
    {
        ImGui::OpenPopup("Add object to scene");
    }

    if (!ImGui::BeginPopupModal("Add object to scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }

    static const char *shapeItems[] = {"Cube", "Sphere", "Plane", "Cone"};
    int selectedShape = static_cast<int>(m_SpawnShape);
    if (ImGui::Combo("Type", &selectedShape, shapeItems, IM_ARRAYSIZE(shapeItems)))
    {
        m_SpawnShape = static_cast<SpawnShapeType>(selectedShape);
    }

    float spawnPosition[3] = {m_SpawnPosition.x, m_SpawnPosition.y, m_SpawnPosition.z};
    if (ImGui::DragFloat3("Start position", spawnPosition, 0.05f))
    {
        m_SpawnPosition = {spawnPosition[0], spawnPosition[1], spawnPosition[2]};
    }

    float spawnScale[3] = {m_SpawnScale.x, m_SpawnScale.y, m_SpawnScale.z};
    if (ImGui::DragFloat3("Start scale", spawnScale, 0.05f, 0.05f, 100.0f))
    {
        m_SpawnScale = {spawnScale[0], spawnScale[1], spawnScale[2]};
    }

    float spawnColor[4] = {m_SpawnColor.x, m_SpawnColor.y, m_SpawnColor.z, m_SpawnColor.w};
    if (ImGui::ColorEdit4("Start color", spawnColor))
    {
        m_SpawnColor = {spawnColor[0], spawnColor[1], spawnColor[2], spawnColor[3]};
    }

    float spawnEmissive[3] = {m_SpawnEmissive.x, m_SpawnEmissive.y, m_SpawnEmissive.z};
    if (ImGui::ColorEdit3("Start emission", spawnEmissive))
    {
        m_SpawnEmissive = {spawnEmissive[0], spawnEmissive[1], spawnEmissive[2]};
    }

    ImGui::SliderFloat("Start metallic", &m_SpawnMetallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Start roughness", &m_SpawnRoughness, 0.0f, 1.0f);

    if (!m_Models.empty())
    {
        std::vector<const char *> modelNames;
        modelNames.reserve(m_Models.size());
        for (const auto &model : m_Models)
            modelNames.push_back(model.name.c_str());
        ImGui::SeparatorText("Imported model");
        ImGui::Combo("Asset", &m_SelectedModelIndex, modelNames.data(), static_cast<int>(modelNames.size()));
        if (ImGui::Button("Add selected model", ImVec2(-1.0f, 0.0f)))
        {
            const auto &model = m_Models[static_cast<size_t>(m_SelectedModelIndex)];
            EmitEvent(std::make_unique<AddModelRequestedEvent>(model.guid, m_SpawnPosition, m_SpawnScale));
            m_SpawnPosition.x += 2.5f;
            ImGui::CloseCurrentPopup();
        }
    }

    if (ImGui::Button("Add", ImVec2(140.0f, 0.0f)))
    {
        AddObjectRequestedEvent::Shape shape;
        switch (m_SpawnShape)
        {
        case SpawnShapeType::Cube:
            shape = AddObjectRequestedEvent::Shape::Cube;
            break;
        case SpawnShapeType::Sphere:
            shape = AddObjectRequestedEvent::Shape::Sphere;
            break;
        case SpawnShapeType::Plane:
            shape = AddObjectRequestedEvent::Shape::Plane;
            break;
        case SpawnShapeType::Cone:
            shape = AddObjectRequestedEvent::Shape::Cone;
            break;
        }

        EmitEvent(std::make_unique<AddObjectRequestedEvent>(shape, m_SpawnPosition, m_SpawnScale,
                                                            m_SpawnColor, m_SpawnEmissive, m_SpawnMetallic,
                                                            m_SpawnRoughness));

        m_SpawnPosition.x += 2.5f;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f)))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
