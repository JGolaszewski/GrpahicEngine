#include "panels/include/RenderPanel.h"

#include "platform/include/EditorFileDialogs.h"
#include "rendering/include/RenderOutputExporter.h"
#include "scene/include/components/EcsComponents.h"

#include <DirectXMath.h>
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <string>

RenderPanel::RenderPanel(RenderFunction render, PreviewFunction setPreview, std::function<void*()> getPreviewTexture)
    : m_Render(std::move(render)), m_SetPreview(std::move(setPreview)),
      m_GetPreviewTexture(std::move(getPreviewTexture)) {}

void RenderPanel::SetScene(std::shared_ptr<Engine::Scene> scene) {
  if (m_Scene != scene) {
    m_Scene = std::move(scene);
    m_SelectedCamera = Engine::InvalidEntityHandle;
    m_LastOutput.reset();
    m_Status = "Scene changed. Select a scene camera.";
  }
}

auto RenderPanel::GetCameras() const -> std::vector<CameraChoice> {
  std::vector<CameraChoice> cameras;
  if (!m_Scene) {
    return cameras;
  }
  m_Scene->World()
      .ForEachChunkReadOnly<Engine::Ecs::Name, Engine::Ecs::Camera, Engine::Ecs::WorldTransform,
                            Engine::Ecs::GlobalVisibility>(
          [&cameras](std::span<const Engine::EntityHandle> entities, std::span<const Engine::Ecs::Name> names,
                     std::span<const Engine::Ecs::Camera> cameraSettings,
                     std::span<const Engine::Ecs::WorldTransform> transforms,
                     std::span<const Engine::Ecs::GlobalVisibility> visibility) -> void {
            for (size_t index = 0; index < entities.size(); ++index) {
              if (!visibility[index].IsVisible) {
                continue;
              }
              DirectX::XMVECTOR scale{};
              DirectX::XMVECTOR rotation{};
              DirectX::XMVECTOR position{};
              const DirectX::XMMATRIX matrix = DirectX::XMLoadFloat4x4(&transforms[index].Matrix);
              if (!DirectX::XMMatrixDecompose(&scale, &rotation, &position, matrix)) {
                continue;
              }
              DirectX::XMFLOAT4 quaternion{};
              DirectX::XMFLOAT3 translation{};
              DirectX::XMStoreFloat4(&quaternion, rotation);
              DirectX::XMStoreFloat3(&translation, position);
              const Engine::Ecs::Camera& camera = cameraSettings[index];
              CameraChoice choice;
              choice.Entity = entities[index];
              choice.Label = names[index].Value + " (" + std::to_string(choice.Entity.Index) + ")";
              choice.View.Position = {translation.x, translation.y, translation.z};
              choice.View.Rotation = {quaternion.x, quaternion.y, quaternion.z, quaternion.w};
              choice.View.FieldOfViewDegrees = camera.FieldOfViewDegrees;
              choice.View.NearPlane = camera.NearPlane;
              choice.View.FarPlane = camera.FarPlane;
              choice.Width = camera.OutputWidth;
              choice.Height = camera.OutputHeight;
              cameras.push_back(std::move(choice));
            }
          });
  std::ranges::sort(cameras, {}, &CameraChoice::Label);
  return cameras;
}

auto RenderPanel::FindSelected(const std::vector<CameraChoice>& cameras) const -> const CameraChoice* {
  const auto found = std::ranges::find(cameras, m_SelectedCamera, &CameraChoice::Entity);
  return found == cameras.end() ? nullptr : &*found;
}

void RenderPanel::Draw() {
  const std::vector<CameraChoice> cameras = GetCameras();
  if (ImGui::Begin("Render", &m_Open)) {
    if (cameras.empty()) {
      ImGui::TextDisabled("This scene has no visible camera with a valid world transform.");
    } else {
      if (FindSelected(cameras) == nullptr) {
        m_SelectedCamera = cameras.front().Entity;
      }
      int currentIndex = 0;
      for (size_t index = 0; index < cameras.size(); ++index) {
        if (cameras[index].Entity == m_SelectedCamera) {
          currentIndex = static_cast<int>(index);
          break;
        }
      }
      if (ImGui::BeginCombo("Scene camera", cameras[static_cast<size_t>(currentIndex)].Label.c_str())) {
        for (size_t index = 0; index < cameras.size(); ++index) {
          const bool selected = static_cast<int>(index) == currentIndex;
          if (ImGui::Selectable(cameras[index].Label.c_str(), selected)) {
            m_SelectedCamera = cameras[index].Entity;
            m_LastOutput.reset();
            m_Status = "Camera changed. Render a preview to refresh the output.";
            currentIndex = static_cast<int>(index);
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      const CameraChoice& selected = cameras[static_cast<size_t>(currentIndex)];
      ImGui::Text("Output: %u x %u", selected.Width, selected.Height);
      if (selected.Width == 0 || selected.Height == 0) {
        ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.25F, 1.0F), "Camera output resolution must be positive.");
      }
      const bool canRender = m_Render && selected.Width > 0 && selected.Height > 0;
      if (!canRender) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Render Preview") && canRender) {
        (void)RenderSelected(selected);
      }
      ImGui::SameLine();
      if (ImGui::Button("Save PNG...") && canRender) {
        SavePng(selected);
      }
      ImGui::SameLine();
      if (ImGui::Button("Save EXR...") && canRender) {
        SaveExr(selected);
      }
      if (!canRender) {
        ImGui::EndDisabled();
      }

      ImGui::SeparatorText("EXR AOVs");
      constexpr std::array<Engine::RenderAov, 5> aovs = {
          Engine::RenderAov::FinalColor, Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal,
          Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting};
      for (size_t index = 0; index < aovs.size(); ++index) {
        const auto& descriptor = Engine::RenderOutput::GetDescriptor(aovs[index]);
        ImGui::Checkbox(descriptor.Name, &m_SelectedAovs[index]);
        if ((index % 3U) != 2U && index + 1U < aovs.size()) {
          ImGui::SameLine();
        }
      }

      ImGui::SeparatorText("Preview");
      const ImVec2 available = ImGui::GetContentRegionAvail();
      const float aspect = static_cast<float>(selected.Width) / static_cast<float>(selected.Height);
      ImVec2 previewSize = available;
      if (previewSize.x > 0.0F && previewSize.y > 0.0F) {
        if (previewSize.x / previewSize.y > aspect) {
          previewSize.x = previewSize.y * aspect;
        } else {
          previewSize.y = previewSize.x / aspect;
        }
        void* texture = m_GetPreviewTexture ? m_GetPreviewTexture() : nullptr;
        if (texture != nullptr && m_LastOutput) {
          ImGui::Image(texture, previewSize);
        } else {
          ImGui::TextDisabled("No preview rendered yet.");
        }
      }
    }
    ImGui::Separator();
    ImGui::TextWrapped("%s", m_Status.c_str());
  }
  ImGui::End();
}

bool RenderPanel::RenderSelected(const CameraChoice& camera) {
  if (!m_Render) {
    m_Status = "Final renderer is not available.";
    return false;
  }
  auto result = m_Render(camera.View, camera.Width, camera.Height);
  if (!result) {
    m_LastOutput.reset();
    m_Status = "Render failed: " + result.error().Message;
    return false;
  }
  auto output = std::make_unique<Engine::RenderOutput>(std::move(*result));
  const bool previewUploaded = UpdatePreviewTexture(*output);
  m_LastOutput = std::move(output);
  m_Status = previewUploaded ? "Preview rendered successfully."
                             : "Render completed, but the editor could not upload its preview texture.";
  return true;
}

bool RenderPanel::UpdatePreviewTexture(const Engine::RenderOutput& output) {
  const auto pixels = output.GetPixels(Engine::RenderAov::FinalColor);
  const size_t expected = static_cast<size_t>(output.Width) * output.Height * 4U;
  if (pixels.size() != expected || !m_SetPreview) {
    return false;
  }
  std::vector<uint8_t> rgba(expected);
  const auto encodeChannel = [](float sample) -> uint8_t {
    const float safe = std::isfinite(sample) ? (std::max)(sample, 0.0F) : 0.0F;
    const float mapped = safe / (1.0F + safe);
    const float srgb = mapped <= 0.0031308F ? mapped * 12.92F : 1.055F * std::pow(mapped, 1.0F / 2.4F) - 0.055F;
    return static_cast<uint8_t>(std::lround(std::clamp(srgb, 0.0F, 1.0F) * 255.0F));
  };
  for (size_t index = 0; index < expected; index += 4U) {
    rgba[index] = encodeChannel(pixels[index]);
    rgba[index + 1U] = encodeChannel(pixels[index + 1U]);
    rgba[index + 2U] = encodeChannel(pixels[index + 2U]);
    const float alpha = std::isfinite(pixels[index + 3U]) ? std::clamp(pixels[index + 3U], 0.0F, 1.0F) : 0.0F;
    rgba[index + 3U] = static_cast<uint8_t>(std::lround(alpha * 255.0F));
  }
  return m_SetPreview(output.Width, output.Height, rgba);
}

void RenderPanel::SavePng(const CameraChoice& camera) {
  const auto path = EditorPlatform::ChooseRenderOutputFile(false, std::filesystem::current_path() / "render.png");
  if (!path) {
    m_Status = "PNG export cancelled.";
    return;
  }
  if (!RenderSelected(camera)) {
    return;
  }
  const Engine::Status status = Engine::RenderOutputExporter::WritePng(*m_LastOutput, *path);
  m_Status = status ? "PNG saved to " + path->string() : "PNG export failed: " + status.error().Message;
}

void RenderPanel::SaveExr(const CameraChoice& camera) {
  std::vector<Engine::RenderAov> selected;
  constexpr std::array<Engine::RenderAov, 5> aovs = {
      Engine::RenderAov::FinalColor, Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal,
      Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting};
  for (size_t index = 0; index < aovs.size(); ++index) {
    if (m_SelectedAovs[index]) {
      selected.push_back(aovs[index]);
    }
  }
  if (selected.empty()) {
    m_Status = "Select at least one AOV before saving EXR.";
    return;
  }
  const auto path = EditorPlatform::ChooseRenderOutputFile(true, std::filesystem::current_path() / "render.exr");
  if (!path) {
    m_Status = "EXR export cancelled.";
    return;
  }
  if (!RenderSelected(camera)) {
    return;
  }
  const Engine::Status status = Engine::RenderOutputExporter::WriteMultilayerExr(*m_LastOutput, selected, *path);
  m_Status = status ? "EXR saved to " + path->string() : "EXR export failed: " + status.error().Message;
}
