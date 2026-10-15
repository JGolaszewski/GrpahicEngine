#include "platform/include/EditorUiIntegration.h"

#include "backends/D3D11Backend.h"
#include "rendering/include/ViewportTarget.h"
#include "runtime/include/Window.h"

#include <GLFW/glfw3.h>
#include <algorithm>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_glfw.h>
#include <nvrhi/nvrhi.h>

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
}

void EditorUiIntegration::Shutdown() {
  if (!m_Initialized) {
    return;
  }
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  m_Backend = nullptr;
  m_Initialized = false;
}

auto EditorUiIntegration::GetTextureId(const Engine::ViewportTarget* target) const noexcept -> void* {
  if (!m_Initialized || (target == nullptr)) {
    return nullptr;
  }
  return m_Backend->RegisterViewportTexture(target->GetColorTextureHandle());
}
