#include "platform/include/EditorUiIntegration.h"

#include "rendering/include/RenderBackend.h"
#include "runtime/include/Window.h"

#include <gtest/gtest.h>
#include <imgui.h>

namespace
{

class TestWindow final : public Engine::IWindow
{
  public:
    auto GetNativeHandle() -> void * override
    {
        return this;
    }
    auto GetEventSourceHandle() const -> void * override
    {
        return const_cast<TestWindow *>(this);
    }
    auto ShouldClose() const -> bool override
    {
        return false;
    }
    void PollEvents() override
    {
    }
    void SetEventCallback(Engine::Events::EventCallbackFn) override
    {
    }
    void SetCursorVisible(bool) override
    {
    }
    auto GetWidth() const -> uint32_t override
    {
        return 640;
    }
    auto GetHeight() const -> uint32_t override
    {
        return 480;
    }
};

class TestRenderBackend final : public Engine::IRenderBackend
{
  public:
    auto Initialize(Engine::IWindow *) -> bool override
    {
        return true;
    }
    void Present() override
    {
    }
    void Resize(int, int) override
    {
    }
    auto GetDevice() const -> void * override
    {
        return nullptr;
    }
    auto GetDefaultFramebuffer() const -> void * override
    {
        return nullptr;
    }
    auto InitializeEditorUi(Engine::IWindow *window) -> bool override
    {
        ++initializeCalls;
        receivedWindow = window;
        return initializeResult;
    }
    void BeginEditorUiFrame() override
    {
        ++beginFrameCalls;
    }
    void RenderEditorUi(void *drawData) override
    {
        ++renderCalls;
        receivedDrawData = drawData;
    }
    void ShutdownEditorUi() override
    {
        ++shutdownCalls;
    }

    bool initializeResult = true;
    int initializeCalls = 0;
    int beginFrameCalls = 0;
    int renderCalls = 0;
    int shutdownCalls = 0;
    Engine::IWindow *receivedWindow = nullptr;
    void *receivedDrawData = nullptr;
};

TEST(EditorUiIntegrationTests, RejectsMissingInputsAndBackendInitializationFailure)
{
    TestWindow window;
    TestRenderBackend backend;
    backend.initializeResult = false;
    EditorUiIntegration integration;

    EXPECT_FALSE(integration.Initialize(nullptr, &backend));
    EXPECT_FALSE(integration.Initialize(&window, nullptr));
    EXPECT_FALSE(integration.Initialize(&window, &backend));
    EXPECT_FALSE(integration.IsInitialized());
    EXPECT_EQ(backend.initializeCalls, 1);
    EXPECT_EQ(backend.shutdownCalls, 0);
}

TEST(EditorUiIntegrationTests, RunsAndShutsDownACompleteUiFrame)
{
    TestWindow window;
    TestRenderBackend backend;
    EditorUiIntegration integration;

    ASSERT_TRUE(integration.Initialize(&window, &backend));
    ASSERT_TRUE(integration.IsInitialized());
    EXPECT_EQ(backend.receivedWindow, &window);
    ImGui::GetIO().DisplaySize = ImVec2(640.0F, 480.0F);
    ASSERT_TRUE(ImGui::GetIO().Fonts->Build());

    integration.BeginFrame();
    integration.EndFrame();
    EXPECT_EQ(backend.beginFrameCalls, 1);
    EXPECT_EQ(backend.renderCalls, 1);
    EXPECT_NE(backend.receivedDrawData, nullptr);

    integration.Shutdown();
    integration.Shutdown();
    EXPECT_FALSE(integration.IsInitialized());
    EXPECT_EQ(backend.shutdownCalls, 1);
}

} // namespace
