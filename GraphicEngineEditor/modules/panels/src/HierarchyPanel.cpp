#include "panels/include/HierarchyPanel.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <string>
#include <utility>

namespace {
constexpr const char* kEntityPayload = "ECS_ENTITY";

auto HasText(const char* text, const char* query) -> bool {
  if (query[0] == '\0') {
    return true;
  }
  std::string haystack(text);
  std::string needle(query);
  std::ranges::transform(haystack, haystack.begin(),
                         [](unsigned char value) -> char { return static_cast<char>(std::tolower(value)); });
  std::ranges::transform(needle, needle.begin(),
                         [](unsigned char value) -> char { return static_cast<char>(std::tolower(value)); });
  return haystack.contains(needle);
}
} // namespace

void HierarchyPanel::OnEvent(Engine::Events::Event& event) {
  Engine::Events::EventDispatcher dispatcher(event);
  dispatcher.Dispatch<SceneChangedEvent>([this](const auto& sceneEvent) -> bool {
    m_Scene = sceneEvent.GetScene();
    return false;
  });
  dispatcher.Dispatch<EcsEntitySelectedEvent>([this](const auto& selection) -> bool {
    m_SelectedEntity = selection.GetEntity();
    return false;
  });
  dispatcher.Dispatch<EntityDeselectedEvent>([this](const auto&) -> bool {
    m_SelectedEntity = Engine::InvalidEntityHandle;
    return false;
  });
}

void HierarchyPanel::Draw() {
  ImGui::Begin("Scene Hierarchy", nullptr, ImGuiWindowFlags_NoCollapse);
  if (!m_Scene) {
    ImGui::TextUnformatted("No active scene");
    ImGui::End();
    return;
  }

  (void)ImGui::InputTextWithHint("##hierarchy-filter", "Search entities or components...", m_Filter.data(),
                                 m_Filter.size());
  const std::vector<Engine::Scene::EntitySnapshot> entities = m_Scene->HierarchySnapshot();
  for (const auto& entity : entities) {
    if (entity.Parent == Engine::InvalidEntityHandle && SubtreeMatches(entity, entities)) {
      DrawEntityNode(entity, entities);
    }
  }
  if (entities.empty()) {
    ImGui::TextUnformatted("No entities");
  }
  DrawRootDropTarget();
  if (ImGui::BeginPopupContextWindow("HierarchyEmptyContext", ImGuiPopupFlags_MouseButtonRight)) {
    DrawCreateMenu(Engine::InvalidEntityHandle);
    ImGui::EndPopup();
  }
  if (ImGui::Button("+ Create Entity", ImVec2(-1.0F, 0.0F))) {
    ImGui::OpenPopup("Create entity");
  }
  if (ImGui::BeginPopup("Create entity")) {
    DrawCreateMenu(Engine::InvalidEntityHandle);
    ImGui::EndPopup();
  }
  ImGui::End();
}

auto HierarchyPanel::MatchesFilter(const Engine::Scene::EntitySnapshot& entity) const -> bool {
  if (m_Filter[0] == '\0') {
    return true;
  }
  const char* query = m_Filter.data();
  if (HasText(entity.Name.c_str(), query)) {
    return true;
  }
  if (HasText("Transform", query) || HasText("Hierarchy", query) || HasText("PersistentIdentity", query)) {
    return true;
  }
  return (entity.HasMeshRenderer && (HasText("MeshRenderer", query) || HasText("Mesh", query))) ||
         (entity.HasSpotLight && (HasText("SpotLight", query) || HasText("Light", query))) ||
         (entity.HasCamera && HasText("Camera", query));
}

auto HierarchyPanel::SubtreeMatches(const Engine::Scene::EntitySnapshot& entity,
                                    const std::vector<Engine::Scene::EntitySnapshot>& entities) const -> bool {
  if (MatchesFilter(entity)) {
    return true;
  }
  return std::ranges::any_of(entities, [this, &entity, &entities](const auto& candidate) -> bool {
    return candidate.Parent == entity.Handle && SubtreeMatches(candidate, entities);
  });
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void HierarchyPanel::DrawEntityNode(const Engine::Scene::EntitySnapshot& entity,
                                    const std::vector<Engine::Scene::EntitySnapshot>& entities) {
  const bool hasChildren = std::ranges::any_of(entities, [this, &entity, &entities](const auto& candidate) -> bool {
    return candidate.Parent == entity.Handle && SubtreeMatches(candidate, entities);
  });
  if (!SubtreeMatches(entity, entities)) {
    return;
  }
  const std::string name = entity.Name.empty() ? "Entity " + std::to_string(entity.Handle.Index) : entity.Name;
  const char* icon = "[Entity]";
  if (entity.HasSpotLight) {
    icon = "[Light]";
  } else if (entity.HasCamera) {
    icon = "[Camera]";
  } else if (entity.HasMeshRenderer) {
    icon = "[Mesh]";
  }
  const std::string label = std::string(icon) + " " + name;
  ImGui::PushID(static_cast<int>(entity.Handle.Index));
  ImGui::PushID(static_cast<int>(entity.Handle.Generation));
  const ImGuiTreeNodeFlags flags =
      (hasChildren ? ImGuiTreeNodeFlags_None : ImGuiTreeNodeFlags_Leaf) | ImGuiTreeNodeFlags_OpenOnArrow |
      ImGuiTreeNodeFlags_SpanAvailWidth |
      (m_SelectedEntity == entity.Handle ? ImGuiTreeNodeFlags_Selected : ImGuiTreeNodeFlags_None);
  const bool expanded = ImGui::TreeNodeEx(label.c_str(), flags);
  if (ImGui::IsItemClicked()) {
    EmitEvent(std::make_unique<EcsEntitySelectedEvent>(entity.Handle));
  }
  if (ImGui::BeginPopupContextItem("EntityContext")) {
    if (ImGui::MenuItem("Create child entity")) {
      EmitEvent(std::make_unique<EcsEntityCreateRequestedEvent>("Entity", entity.Handle));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Create child mesh")) {
      EmitEvent(
          std::make_unique<EcsEntityCreateRequestedEvent>("Mesh", entity.Handle, Engine::Scene::EntityPreset::Mesh));
    }
    if (ImGui::MenuItem("Create child spot light")) {
      EmitEvent(std::make_unique<EcsEntityCreateRequestedEvent>("Spot Light", entity.Handle,
                                                                Engine::Scene::EntityPreset::SpotLight));
    }
    if (ImGui::MenuItem("Create child camera")) {
      EmitEvent(std::make_unique<EcsEntityCreateRequestedEvent>("Camera", entity.Handle,
                                                                Engine::Scene::EntityPreset::Camera));
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Delete entity")) {
      EmitEvent(std::make_unique<EcsEntityDeleteRequestedEvent>(entity.Handle));
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginDragDropSource()) {
    ImGui::SetDragDropPayload(kEntityPayload, &entity.Handle, sizeof(entity.Handle));
    ImGui::TextUnformatted(label.c_str());
    ImGui::EndDragDropSource();
  }
  if (ImGui::BeginDragDropTarget()) {
    if (const auto* payload = ImGui::AcceptDragDropPayload(kEntityPayload);
        payload != nullptr && std::cmp_equal(payload->DataSize, sizeof(Engine::EntityHandle))) {
      Engine::EntityHandle dragged;
      std::memcpy(&dragged, payload->Data, sizeof(dragged));
      if (dragged != entity.Handle) {
        EmitEvent(std::make_unique<EcsEntityReparentRequestedEvent>(dragged, entity.Handle));
      }
    }
    ImGui::EndDragDropTarget();
  }
  if (expanded) {
    for (const auto& child : entities) {
      if (child.Parent == entity.Handle && SubtreeMatches(child, entities)) {
        DrawEntityNode(child, entities);
      }
    }
    ImGui::TreePop();
  }
  ImGui::PopID();
  ImGui::PopID();
}

void HierarchyPanel::DrawRootDropTarget() {
  ImGui::Separator();
  (void)ImGui::Button("Scene Root", ImVec2(-1.0F, 0.0F));
  if (ImGui::BeginDragDropTarget()) {
    if (const auto* payload = ImGui::AcceptDragDropPayload(kEntityPayload);
        payload != nullptr && std::cmp_equal(payload->DataSize, sizeof(Engine::EntityHandle))) {
      Engine::EntityHandle dragged;
      std::memcpy(&dragged, payload->Data, sizeof(dragged));
      EmitEvent(std::make_unique<EcsEntityReparentRequestedEvent>(dragged, Engine::InvalidEntityHandle));
    }
    ImGui::EndDragDropTarget();
  }
}

void HierarchyPanel::DrawCreateMenu(Engine::EntityHandle parent) {
  (void)ImGui::InputText("Name", m_NewEntityName.data(), m_NewEntityName.size());
  if (ImGui::MenuItem("Create Empty Entity")) {
    EmitEvent(std::make_unique<EcsEntityCreateRequestedEvent>(m_NewEntityName.data(), parent));
  }
  if (ImGui::MenuItem("Create Mesh Entity")) {
    EmitEvent(std::make_unique<EcsEntityCreateRequestedEvent>(m_NewEntityName.data(), parent,
                                                              Engine::Scene::EntityPreset::Mesh));
  }
  if (ImGui::MenuItem("Create Spot Light")) {
    EmitEvent(std::make_unique<EcsEntityCreateRequestedEvent>(m_NewEntityName.data(), parent,
                                                              Engine::Scene::EntityPreset::SpotLight));
  }
  if (ImGui::MenuItem("Create Camera")) {
    EmitEvent(std::make_unique<EcsEntityCreateRequestedEvent>(m_NewEntityName.data(), parent,
                                                              Engine::Scene::EntityPreset::Camera));
  }
}
