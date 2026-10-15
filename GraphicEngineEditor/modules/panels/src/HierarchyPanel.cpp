#include "panels/include/HierarchyPanel.h"
#include "rendering/include/renderer/Entity.h"

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
        m_Scene = e.GetScene();
        return false;
    });

    dispatcher.Dispatch<EntitySelectedEvent>([this](const auto &e) {
        m_SelectedItemType = e.GetItemType();
        m_SelectedObjectIndex = e.GetObjectIndex();
        m_SelectedObjectId = e.GetEntityId();
        m_SelectedLightIndex = e.GetLightIndex();
        m_SelectedCameraIndex = e.GetCameraIndex();
        return false;
    });

    dispatcher.Dispatch<EntityDeselectedEvent>([this](const auto &) {
        m_SelectedItemType = EntitySelectedEvent::ItemType::None;
        m_SelectedObjectIndex = -1;
        m_SelectedObjectId = Engine::InvalidEntityId;
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
        if (m_Scene)
        {
            DrawEntityTree(m_Scene->GetRoot());
            DrawRootDropTarget();
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

void HierarchyPanel::DrawEntityTree(const Engine::Object &parent)
{
    for (const auto &child : parent.GetChildren())
    {
        DrawEntityNode(child);
    }
}

void HierarchyPanel::DrawEntityNode(const std::shared_ptr<Engine::Object> &object)
{
    const auto entity = std::dynamic_pointer_cast<::Engine::Renderer::Entity>(object);
    if (!entity || !m_Scene)
    {
        return;
    }
    const Engine::EntityId entityId = m_Scene->GetEntityId(entity.get());
    const std::string label = entity->Name.empty() ? "Entity " + std::to_string(entityId) : entity->Name;
    ImGui::PushID(static_cast<int>(entityId >> 32U));
    ImGui::PushID(static_cast<int>(entityId & 0xffffffffU));
    const bool hasChildren = !entity->GetChildren().empty();
    const ImGuiTreeNodeFlags flags =
        (hasChildren ? 0 : ImGuiTreeNodeFlags_Leaf) | ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        (m_SelectedItemType == EntitySelectedEvent::ItemType::Object && m_SelectedObjectId == entityId
             ? ImGuiTreeNodeFlags_Selected
             : 0);
    const bool expanded = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked())
    {
        const auto found = std::ranges::find(m_ObjectIds, entityId);
        const int index =
            found == m_ObjectIds.end() ? -1 : static_cast<int>(std::distance(m_ObjectIds.begin(), found));
        EmitEvent(std::make_unique<EntitySelectedEvent>(EntitySelectedEvent::ItemType::Object, index, -1, -1,
                                                        entityId, Engine::InvalidLightId,
                                                        Engine::InvalidCameraId));
    }
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("SCENE_ENTITY_ID", &entityId, sizeof(entityId));
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const auto *payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY_ID"))
        {
            const auto draggedId = *static_cast<const Engine::EntityId *>(payload->Data);
            if (draggedId != entityId)
            {
                EmitEvent(std::make_unique<EntityReparentRequestedEvent>(draggedId, entityId));
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (expanded)
    {
        DrawEntityTree(*entity);
        ImGui::TreePop();
    }
    ImGui::PopID();
    ImGui::PopID();
}

void HierarchyPanel::DrawRootDropTarget()
{
    ImGui::Separator();
    ImGui::Button("Scene Root", ImVec2(-1.0F, 0.0F));
    if (ImGui::BeginDragDropTarget())
    {
        if (const auto *payload = ImGui::AcceptDragDropPayload("SCENE_ENTITY_ID"))
        {
            const auto draggedId = *static_cast<const Engine::EntityId *>(payload->Data);
            EmitEvent(std::make_unique<EntityReparentRequestedEvent>(draggedId, Engine::InvalidEntityId));
        }
        ImGui::EndDragDropTarget();
    }
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

    if (m_PendingCreation)
    {
        const auto result = m_PendingCreation->GetSnapshot();
        if (result.state == SceneCreationResult::State::Succeeded)
        {
            m_SpawnPosition.x += 2.5f;
            m_PendingCreation.reset();
            m_CreationError.clear();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        if (result.state == SceneCreationResult::State::Failed)
        {
            m_CreationError = result.error;
            m_PendingCreation.reset();
        }
    }

    static const char *shapeItems[] = {"Cube", "Sphere", "Plane", "Cone"};
    int selectedShape = static_cast<int>(m_SpawnShape);
    if (ImGui::Combo("Type", &selectedShape, shapeItems, IM_ARRAYSIZE(shapeItems)))
    {
        m_SpawnShape = static_cast<SpawnShapeType>(selectedShape);
    }

    ImGui::InputText("Name", m_SpawnName, sizeof(m_SpawnName));

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
            auto request = std::make_unique<AddModelRequestedEvent>(model.guid, m_SpawnPosition, m_SpawnScale,
                                                                    m_SpawnName);
            m_PendingCreation = request->GetCreationResult();
            m_CreationError.clear();
            EmitEvent(std::move(request));
            const auto result = m_PendingCreation->GetSnapshot();
            if (result.state == SceneCreationResult::State::Succeeded)
            {
                m_SpawnPosition.x += 2.5f;
                m_PendingCreation.reset();
                ImGui::CloseCurrentPopup();
            }
            else if (result.state == SceneCreationResult::State::Failed)
            {
                m_CreationError = result.error;
                m_PendingCreation.reset();
            }
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

        auto request = std::make_unique<AddObjectRequestedEvent>(
            shape, m_SpawnPosition, m_SpawnScale, m_SpawnColor, m_SpawnEmissive, m_SpawnMetallic,
            m_SpawnRoughness, m_SpawnName);
        m_PendingCreation = request->GetCreationResult();
        m_CreationError.clear();
        EmitEvent(std::move(request));
        const auto result = m_PendingCreation->GetSnapshot();
        if (result.state == SceneCreationResult::State::Succeeded)
        {
            m_SpawnPosition.x += 2.5f;
            m_PendingCreation.reset();
            ImGui::CloseCurrentPopup();
        }
        else if (result.state == SceneCreationResult::State::Failed)
        {
            m_CreationError = result.error;
            m_PendingCreation.reset();
        }
    }

    if (m_PendingCreation)
        ImGui::TextDisabled("Creation is pending...");
    if (!m_CreationError.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_CreationError.c_str());
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(140.0f, 0.0f)))
    {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}
