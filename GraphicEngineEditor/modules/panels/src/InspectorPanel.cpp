#include "panels/include/InspectorPanel.h"
#include "InspectorWidgetIds.h"
#include "events/include/AssetImportRequestedEvent.h"
#include "events/include/MaterialAssetEditRequestedEvent.h"
#include "events/include/MaterialAssetSelectionRequestedEvent.h"
#include "panels/include/AssetDragDropPayload.h"
#include "panels/include/AssetDropAssignment.h"
#include "panels/include/TransformRotation.h"
#include "panels/include/TransformScaleLock.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <functional>
#include <imgui.h>
#include <iterator>
#include <limits>
#include <string>

namespace {
using Mutation = EcsComponentMutationRequestedEvent;

auto Matches(const char* name, const char* filter) -> bool {
  if (filter[0] == '\0') {
    return true;
  }
  std::string haystack(name);
  std::string needle(filter);
  std::ranges::transform(haystack, haystack.begin(),
                         [](unsigned char value) -> char { return static_cast<char>(std::tolower(value)); });
  std::ranges::transform(needle, needle.begin(),
                         [](unsigned char value) -> char { return static_cast<char>(std::tolower(value)); });
  return haystack.contains(needle);
}

auto DrawComponentHeader(const char* label, const char* componentName, const char* tableId,
                         Mutation::Component component, const std::function<void(Mutation::Component)>& emitRemove)
    -> bool {
  const float buttonSize = ImGui::GetFrameHeight();
  const bool tableVisible =
      ImGui::BeginTable(tableId, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings);
  if (!tableVisible) {
    return false;
  }

  ImGui::TableSetupColumn("Component header", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("Remove component", ImGuiTableColumnFlags_WidthFixed, buttonSize);
  ImGui::TableNextRow(ImGuiTableRowFlags_None, buttonSize);
  ImGui::TableSetColumnIndex(0);
  const bool open = ImGui::CollapsingHeader(label);
  ImGui::TableSetColumnIndex(1);
  // The button has its own table cell, so clicking it cannot activate the header beside it.
  const std::string buttonId = std::string("##Remove ") + componentName + " component";
  if (ImGui::Button(buttonId.c_str(), ImVec2(buttonSize, buttonSize))) {
    emitRemove(component);
  }
  if (ImGui::IsItemHovered()) {
    const std::string tooltip = std::string("Remove ") + componentName + " component";
    ImGui::SetTooltip("%s", tooltip.c_str());
  }

  const ImVec2 buttonMin = ImGui::GetItemRectMin();
  const ImVec2 buttonMax = ImGui::GetItemRectMax();
  const float centerX = (buttonMin.x + buttonMax.x) * 0.5F;
  const float centerY = (buttonMin.y + buttonMax.y) * 0.5F;
  const ImU32 iconColor = ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_Text : ImGuiCol_TextDisabled);
  ImDrawList* drawList = ImGui::GetWindowDrawList();
  const float scale = buttonSize / 20.0F;
  drawList->AddRectFilled(ImVec2(centerX - 4.0F * scale, centerY - 2.0F * scale),
                          ImVec2(centerX + 4.0F * scale, centerY + 5.0F * scale), iconColor, 1.0F * scale);
  drawList->AddRectFilled(ImVec2(centerX - 5.0F * scale, centerY - 4.0F * scale),
                          ImVec2(centerX + 5.0F * scale, centerY - 3.0F * scale), iconColor, 0.5F * scale);
  drawList->AddRectFilled(ImVec2(centerX - 2.0F * scale, centerY - 6.0F * scale),
                          ImVec2(centerX + 2.0F * scale, centerY - 5.0F * scale), iconColor, 0.5F * scale);
  ImGui::EndTable();
  return open;
}
} // namespace

void InspectorPanel::OnEvent(Engine::Events::Event& event) {
  Engine::Events::EventDispatcher dispatcher(event);
  dispatcher.Dispatch<EcsEntitySelectedEvent>([this](const auto& selection) -> bool {
    m_SelectedEntity = selection.GetEntity();
    return false;
  });
  dispatcher.Dispatch<EntityDeselectedEvent>([this](const auto&) -> bool {
    m_SelectedEntity = Engine::InvalidEntityHandle;
    return false;
  });
  dispatcher.Dispatch<SceneChangedEvent>([this](const auto& sceneEvent) -> bool {
    m_Scene = sceneEvent.GetScene();
    return false;
  });
  dispatcher.Dispatch<AssetRegistryChangedEvent>([this](const auto& update) -> bool {
    m_Models = update.GetModels();
    m_Textures = update.GetTextures();
    m_Materials = update.GetMaterials();
    return false;
  });
  dispatcher.Dispatch<AssetWorkflowFeedbackEvent>([this](const auto& update) -> bool {
    m_AssetFeedbackSucceeded = update.Succeeded();
    m_AssetFeedback = update.GetMessage();
    return false;
  });
}

void InspectorPanel::Draw() {
  ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
  if (m_Scene && m_SelectedEntity != Engine::InvalidEntityHandle && m_Scene->World().IsAlive(m_SelectedEntity)) {
    DrawEntityInspector();
  } else {
    ImGui::TextUnformatted("Select an entity from the scene hierarchy.");
  }
  if (!m_AssetFeedback.empty()) {
    ImGui::TextColored(m_AssetFeedbackSucceeded ? ImVec4(0.45F, 0.85F, 0.5F, 1.0F) : ImVec4(0.95F, 0.4F, 0.35F, 1.0F),
                       "%s", m_AssetFeedback.c_str());
  }

  ImGui::End();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void InspectorPanel::DrawEntityInspector() {
  const Engine::ArchetypeWorld& world = m_Scene->World();
  const Engine::EntityHandle entity = m_SelectedEntity;
  const bool isSceneRoot = m_Scene->IsSceneRoot(entity);
  const auto emitSet = [this, entity](Mutation::Component type, auto value) -> void {
    EmitEvent(std::make_unique<Mutation>(entity, Mutation::Operation::Set, type, Mutation::Payload{std::move(value)}));
  };
  const auto emitAdd = [this, entity](Mutation::Component type) -> void {
    EmitEvent(std::make_unique<Mutation>(entity, Mutation::Operation::Add, type));
  };
  const auto emitRemove = [this, entity](Mutation::Component type) -> void {
    EmitEvent(std::make_unique<Mutation>(entity, Mutation::Operation::Remove, type));
  };

  const std::string entityId = "ID: " + std::to_string(entity.Index) + ":" + std::to_string(entity.Generation);
  ImGui::TextUnformatted(entityId.c_str());
  ImGui::Separator();
  ImGui::PushID(entityId.c_str());
  if (const auto* storedName = world.Get<Engine::Ecs::Name>(entity);
      storedName != nullptr && ImGui::CollapsingHeader("Name", ImGuiTreeNodeFlags_DefaultOpen)) {
    Engine::Ecs::Name name = *storedName;
    std::array<char, 256> buffer{};
    const size_t length = (std::min)(name.Value.size(), buffer.size() - 1);
    std::copy_n(name.Value.data(), length, buffer.data());
    if (isSceneRoot) {
      ImGui::BeginDisabled();
      (void)ImGui::InputText(Editor::Panels::Detail::kEntityNameInputLabel, buffer.data(), buffer.size());
      ImGui::EndDisabled();
    } else if (ImGui::InputText(Editor::Panels::Detail::kEntityNameInputLabel, buffer.data(), buffer.size())) {
      name.Value = buffer.data();
      emitSet(Mutation::Component::Name, std::move(name));
    }
  }
  ImGui::PopID();

  if (!isSceneRoot) {
    if (const auto* storedTransform = world.Get<Engine::Ecs::LocalTransform>(entity);
        storedTransform != nullptr && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
      Engine::Ecs::LocalTransform transform = *storedTransform;
      bool changed = ImGui::DragFloat3("Local position", transform.Position.data(), 0.05F);
      std::array<float, 3> anglesDegrees = Editor::TransformRotation::ToPitchYawRollDegrees(transform.Rotation);
      if (ImGui::DragFloat3("Pitch / Yaw / Roll", anglesDegrees.data(), 0.25F, -360.0F, 360.0F, "%.1f deg")) {
        transform.Rotation = Editor::TransformRotation::FromPitchYawRollDegrees(anglesDegrees);
        changed = true;
      }
      if (ImGui::TreeNode("Quaternion (advanced)")) {
        changed |= ImGui::DragFloat4("Raw quaternion", transform.Rotation.data(), 0.01F);
        ImGui::TreePop();
      }
      const std::array<float, 3> previousScale = transform.Scale;
      const float lockWidth = ImGui::CalcTextSize("Lock").x + ImGui::GetFrameHeight();
      ImGui::SetNextItemWidth(-lockWidth);
      if (ImGui::DragFloat3("Local scale", transform.Scale.data(), 0.05F, 0.01F, 1000.0F)) {
        (void)Editor::TransformScaleLock::Apply(transform.Scale, previousScale, m_LockTransformScale);
        changed = true;
      }
      ImGui::SameLine();
      (void)ImGui::Checkbox("Lock", &m_LockTransformScale);
      changed |= ImGui::Checkbox("Flip horizontal", &transform.FlipHorizontal);
      changed |= ImGui::Checkbox("Flip vertical", &transform.FlipVertical);
      if (changed) {
        emitSet(Mutation::Component::Transform, transform);
      }
    }

    if (const auto* storedVisibility = world.Get<Engine::Ecs::LocalVisibility>(entity);
        storedVisibility != nullptr && ImGui::CollapsingHeader("Visibility")) {
      Engine::Ecs::LocalVisibility visibility = *storedVisibility;
      if (ImGui::Checkbox("Visible in scene", &visibility.IsVisible)) {
        emitSet(Mutation::Component::LocalVisibility, visibility);
      }
    }
  }

  if (!world.Has<Engine::Ecs::MeshRenderer>(entity)) {
    m_MeshSourceSelectionEntity = Engine::InvalidEntityHandle;
  }
  if (const auto* storedMesh = world.Get<Engine::Ecs::MeshRenderer>(entity);
      storedMesh != nullptr &&
      DrawComponentHeader("Mesh", "Mesh", "Mesh component header", Mutation::Component::MeshRenderer, emitRemove)) {
    Engine::Ecs::MeshRenderer mesh = *storedMesh;
    const char* sourceNames[] = {"Asset model", "Primitive"};
    if (m_MeshSourceSelectionEntity != entity) {
      m_MeshSourceSelectionEntity = entity;
      m_MeshSourceSelectionIndex = mesh.PrimitiveType.empty() ? 0 : 1;
    }
    int sourceIndex = m_MeshSourceSelectionIndex;
    bool changed = false;
    bool materialChanged = false;
    if (ImGui::Combo("Mesh source", &sourceIndex, sourceNames, 2)) {
      m_MeshSourceSelectionIndex = sourceIndex;
      if (sourceIndex == 1 && mesh.PrimitiveType.empty()) {
        mesh.ModelGuid.clear();
        mesh.PrimitiveType = "cube";
        changed = true;
      }
    }
    if (sourceIndex == 0) {
      const auto selected = std::ranges::find(m_Models, mesh.ModelGuid, &Engine::AssetManager::ModelAssetInfo::guid);
      const char* preview = selected == m_Models.end() ? "Select model..." : selected->name.c_str();
      const bool modelPickerOpen = ImGui::BeginCombo("Model asset", preview);
      // The combo itself is the drop target, so models can be dropped directly
      // onto the picker without a separate placeholder button.
      if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ModelAssetDragDropType);
            payload != nullptr && payload->IsDelivery()) {
          const auto guid = ReadAssetDragDropGuid(payload->Data, payload->DataSize);
          std::string error;
          if (!guid || !AssignDroppedModel(mesh, guid.value_or(std::string{}), m_Models, error)) {
            m_AssetFeedbackSucceeded = false;
            m_AssetFeedback = guid ? error : "Dropped model payload is malformed";
          } else {
            m_MeshSourceSelectionIndex = 0;
            m_AssetFeedbackSucceeded = true;
            m_AssetFeedback = "Model assigned";
            changed = true;
          }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(TextureAssetDragDropType);
            payload != nullptr && payload->IsDelivery()) {
          m_AssetFeedbackSucceeded = false;
          m_AssetFeedback = "Textures can only be dropped on a material texture slot";
        }
        ImGui::EndDragDropTarget();
      }
      if (modelPickerOpen) {
        for (const auto& model : m_Models) {
          const bool isSelected = model.guid == mesh.ModelGuid;
          if (ImGui::Selectable(model.name.c_str(), isSelected)) {
            mesh.ModelGuid = model.guid;
            mesh.PrimitiveType.clear();
            m_MeshSourceSelectionIndex = 0;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
    } else {
      constexpr std::array<const char*, 4> primitiveNames = {"Cube", "Sphere", "Plane", "Cone"};
      constexpr std::array<const char*, 4> primitiveValues = {"cube", "sphere", "plane", "cone"};
      size_t primitiveIndex = 0;
      for (size_t index = 0; index < primitiveValues.size(); ++index) {
        if (mesh.PrimitiveType == primitiveValues[index]) {
          primitiveIndex = index;
          break;
        }
      }
      if (ImGui::BeginCombo("Primitive", primitiveNames[primitiveIndex])) {
        for (size_t index = 0; index < primitiveNames.size(); ++index) {
          const bool isSelected = primitiveIndex == index;
          if (ImGui::Selectable(primitiveNames[index], isSelected)) {
            mesh.ModelGuid.clear();
            mesh.PrimitiveType = primitiveValues[index];
            m_MeshSourceSelectionIndex = 1;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
    }
    changed |= ImGui::Checkbox("Visible", &mesh.Visible);
    changed |= ImGui::Checkbox("Cast shadows", &mesh.CastShadows);
    changed |= ImGui::Checkbox("Receive shadows", &mesh.ReceiveShadows);
    changed |= ImGui::Checkbox("Wireframe", &mesh.Wireframe);
    changed |= ImGui::Checkbox("Disable depth test", &mesh.DisableDepthTest);
    const bool legacyEmbeddedMaterial = mesh.Material.has_value() && mesh.MaterialGuid.empty();
    const auto selectedMaterial =
        std::ranges::find(m_Materials, mesh.MaterialGuid, &Engine::AssetManager::MaterialAssetInfo::guid);
    const char* materialPreview = "Default";
    if (legacyEmbeddedMaterial) {
      materialPreview = "Embedded (legacy)";
    } else if (!mesh.MaterialGuid.empty()) {
      materialPreview =
          selectedMaterial == m_Materials.end() ? "Missing material asset" : selectedMaterial->name.c_str();
    }
    if (ImGui::BeginCombo("Material", materialPreview)) {
      if (ImGui::Selectable("Default", !legacyEmbeddedMaterial && mesh.MaterialGuid.empty())) {
        UseDefaultMaterial(mesh);
        changed = true;
      }
      if (legacyEmbeddedMaterial) {
        ImGui::BeginDisabled();
        (void)ImGui::Selectable("Embedded (legacy)", true);
        ImGui::EndDisabled();
      }
      for (const auto& materialAsset : m_Materials) {
        const bool isSelected = materialAsset.guid == mesh.MaterialGuid;
        if (ImGui::Selectable(materialAsset.name.c_str(), isSelected)) {
          EmitEvent(std::make_unique<MaterialAssetSelectionRequestedEvent>(entity, materialAsset.guid));
        }
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Create Custom")) {
      EmitEvent(std::make_unique<AssetImportRequestedEvent>(AssetImportRequestedEvent::AssetType::Material, entity));
    }
    if (mesh.Material) {
      auto& material = *mesh.Material;
      ImGui::SeparatorText("Material");
      const auto drawTextureTarget = [this, &material, &changed,
                                      &materialChanged](const char* label, MaterialTextureSlot slot,
                                                        const std::optional<std::string>& currentGuid) -> void {
        std::string targetLabel = std::string(label) + ": Drop texture here";
        if (currentGuid) {
          const auto texture =
              std::ranges::find(m_Textures, *currentGuid, &Engine::AssetManager::TextureAssetInfo::guid);
          targetLabel = std::string(label) + ": " + (texture == m_Textures.end() ? *currentGuid : texture->name);
        }
        ImGui::Button(targetLabel.c_str(), ImVec2(-1.0F, 0.0F));
        if (!ImGui::BeginDragDropTarget()) {
          return;
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(TextureAssetDragDropType);
            payload != nullptr && payload->IsDelivery()) {
          const auto guid = ReadAssetDragDropGuid(payload->Data, payload->DataSize);
          std::string error;
          if (!guid || !AssignDroppedTexture(material, slot, guid.value_or(std::string{}), m_Textures, error)) {
            m_AssetFeedbackSucceeded = false;
            m_AssetFeedback = guid ? error : "Dropped texture payload is malformed";
          } else {
            changed = true;
            materialChanged = true;
            m_AssetFeedbackSucceeded = true;
            m_AssetFeedback = std::string(label) + " texture assigned";
          }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ModelAssetDragDropType);
            payload != nullptr && payload->IsDelivery()) {
          m_AssetFeedbackSucceeded = false;
          m_AssetFeedback = "Models cannot be assigned to material texture slots";
        }
        ImGui::EndDragDropTarget();
      };
      const auto drawTextureAnisotropy = [&material, &materialChanged](const char* label, size_t slot) -> void {
        constexpr std::array<uint32_t, 6> levels = {0, 1, 2, 4, 8, 16};
        constexpr std::array<const char*, 6> labels = {"Global cap", "1x", "2x", "4x", "8x", "16x"};
        const auto current = material.textureAnisotropyLevels[slot];
        const auto found = std::ranges::find(levels, current);
        int selection = found == levels.end() ? 0 : static_cast<int>(std::distance(levels.begin(), found));
        if (ImGui::Combo(label, &selection, labels.data(), static_cast<int>(labels.size()))) {
          material.textureAnisotropyLevels[slot] = levels[static_cast<size_t>(selection)];
          materialChanged = true;
        }
      };
      ImGui::SeparatorText("General Material Properties");
      materialChanged |= ImGui::ColorEdit4("Base color factor", material.baseColor.data());
      drawTextureTarget("Color texture", MaterialTextureSlot::BaseColor, material.baseColorTextureGuid);
      drawTextureAnisotropy("Color texture anisotropy", 0);
      drawTextureTarget("Normal texture", MaterialTextureSlot::Normal, material.normalTextureGuid);
      drawTextureAnisotropy("Normal texture anisotropy", 1);
      if (ImGui::TreeNode("Other texture filtering")) {
        drawTextureAnisotropy("Metallic-roughness texture anisotropy", 2);
        drawTextureAnisotropy("Occlusion texture anisotropy", 3);
        drawTextureAnisotropy("Emissive texture anisotropy", 4);
        ImGui::TreePop();
      }
      materialChanged |= ImGui::DragFloat("Normal map strength", &material.normalScale, 0.01F, 0.0F, 10.0F);
      if (ImGui::TreeNode("UV Settings")) {
        int baseColorTexCoord = static_cast<int>(material.baseColorTexCoord);
        if (ImGui::DragInt("Color UV set", &baseColorTexCoord, 0.1F, 0, 7)) {
          material.baseColorTexCoord = static_cast<uint32_t>(std::clamp(baseColorTexCoord, 0, 7));
          materialChanged = true;
        }
        materialChanged |=
            ImGui::DragFloat2("Color UV offset", material.baseColorTextureTransform.offset.data(), 0.01F);
        materialChanged |= ImGui::DragFloat2("Color UV scale", material.baseColorTextureTransform.scale.data(), 0.01F);
        materialChanged |= ImGui::DragFloat("Color UV rotation", &material.baseColorTextureTransform.rotation, 0.01F);

        int normalTexCoord = static_cast<int>(material.normalTexCoord);
        if (ImGui::DragInt("Normal UV set", &normalTexCoord, 0.1F, 0, 7)) {
          material.normalTexCoord = static_cast<uint32_t>(std::clamp(normalTexCoord, 0, 7));
          materialChanged = true;
        }
        materialChanged |= ImGui::DragFloat2("Normal UV offset", material.normalTextureTransform.offset.data(), 0.01F);
        materialChanged |= ImGui::DragFloat2("Normal UV scale", material.normalTextureTransform.scale.data(), 0.01F);
        materialChanged |= ImGui::DragFloat("Normal UV rotation", &material.normalTextureTransform.rotation, 0.01F);
        ImGui::TreePop();
      }
      materialChanged |= ImGui::SliderFloat("Metallic", &material.metallic, 0.0F, 1.0F);
      materialChanged |= ImGui::SliderFloat("Roughness", &material.roughness, 0.0F, 1.0F);
      materialChanged |= ImGui::ColorEdit3("Emissive", material.emissive.data());
      constexpr const char* workflowNames[] = {"Metallic-Roughness", "Specular-Glossiness"};
      int workflowIndex = material.workflow == "specularGlossiness" ? 1 : 0;
      if (ImGui::Combo("Workflow", &workflowIndex, workflowNames, 2)) {
        material.workflow = workflowIndex == 1 ? "specularGlossiness" : "metallicRoughness";
        materialChanged = true;
      }
      constexpr const char* specularModeNames[] = {"Index of refraction", "Explicit F0"};
      int specularModeIndex = material.specularMode == "explicitF0" ? 1 : 0;
      if (ImGui::Combo("Dielectric specular", &specularModeIndex, specularModeNames, 2)) {
        material.specularMode = specularModeIndex == 1 ? "explicitF0" : "ior";
        materialChanged = true;
      }
      if (material.specularMode == "explicitF0") {
        materialChanged |= ImGui::ColorEdit3("Specular F0", material.specularF0.data());
      } else {
        materialChanged |= ImGui::SliderFloat("Index of refraction", &material.ior, 1.0F, 3.0F, "%.2f");
      }
      ImGui::SeparatorText("MDF Parameters");
      constexpr const char* brdfNames[] = {"GGX",     "GTR",    "Beckmann", "Gauss", "Blinn-Phong", "Ashikhmin-Shirley",
                                           "Schlick", "Sawicki"};
      int brdfIndex = material.brdfModel == "gtr"                ? 1
                      : material.brdfModel == "beckmann"         ? 2
                      : material.brdfModel == "gauss"            ? 3
                      : material.brdfModel == "blinnPhong"       ? 4
                      : material.brdfModel == "ashikhminShirley" ? 5
                      : material.brdfModel == "schlick"          ? 6
                      : material.brdfModel == "sawicki"          ? 7
                                                                 : 0;
      if (ImGui::Combo("BRDF distribution", &brdfIndex, brdfNames, 8)) {
        material.brdfModel = brdfIndex == 1   ? "gtr"
                             : brdfIndex == 2 ? "beckmann"
                             : brdfIndex == 3 ? "gauss"
                             : brdfIndex == 4 ? "blinnPhong"
                             : brdfIndex == 5 ? "ashikhminShirley"
                             : brdfIndex == 6 ? "schlick"
                             : brdfIndex == 7 ? "sawicki"
                                              : "ggx";
        materialChanged = true;
      }
      if (material.brdfModel == "gtr") {
        materialChanged |= ImGui::SliderFloat("GTR gamma", &material.gtrGamma, 1.0F, 8.0F, "%.2f");
      }
      if (material.brdfModel == "ashikhminShirley") {
        materialChanged |= ImGui::SliderFloat("Roughness U", &material.anisotropicRoughnessU, 0.0f, 1.0f);
        materialChanged |= ImGui::SliderFloat("Roughness V", &material.anisotropicRoughnessV, 0.0f, 1.0f);
      }
      constexpr const char* alphaNames[] = {"Opaque", "Mask", "Blend"};
      int alphaIndex = material.alphaMode == "mask" ? 1 : material.alphaMode == "blend" ? 2 : 0;
      if (ImGui::Combo("Alpha mode", &alphaIndex, alphaNames, 3)) {
        material.alphaMode = alphaIndex == 1 ? "mask" : alphaIndex == 2 ? "blend" : "opaque";
        materialChanged = true;
      }
      if (material.alphaMode == "mask") {
        materialChanged |= ImGui::SliderFloat("Alpha cutoff", &material.alphaCutoff, 0.0F, 1.0F);
      }
      materialChanged |= ImGui::Checkbox("Double sided", &material.doubleSided);
      changed |= materialChanged;
    }
    if (materialChanged && !legacyEmbeddedMaterial && !mesh.MaterialGuid.empty() && mesh.Material.has_value()) {
      EmitEvent(std::make_unique<MaterialAssetEditRequestedEvent>(mesh.MaterialGuid, *mesh.Material));
    }
    if (changed) {
      emitSet(Mutation::Component::MeshRenderer, std::move(mesh));
    }
  }

  if (const auto* storedLight = world.Get<Engine::Ecs::Light>(entity);
      storedLight != nullptr &&
      DrawComponentHeader("Light", "Light", "Light component header", Mutation::Component::SpotLight, emitRemove)) {
    Engine::Ecs::Light light = *storedLight;
    constexpr const char* lightTypeNames[] = {"Directional", "Point", "Spot"};
    int typeIndex = static_cast<int>(light.Type);
    bool changed = ImGui::Combo("Type", &typeIndex, lightTypeNames, 3);
    if (changed) {
      light.Type = static_cast<Engine::Ecs::LightType>(typeIndex);
    }
    if (light.Type != Engine::Ecs::LightType::Point) {
      changed |= ImGui::DragFloat3("Direction", light.Direction.data(), 0.01F, -1.0F, 1.0F);
    }
    changed |= ImGui::ColorEdit3("Light color", light.Color.data());
    changed |= ImGui::DragFloat("Intensity", &light.Intensity, 0.05F, 0.0F, 100000.0F);
    if (light.Type != Engine::Ecs::LightType::Directional) {
      changed |= ImGui::DragFloat("Range", &light.Range, 0.05F, 0.01F, 100000.0F);
    }
    if (light.Type == Engine::Ecs::LightType::Spot) {
      changed |= ImGui::DragFloat("Inner cone", &light.InnerAngleDegrees, 0.1F, 0.0F, 88.9F);
      changed |= ImGui::DragFloat("Outer cone", &light.OuterAngleDegrees, 0.1F, light.InnerAngleDegrees + 0.1F, 89.9F);
    }
    changed |= ImGui::DragFloat("Shadow bias", &light.ShadowBias, 0.0001F, 0.0F, 0.1F);
    changed |= ImGui::DragFloat("Shadow normal bias", &light.ShadowNormalBias, 0.0001F, 0.0F, 0.1F);
    changed |= ImGui::Checkbox("Cast shadows", &light.CastShadows);
    if (changed) {
      emitSet(Mutation::Component::SpotLight, light);
    }
  }

  if (const auto* storedCamera = world.Get<Engine::Ecs::Camera>(entity);
      storedCamera != nullptr &&
      DrawComponentHeader("Camera", "Camera", "Camera component header", Mutation::Component::Camera, emitRemove)) {
    Engine::Ecs::Camera camera = *storedCamera;
    bool changed = ImGui::SliderFloat("Field of view", &camera.FieldOfViewDegrees, 10.0F, 150.0F, "%.1f deg");
    changed |= ImGui::DragFloat("Near plane", &camera.NearPlane, 0.01F, 0.001F, 100.0F);
    changed |= ImGui::DragFloat("Far plane", &camera.FarPlane, 0.1F, camera.NearPlane + 0.01F, 100000.0F);
    constexpr std::array<const char*, 5> resolutionOptions = {"720p (1280 x 720)", "1080p (1920 x 1080)",
                                                              "1440p (2560 x 1440)", "4K UHD (3840 x 2160)", "Custom"};
    int resolutionIndex = 4;
    if (camera.OutputWidth == 1280 && camera.OutputHeight == 720) {
      resolutionIndex = 0;
    } else if (camera.OutputWidth == 1920 && camera.OutputHeight == 1080) {
      resolutionIndex = 1;
    } else if (camera.OutputWidth == 2560 && camera.OutputHeight == 1440) {
      resolutionIndex = 2;
    } else if (camera.OutputWidth == 3840 && camera.OutputHeight == 2160) {
      resolutionIndex = 3;
    } else if (m_CustomResolutionEntity == entity || (camera.OutputWidth != 0 && camera.OutputHeight != 0)) {
      resolutionIndex = 4;
    }
    if (ImGui::Combo("Output resolution", &resolutionIndex, resolutionOptions.data(),
                     static_cast<int>(resolutionOptions.size()))) {
      switch (resolutionIndex) {
      case 0:
        m_CustomResolutionEntity = Engine::InvalidEntityHandle;
        camera.OutputWidth = 1280;
        camera.OutputHeight = 720;
        changed = true;
        break;
      case 1:
        m_CustomResolutionEntity = Engine::InvalidEntityHandle;
        camera.OutputWidth = 1920;
        camera.OutputHeight = 1080;
        changed = true;
        break;
      case 2:
        m_CustomResolutionEntity = Engine::InvalidEntityHandle;
        camera.OutputWidth = 2560;
        camera.OutputHeight = 1440;
        changed = true;
        break;
      case 3:
        m_CustomResolutionEntity = Engine::InvalidEntityHandle;
        camera.OutputWidth = 3840;
        camera.OutputHeight = 2160;
        changed = true;
        break;
      default:
        m_CustomResolutionEntity = entity;
        break;
      }
    }
    if (resolutionIndex == 4) {
      int outputWidth =
          static_cast<int>((std::min)(camera.OutputWidth, static_cast<uint32_t>(std::numeric_limits<int>::max())));
      int outputHeight =
          static_cast<int>((std::min)(camera.OutputHeight, static_cast<uint32_t>(std::numeric_limits<int>::max())));
      const bool widthChanged = ImGui::InputInt("Output width", &outputWidth);
      const bool heightChanged = ImGui::InputInt("Output height", &outputHeight);
      if (widthChanged && outputWidth > 0) {
        camera.OutputWidth = static_cast<uint32_t>(outputWidth);
        changed = true;
      }
      if (heightChanged && outputHeight > 0) {
        camera.OutputHeight = static_cast<uint32_t>(outputHeight);
        changed = true;
      }
    }
    changed |= ImGui::InputInt("Priority", &camera.Priority);
    changed |= ImGui::Checkbox("Active camera", &camera.Active);
    if (changed) {
      emitSet(Mutation::Component::Camera, camera);
    }
  }

  if (world.Has<Engine::Ecs::Camera>(entity) && !world.Has<Engine::Ecs::CameraPostProcessing>(entity)) {
    ImGui::TextWrapped("This camera has no post-processing effects.");
    if (ImGui::Button("Add Post Processing")) {
      emitAdd(Mutation::Component::CameraPostProcessing);
    }
  }

  if (const auto* storedPostProcessing = world.Get<Engine::Ecs::CameraPostProcessing>(entity);
      storedPostProcessing != nullptr &&
      DrawComponentHeader("Post Processing", "Post Processing", "Camera post-processing component header",
                          Mutation::Component::CameraPostProcessing, emitRemove)) {
    Engine::Ecs::CameraPostProcessing postProcessing = *storedPostProcessing;
    bool changed = false;
    if (postProcessing.Effects.empty() && !postProcessing.AmbientOcclusion) {
      ImGui::TextWrapped("No effects are active. The camera renders without post-processing.");
    }
    bool ambientOcclusionEnabled = postProcessing.AmbientOcclusion.has_value();
    if (ImGui::Checkbox("Screen-space ambient occlusion", &ambientOcclusionEnabled)) {
      if (ambientOcclusionEnabled) {
        postProcessing.AmbientOcclusion.emplace();
      } else {
        postProcessing.AmbientOcclusion.reset();
      }
      changed = true;
    }
    if (postProcessing.AmbientOcclusion) {
      ImGui::Indent();
      changed |=
          ImGui::SliderFloat("AO Radius", &postProcessing.AmbientOcclusion->Radius, 0.0F, 5.0F, "%.2f world units");
      changed |= ImGui::SliderFloat("AO Intensity", &postProcessing.AmbientOcclusion->Intensity, 0.0F, 4.0F, "%.2f");
      ImGui::Unindent();
    }
    for (size_t index = 0; index < postProcessing.Effects.size(); ++index) {
      ImGui::PushID(static_cast<int>(index));
      Engine::Ecs::CameraPostProcessEffect& effect = postProcessing.Effects[index];
      const char* effectName =
          std::holds_alternative<Engine::Ecs::ColorGradingEffect>(effect) ? "Color Grading" : "Bloom";
      ImGui::Text("%zu. %s", index + 1U, effectName);
      ImGui::SameLine();
      if (ImGui::SmallButton("Up") && index > 0) {
        std::swap(postProcessing.Effects[index], postProcessing.Effects[index - 1U]);
        changed = true;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Down") && index + 1U < postProcessing.Effects.size()) {
        std::swap(postProcessing.Effects[index], postProcessing.Effects[index + 1U]);
        changed = true;
      }
      if (auto* grading = std::get_if<Engine::Ecs::ColorGradingEffect>(&effect)) {
        changed |= ImGui::SliderFloat("Exposure", &grading->Exposure, -16.0F, 16.0F, "%.2f stops");
        changed |= ImGui::SliderFloat("Contrast", &grading->Contrast, 0.0F, 4.0F, "%.2f");
        changed |= ImGui::SliderFloat("Saturation", &grading->Saturation, 0.0F, 4.0F, "%.2f");
      } else if (auto* bloom = std::get_if<Engine::Ecs::BloomEffect>(&effect)) {
        changed |= ImGui::DragFloat("Threshold", &bloom->Threshold, 0.05F, 0.0F, 100000.0F, "%.2f");
        changed |= ImGui::SliderFloat("Intensity", &bloom->Intensity, 0.0F, 10.0F, "%.2f");
        changed |= ImGui::SliderFloat("Radius", &bloom->Radius, 0.0F, 64.0F, "%.1f px");
      }
      if (ImGui::SmallButton("Remove effect")) {
        postProcessing.Effects.erase(postProcessing.Effects.begin() + static_cast<std::ptrdiff_t>(index));
        changed = true;
        ImGui::PopID();
        break;
      }
      ImGui::Separator();
      ImGui::PopID();
    }
    static constexpr const char* effectTypes[] = {"Color Grading", "Bloom"};
    static int newEffectType = 0;
    ImGui::SetNextItemWidth(150.0F);
    ImGui::Combo("New effect", &newEffectType, effectTypes, 2);
    ImGui::SameLine();
    if (ImGui::Button("Add effect")) {
      if (newEffectType == 0) {
        postProcessing.Effects.emplace_back(Engine::Ecs::ColorGradingEffect{});
      } else {
        postProcessing.Effects.emplace_back(Engine::Ecs::BloomEffect{});
      }
      changed = true;
    }
    if (changed) {
      emitSet(Mutation::Component::CameraPostProcessing, std::move(postProcessing));
    }
  }

  if (!isSceneRoot && ImGui::Button("Add Component")) {
    m_AddComponentFilter.fill('\0');
    ImGui::OpenPopup("Add component popup");
  }
  if (!isSceneRoot && ImGui::BeginPopup("Add component popup")) {
    (void)ImGui::InputTextWithHint("##add-filter", "Search components...", m_AddComponentFilter.data(),
                                   m_AddComponentFilter.size());
    if (!world.Has<Engine::Ecs::MeshRenderer>(entity) && Matches("Mesh", m_AddComponentFilter.data()) &&
        ImGui::Selectable("Mesh")) {
      emitAdd(Mutation::Component::MeshRenderer);
    }
    if (!world.Has<Engine::Ecs::SpotLight>(entity) && Matches("Light", m_AddComponentFilter.data()) &&
        ImGui::Selectable("Light")) {
      emitAdd(Mutation::Component::SpotLight);
    }
    if (!world.Has<Engine::Ecs::Camera>(entity) && Matches("Camera", m_AddComponentFilter.data()) &&
        ImGui::Selectable("Camera")) {
      emitAdd(Mutation::Component::Camera);
    }
    if (world.Has<Engine::Ecs::Camera>(entity) && !world.Has<Engine::Ecs::CameraPostProcessing>(entity) &&
        Matches("Post Processing", m_AddComponentFilter.data()) && ImGui::Selectable("Post Processing")) {
      emitAdd(Mutation::Component::CameraPostProcessing);
    }
    ImGui::EndPopup();
  }

  std::string signature = "{";
  const auto append = [&signature](const char* component) -> void {
    if (signature.size() > 1) {
      signature += ", ";
    }
    signature += component;
  };
  append("PersistentIdentity");
  if (world.Has<Engine::Ecs::Name>(entity)) {
    append("Name");
  }
  if (world.Has<Engine::Ecs::LocalTransform>(entity)) {
    append("Transform");
  }
  if (world.Has<Engine::Ecs::Parent>(entity) && world.Has<Engine::Ecs::ChildList>(entity)) {
    append("Parent / ChildList");
  }
  if (world.Has<Engine::Ecs::MeshRenderer>(entity)) {
    append("MeshRenderer");
  }
  if (world.Has<Engine::Ecs::SpotLight>(entity)) {
    append("SpotLight");
  }
  if (world.Has<Engine::Ecs::Camera>(entity)) {
    append("Camera");
  }
  if (world.Has<Engine::Ecs::CameraPostProcessing>(entity)) {
    append("CameraPostProcessing");
  }
  signature += '}';
  ImGui::Separator();
  ImGui::BeginDisabled();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  ImGui::TextWrapped("Archetype: %s", signature.c_str());
  ImGui::EndDisabled();
}
