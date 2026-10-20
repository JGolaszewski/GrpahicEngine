#include "platform/include/EditorUiIntegration.h"

#include "backends/D3D11Backend.h"
#include "rendering/include/ViewportTarget.h"
#include "runtime/include/Window.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_glfw.h>
#include <nvrhi/nvrhi.h>
#include <wrl/client.h>

struct EditorUiIntegration::PreviewTexture {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> View;
};

EditorUiIntegration::EditorUiIntegration() = default;
EditorUiIntegration::~EditorUiIntegration() = default;

auto EditorUiIntegration::Initialize(Engine::IWindow* window, Engine::IRenderBackend* backend) -> bool {
  auto* d3d11Backend = dynamic_cast<Engine::D3D11Backend*>(backend);
  if ((window == nullptr) || (window->GetEventSourceHandle() == nullptr) || (d3d11Backend == nullptr) ||
      (d3d11Backend->GetNativeDevice() == nullptr) || (d3d11Backend->GetNativeContext() == nullptr)) {
    return false;
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

  auto* glfwWindow = static_cast<GLFWwindow*>(window->GetEventSourceHandle());
  if (!ImGui_ImplGlfw_InitForOther(glfwWindow, true)) {
    ImGui::DestroyContext();
    return false;
  }
  if (!ImGui_ImplDX11_Init(d3d11Backend->GetNativeDevice(), d3d11Backend->GetNativeContext())) {
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    return false;
  }

  float xScale = 1.0F;
  float yScale = 1.0F;
  glfwGetWindowContentScale(glfwWindow, &xScale, &yScale);
  const float uiScale = (std::max)(xScale, yScale);
  if (uiScale > 1.0F) {
    ImGui::GetStyle().ScaleAllSizes(uiScale);
    io.FontGlobalScale = uiScale;
  }

  m_Backend = d3d11Backend;
  m_Initialized = true;
  return true;
}

void EditorUiIntegration::BeginFrame() const {
  if (!m_Initialized) {
    return;
  }
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void EditorUiIntegration::EndFrame() {
  if (!m_Initialized || (m_Backend == nullptr)) {
    return;
  }
  ImGui::Render();
  ID3D11RenderTargetView* renderTarget = m_Backend->GetBackBufferRenderTarget();
  if (renderTarget == nullptr) {
    return;
  }
  ID3D11DeviceContext* context = m_Backend->GetNativeContext();
  context->OMSetRenderTargets(1, &renderTarget, nullptr);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  if (m_ClearCameraPreviewAfterDraw) {
    m_CameraPreviewTexture.reset();
    m_ClearCameraPreviewAfterDraw = false;
  }
  if (m_ClearAnalyticalFrameAfterDraw) {
    m_AnalyticalFrameTexture.reset();
    m_ClearAnalyticalFrameAfterDraw = false;
  }
}

void EditorUiIntegration::Shutdown() {
  if (!m_Initialized) {
    return;
  }
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  m_Backend = nullptr;
  m_CameraPreviewTexture.reset();
  m_AnalyticalFrameTexture.reset();
  m_ClearCameraPreviewAfterDraw = false;
  m_ClearAnalyticalFrameAfterDraw = false;
  m_Initialized = false;
}

auto EditorUiIntegration::GetTextureId(const Engine::ViewportTarget* target) const noexcept -> void* {
  if (!m_Initialized || (target == nullptr)) {
    return nullptr;
  }
  return m_Backend->RegisterViewportTexture(*target);
}

auto EditorUiIntegration::SetCameraPreview(uint32_t width, uint32_t height, std::span<const uint8_t> rgbaPixels)
    -> bool {
  return SetPreviewTexture(m_CameraPreviewTexture, width, height, rgbaPixels);
}

auto EditorUiIntegration::SetAnalyticalFrameImage(uint32_t width, uint32_t height, std::span<const uint8_t> rgbaPixels)
    -> bool {
  return SetPreviewTexture(m_AnalyticalFrameTexture, width, height, rgbaPixels);
}

auto EditorUiIntegration::SetPreviewTexture(std::unique_ptr<PreviewTexture>& target, uint32_t width, uint32_t height,
                                            std::span<const uint8_t> rgbaPixels) -> bool {
  if (!m_Initialized || m_Backend == nullptr || width == 0 || height == 0 ||
      rgbaPixels.size() != static_cast<size_t>(width) * height * 4U) {
    return false;
  }
  D3D11_TEXTURE2D_DESC textureDescription{};
  textureDescription.Width = width;
  textureDescription.Height = height;
  textureDescription.MipLevels = 1;
  textureDescription.ArraySize = 1;
  textureDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  textureDescription.SampleDesc.Count = 1;
  textureDescription.Usage = D3D11_USAGE_DEFAULT;
  textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA initialData{};
  initialData.pSysMem = rgbaPixels.data();
  initialData.SysMemPitch = width * 4U;
  auto preview = std::make_unique<PreviewTexture>();
  ID3D11Device* device = m_Backend->GetNativeDevice();
  if (device == nullptr || FAILED(device->CreateTexture2D(&textureDescription, &initialData, &preview->Texture)) ||
      FAILED(device->CreateShaderResourceView(preview->Texture.Get(), nullptr, &preview->View))) {
    return false;
  }
  if (&target == &m_CameraPreviewTexture) {
    m_ClearCameraPreviewAfterDraw = false;
  } else if (&target == &m_AnalyticalFrameTexture) {
    m_ClearAnalyticalFrameAfterDraw = false;
  }
  target = std::move(preview);
  return true;
}

auto EditorUiIntegration::GetCameraPreviewTextureId() const noexcept -> void* {
  return m_CameraPreviewTexture == nullptr ? nullptr : m_CameraPreviewTexture->View.Get();
}

void EditorUiIntegration::ClearCameraPreview() noexcept {
  if (m_Initialized) {
    m_ClearCameraPreviewAfterDraw = true;
  } else {
    m_CameraPreviewTexture.reset();
    m_ClearCameraPreviewAfterDraw = false;
  }
}

auto EditorUiIntegration::GetAnalyticalFrameTextureId() const noexcept -> void* {
  return m_AnalyticalFrameTexture == nullptr ? nullptr : m_AnalyticalFrameTexture->View.Get();
}

void EditorUiIntegration::ClearAnalyticalFrameImage() noexcept {
  if (m_Initialized) {
    m_ClearAnalyticalFrameAfterDraw = true;
  } else {
    m_AnalyticalFrameTexture.reset();
    m_ClearAnalyticalFrameAfterDraw = false;
  }
}
