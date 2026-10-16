#include "application/include/EditorEventRouter.h"
#include "events/include/EditorEvents.h"
#include "panels/include/ScenePanel.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace {

class ImGuiContextScope final {
public:
  ImGuiContextScope() : m_Context(ImGui::CreateContext()) {
    ImGui::SetCurrentContext(m_Context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(800.0F, 600.0F);
    io.DeltaTime = 1.0F / 60.0F;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    unsigned char* atlasPixels = nullptr;
    int atlasWidth = 0;
    int atlasHeight = 0;
    io.Fonts->GetTexDataAsRGBA32(&atlasPixels, &atlasWidth, &atlasHeight);
  }

  ~ImGuiContextScope() { ImGui::DestroyContext(m_Context); }

private:
  ImGuiContext* m_Context = nullptr;
};

class ScenePanelInteraction final {
public:
  ScenePanelInteraction() {
    m_Connection = m_Router.Subscribe(m_Panel, [this](std::unique_ptr<Engine::Events::Event> event) -> void {
      m_Requests.push_back(std::move(event));
    });

    ViewportChangedEvent viewportChanged(reinterpret_cast<void*>(1), 800.0F, 600.0F);
    m_Panel.OnEvent(viewportChanged);
    CameraPreviewChangedEvent previewChanged(Engine::EntityHandle{42}, reinterpret_cast<void*>(2), 200.0F, 100.0F,
                                             0.35F);
    m_Panel.OnEvent(previewChanged);

    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
    ImGui::SetNextWindowSize(ImVec2(800.0F, 600.0F));
    m_Panel.Draw();
    ImGui::Render();
    m_Requests.clear();
  }

  void MoveMouseTo(ImVec2 position) {
    ImGui::GetIO().AddMousePosEvent(position.x, position.y);
    DrawFrame();
  }

  void ClickLeft() {
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
    DrawFrame();
  }

  void ClearRequests() { m_Requests.clear(); }

  [[nodiscard]] auto ViewportWindow() const -> ImGuiWindow* { return ImGui::FindWindowByName("Viewport"); }
  [[nodiscard]] auto Requests() const -> const std::vector<std::unique_ptr<Engine::Events::Event>>& {
    return m_Requests;
  }

private:
  void DrawFrame() {
    ImGui::NewFrame();
    m_Panel.Draw();
    ImGui::Render();
  }

  ScenePanel m_Panel;
  EditorEventRouter m_Router;
  std::vector<std::unique_ptr<Engine::Events::Event>> m_Requests;
  EditorEventRouter::Connection m_Connection;
};

} // namespace

TEST(ScenePanelImageCaptureTests, ClickingCameraPreviewPipRequestsTheFullResolutionWindow) {
  ImGuiContextScope context;
  ScenePanelInteraction scene;
  ImGuiWindow* viewport = scene.ViewportWindow();
  ASSERT_NE(viewport, nullptr);
  const ImVec2 pipClick(viewport->InnerRect.Min.x + 150.0F, viewport->InnerRect.Min.y + 80.0F);

  scene.MoveMouseTo(pipClick);
  ASSERT_EQ(GImGui->HoveredId, viewport->GetID("CameraPreviewImage"));
  scene.ClickLeft();

  const auto& requests = scene.Requests();
  const auto openRequest = std::ranges::find_if(requests, [](const auto& event) -> bool {
    return event->GetType() == CameraPreviewOpenRequestedEvent::GetStaticType();
  });
  ASSERT_NE(openRequest, requests.end());
  EXPECT_EQ(static_cast<const CameraPreviewOpenRequestedEvent&>(**openRequest).GetEntity(), Engine::EntityHandle{42});
  EXPECT_EQ(std::ranges::count_if(
                requests,
                [](const auto& event) -> bool { return event->GetType() == ViewportClickedEvent::GetStaticType(); }),
            0);
}

TEST(ScenePanelImageCaptureTests, ClickingMainViewportStillRoutesViewportCoordinates) {
  ImGuiContextScope context;
  ScenePanelInteraction scene;
  ImGuiWindow* viewport = scene.ViewportWindow();
  ASSERT_NE(viewport, nullptr);
  const ImVec2 clickPosition = viewport->InnerRect.GetCenter();
  ASSERT_FALSE(clickPosition.x < viewport->InnerRect.Min.x + 8.0F + 280.0F &&
               clickPosition.y < viewport->InnerRect.Min.y + 8.0F + 140.0F);

  scene.MoveMouseTo(clickPosition);
  ASSERT_EQ(GImGui->HoveredId, viewport->GetID("ViewportImage"));
  scene.ClickLeft();

  const auto& requests = scene.Requests();
  const auto viewportClick = std::ranges::find_if(
      requests, [](const auto& event) -> bool { return event->GetType() == ViewportClickedEvent::GetStaticType(); });
  ASSERT_NE(viewportClick, requests.end());
  const auto& click = static_cast<const ViewportClickedEvent&>(**viewportClick);
  EXPECT_NEAR(click.GetX(), clickPosition.x - viewport->InnerRect.Min.x, 1.0F);
  EXPECT_NEAR(click.GetY(), clickPosition.y - viewport->InnerRect.Min.y, 1.0F);
  EXPECT_EQ(std::ranges::count_if(requests,
                                  [](const auto& event) -> bool {
                                    return event->GetType() == CameraPreviewOpenRequestedEvent::GetStaticType();
                                  }),
            0);
}
