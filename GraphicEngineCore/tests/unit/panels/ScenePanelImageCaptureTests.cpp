#include "events/include/EditorEvents.h"
#include "panels/include/ScenePanel.h"

#include <gtest/gtest.h>
#include <imgui.h>
#include <imgui_internal.h>

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

} // namespace

TEST(ScenePanelImageCaptureTests, ClickOnRenderedViewportIsOwnedByTheImageItem) {
  ImGuiContextScope context;
  ScenePanel panel;
  ViewportChangedEvent viewportChanged(reinterpret_cast<void*>(1), 800.0F, 600.0F);
  panel.OnEvent(viewportChanged);

  ImGuiIO& io = ImGui::GetIO();
  ImGui::NewFrame();
  ImGui::SetNextWindowPos(ImVec2(0.0F, 0.0F));
  ImGui::SetNextWindowSize(ImVec2(800.0F, 600.0F));
  panel.Draw();
  ImGui::Render();

  ImGuiWindow* viewport = ImGui::FindWindowByName("Viewport");
  ASSERT_NE(viewport, nullptr);
  const ImGuiID viewportImageId = viewport->GetID("ViewportImage");

  const ImVec2 imageCenter = viewport->InnerRect.GetCenter();
  io.AddMousePosEvent(imageCenter.x, imageCenter.y);
  ImGui::NewFrame();
  panel.Draw();
  ASSERT_EQ(GImGui->HoveredId, viewportImageId);
  ImGui::Render();

  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  ImGui::NewFrame();
  panel.Draw();

  EXPECT_EQ(GImGui->ActiveId, viewportImageId);
  EXPECT_EQ(GImGui->MovingWindow, nullptr);
  ImGui::Render();
}
