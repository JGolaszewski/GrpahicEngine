#include "panels/include/InspectorPanel.h"

#include <DirectXMath.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <imgui.h>
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

auto QuaternionToPitchYawRoll(const std::array<float, 4>& quaternion) -> std::array<float, 3> {
  using namespace DirectX;
  const XMVECTOR rotation =
      XMQuaternionNormalize(XMVectorSet(quaternion[0], quaternion[1], quaternion[2], quaternion[3]));
  const XMVECTOR forward = XMVector3Rotate(XMVectorSet(0.0F, 0.0F, 1.0F, 0.0F), rotation);
  const float pitch = std::asin(std::clamp(-XMVectorGetY(forward), -1.0F, 1.0F));
  const float yaw = std::atan2(XMVectorGetX(forward), XMVectorGetZ(forward));
  const XMVECTOR noRollRotation = XMQuaternionRotationRollPitchYaw(pitch, yaw, 0.0F);
  const XMVECTOR baseUp = XMVector3Rotate(XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), noRollRotation);
  const XMVECTOR baseRight = XMVector3Rotate(XMVectorSet(1.0F, 0.0F, 0.0F, 0.0F), noRollRotation);
  const XMVECTOR actualUp = XMVector3Rotate(XMVectorSet(0.0F, 1.0F, 0.0F, 0.0F), rotation);
  const float roll =
      std::atan2(XMVectorGetX(XMVector3Dot(actualUp, baseRight)), XMVectorGetX(XMVector3Dot(actualUp, baseUp)));
  return {pitch, yaw, roll};
}

void SetQuaternionFromPitchYawRoll(std::array<float, 4>& quaternion, const std::array<float, 3>& angles) {
  DirectX::XMFLOAT4 result{};
  DirectX::XMStoreFloat4(&result, DirectX::XMQuaternionRotationRollPitchYaw(angles[0], angles[1], angles[2]));
  quaternion = {result.x, result.y, result.z, result.w};
}

void DrawRemoveButton(const char* id, Mutation::Component component,
                      const std::function<void(Mutation::Component)>& emitRemove) {
  if (ImGui::Button(id, ImVec2(-1.0F, 0.0F))) {
    emitRemove(component);
  }
  if (ImGui::IsItemHovered()) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    ImGui::SetTooltip("Remove component");
  }
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
}

void InspectorPanel::Draw() {
  ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse);
  if (m_Scene && m_SelectedEntity != Engine::InvalidEntityHandle && m_Scene->World().IsAlive(m_SelectedEntity)) {
    DrawEntityInspector();
  } else {
    ImGui::TextUnformatted("Select an entity from the scene hierarchy.");
  }

#ifdef _DEBUG
  ImGui::Spacing();
  if (ImGui::CollapsingHeader("Debug Render")) {
    if (ImGui::Checkbox("Show light gizmos", &m_ShowDebugLightGizmos)) {
      EmitEvent(std::make_unique<DebugRenderToggledEvent>(DebugRenderToggledEvent::Toggle::LightGizmos,
                                                          m_ShowDebugLightGizmos));
    }
    if (ImGui::Checkbox("Show wireframe plane", &m_ShowDebugWireframePlane)) {
      EmitEvent(std::make_unique<DebugRenderToggledEvent>(DebugRenderToggledEvent::Toggle::WireframePlane,
                                                          m_ShowDebugWireframePlane));
    }
  }
#endif
  ImGui::End();
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
void InspectorPanel::DrawEntityInspector() {
  const Engine::ArchetypeWorld& world = m_Scene->World();
  const Engine::EntityHandle entity = m_SelectedEntity;
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
  if (const auto* storedName = world.Get<Engine::Ecs::Name>(entity);
      storedName != nullptr && ImGui::CollapsingHeader("Name / Tag", ImGuiTreeNodeFlags_DefaultOpen)) {
    Engine::Ecs::Name name = *storedName;
    std::array<char, 256> buffer{};
    const size_t length = (std::min)(name.Value.size(), buffer.size() - 1);
    std::copy_n(name.Value.data(), length, buffer.data());
    if (ImGui::InputText("Name", buffer.data(), buffer.size())) {
      name.Value = buffer.data();
      emitSet(Mutation::Component::Name, std::move(name));
    }
  }

  if (const auto* storedTransform = world.Get<Engine::Ecs::LocalTransform>(entity);
      storedTransform != nullptr && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    Engine::Ecs::LocalTransform transform = *storedTransform;
    bool changed = ImGui::DragFloat3("Local position", transform.Position.data(), 0.05F);
    std::array<float, 3> pitchYawRoll = QuaternionToPitchYawRoll(transform.Rotation);
    std::array<float, 3> anglesDegrees = {
        DirectX::XMConvertToDegrees(pitchYawRoll[0]),
        DirectX::XMConvertToDegrees(pitchYawRoll[1]),
        DirectX::XMConvertToDegrees(pitchYawRoll[2]),
    };
    if (ImGui::DragFloat3("Pitch / Yaw / Roll", anglesDegrees.data(), 0.25F, -360.0F, 360.0F, "%.1f deg")) {
      pitchYawRoll = {
          DirectX::XMConvertToRadians(anglesDegrees[0]),
          DirectX::XMConvertToRadians(anglesDegrees[1]),
          DirectX::XMConvertToRadians(anglesDegrees[2]),
      };
      SetQuaternionFromPitchYawRoll(transform.Rotation, pitchYawRoll);
      changed = true;
    }
    if (ImGui::TreeNode("Quaternion (advanced)")) {
      changed |= ImGui::DragFloat4("Raw quaternion", transform.Rotation.data(), 0.01F);
      ImGui::TreePop();
    }
    changed |= ImGui::DragFloat3("Local scale", transform.Scale.data(), 0.05F, 0.01F, 1000.0F);
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

  if (const auto* storedMesh = world.Get<Engine::Ecs::MeshRenderer>(entity);
      storedMesh != nullptr && ImGui::CollapsingHeader("Mesh Renderer")) {
    DrawRemoveButton("Remove Mesh Renderer", Mutation::Component::MeshRenderer, emitRemove);
    Engine::Ecs::MeshRenderer mesh = *storedMesh;
    std::array<char, 256> modelGuid{};
    const size_t length = (std::min)(mesh.ModelGuid.size(), modelGuid.size() - 1);
    std::copy_n(mesh.ModelGuid.data(), length, modelGuid.data());
    bool changed = ImGui::InputText("Model asset GUID", modelGuid.data(), modelGuid.size());
    mesh.ModelGuid = modelGuid.data();
    std::array<char, 128> primitiveType{};
    const size_t primitiveLength = (std::min)(mesh.PrimitiveType.size(), primitiveType.size() - 1);
    std::copy_n(mesh.PrimitiveType.data(), primitiveLength, primitiveType.data());
    changed |= ImGui::InputText("Primitive type", primitiveType.data(), primitiveType.size());
    mesh.PrimitiveType = primitiveType.data();
    changed |= ImGui::Checkbox("Visible", &mesh.Visible);
    changed |= ImGui::Checkbox("Cast shadows", &mesh.CastShadows);
    changed |= ImGui::Checkbox("Receive shadows", &mesh.ReceiveShadows);
    changed |= ImGui::Checkbox("Wireframe", &mesh.Wireframe);
    changed |= ImGui::Checkbox("Disable depth test", &mesh.DisableDepthTest);
    if (changed) {
      emitSet(Mutation::Component::MeshRenderer, std::move(mesh));
    }
  }

  if (const auto* storedLight = world.Get<Engine::Ecs::SpotLight>(entity);
      storedLight != nullptr && ImGui::CollapsingHeader("Spot Light")) {
    DrawRemoveButton("Remove Spot Light Component", Mutation::Component::SpotLight, emitRemove);
    Engine::Ecs::SpotLight light = *storedLight;
    bool changed = ImGui::DragFloat3("Direction", light.Direction.data(), 0.01F, -1.0F, 1.0F);
    changed |= ImGui::ColorEdit3("Light color", light.Color.data());
    changed |= ImGui::DragFloat("Intensity", &light.Intensity, 0.05F, 0.0F, 100000.0F);
    changed |= ImGui::DragFloat("Range", &light.Range, 0.05F, 0.01F, 100000.0F);
    changed |= ImGui::DragFloat("Inner cone", &light.InnerAngleDegrees, 0.1F, 0.0F, 89.0F);
    changed |= ImGui::DragFloat("Outer cone", &light.OuterAngleDegrees, 0.1F, 0.1F, 89.9F);
    changed |= ImGui::DragFloat("Shadow bias", &light.ShadowBias, 0.0001F, 0.0F, 0.1F);
    changed |= ImGui::DragFloat("Shadow normal bias", &light.ShadowNormalBias, 0.0001F, 0.0F, 0.1F);
    changed |= ImGui::Checkbox("Use position", &light.UsePosition);
    changed |= ImGui::Checkbox("Cast shadows", &light.CastShadows);
    if (changed) {
      emitSet(Mutation::Component::SpotLight, light);
    }
  }

  if (const auto* storedCamera = world.Get<Engine::Ecs::Camera>(entity);
      storedCamera != nullptr && ImGui::CollapsingHeader("Camera")) {
    DrawRemoveButton("Remove Camera", Mutation::Component::Camera, emitRemove);
    Engine::Ecs::Camera camera = *storedCamera;
    bool changed = ImGui::SliderFloat("Field of view", &camera.FieldOfViewDegrees, 10.0F, 150.0F, "%.1f deg");
    changed |= ImGui::DragFloat("Near plane", &camera.NearPlane, 0.01F, 0.001F, 100.0F);
    changed |= ImGui::DragFloat("Far plane", &camera.FarPlane, 0.1F, camera.NearPlane + 0.01F, 100000.0F);
    changed |= ImGui::InputInt("Priority", &camera.Priority);
    changed |= ImGui::Checkbox("Active camera", &camera.Active);
    if (changed) {
      emitSet(Mutation::Component::Camera, camera);
    }
  }

  if (ImGui::Button("Add Component")) {
    m_AddComponentFilter.fill('\0');
    ImGui::OpenPopup("Add component popup");
  }
  if (ImGui::BeginPopup("Add component popup")) {
    (void)ImGui::InputTextWithHint("##add-filter", "Search components...", m_AddComponentFilter.data(),
                                   m_AddComponentFilter.size());
    if (!world.Has<Engine::Ecs::MeshRenderer>(entity) && Matches("Mesh Renderer", m_AddComponentFilter.data()) &&
        ImGui::Selectable("Mesh Renderer")) {
      emitAdd(Mutation::Component::MeshRenderer);
    }
    if (!world.Has<Engine::Ecs::SpotLight>(entity) && Matches("Spot Light", m_AddComponentFilter.data()) &&
        ImGui::Selectable("Spot Light")) {
      emitAdd(Mutation::Component::SpotLight);
    }
    if (!world.Has<Engine::Ecs::Camera>(entity) && Matches("Camera", m_AddComponentFilter.data()) &&
        ImGui::Selectable("Camera")) {
      emitAdd(Mutation::Component::Camera);
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
  signature += '}';
  ImGui::Separator();
  ImGui::BeginDisabled();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  ImGui::TextWrapped("Archetype: %s", signature.c_str());
  ImGui::EndDisabled();
}
