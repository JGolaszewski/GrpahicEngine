#include "rendering/src/SceneRenderer.h"
#include "rendering/src/render_graph/FrameContext.h"
#include "rendering/src/render_graph/RenderGraph.h"
#include "rendering/src/render_graph/RenderPass.h"
#include "scene/include/Scene.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
struct SharedGraphResource
{
    Engine::Renderer::RGResourceID Id;
};

class WriteTexturePass final : public Engine::Renderer::IRenderPass
{
  public:
    explicit WriteTexturePass(std::shared_ptr<SharedGraphResource> resource) : m_Resource(std::move(resource))
    {
    }

    std::string GetName() const override
    {
        return "WriteTexture";
    }
    bool HasSideEffect() const override
    {
        return true;
    }
    void Setup(Engine::Renderer::RGBuilder &builder, Engine::Renderer::FrameBlackboard &) override
    {
        Engine::Renderer::RGTextureDesc desc;
        desc.width = 64;
        desc.height = 64;
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.isRenderTarget = true;
        m_Resource->Id = builder.CreateAndWriteTexture(desc);
    }
    bool Execute(nvrhi::ICommandList *, const Engine::Renderer::RenderContext &) override
    {
        return true;
    }
    Engine::Renderer::RGResourceID GetOutputID() const override
    {
        return m_Resource->Id;
    }

  private:
    std::shared_ptr<SharedGraphResource> m_Resource;
};

class ReadUnproducedTexturePass final : public Engine::Renderer::IRenderPass
{
  public:
    std::string GetName() const override
    {
        return "ReadUnproducedTexture";
    }
    bool HasSideEffect() const override
    {
        return true;
    }
    void Setup(Engine::Renderer::RGBuilder &builder, Engine::Renderer::FrameBlackboard &) override
    {
        Engine::Renderer::RGTextureDesc desc;
        desc.format = nvrhi::Format::RGBA8_UNORM;
        const Engine::Renderer::RGResourceID texture = builder.CreateTexture(desc);
        builder.Read(texture);
    }
    bool Execute(nvrhi::ICommandList *, const Engine::Renderer::RenderContext &) override
    {
        return true;
    }
};

class ReadProducedTexturePass final : public Engine::Renderer::IRenderPass
{
  public:
    explicit ReadProducedTexturePass(std::shared_ptr<SharedGraphResource> resource)
        : m_Resource(std::move(resource))
    {
    }
    std::string GetName() const override
    {
        return "ReadProducedTexture";
    }
    bool HasSideEffect() const override
    {
        return true;
    }
    void Setup(Engine::Renderer::RGBuilder &builder, Engine::Renderer::FrameBlackboard &) override
    {
        builder.Read(m_Resource->Id);
    }
    bool Execute(nvrhi::ICommandList *, const Engine::Renderer::RenderContext &) override
    {
        return true;
    }

  private:
    std::shared_ptr<SharedGraphResource> m_Resource;
};

class InvalidGraphSetupPass final : public Engine::Renderer::IRenderPass
{
  public:
    enum class SetupError : std::uint8_t
    {
        InvalidDescriptor,
        InvalidResource,
        InvalidState,
        InvalidRange,
        OverlappingWrites
    };

    explicit InvalidGraphSetupPass(SetupError error) : m_Error(error)
    {
    }
    std::string GetName() const override
    {
        return "InvalidGraphSetup";
    }
    void Setup(Engine::Renderer::RGBuilder &builder, Engine::Renderer::FrameBlackboard &) override
    {
        using namespace Engine::Renderer;
        if (m_Error == SetupError::InvalidDescriptor)
        {
            RGTextureDesc desc;
            desc.width = 0;
            desc.format = nvrhi::Format::RGBA8_UNORM;
            builder.CreateTexture(desc);
            return;
        }
        if (m_Error == SetupError::InvalidResource)
        {
            builder.Read(RGResourceID{7});
            return;
        }

        RGTextureDesc desc;
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.isRenderTarget = true;
        desc.isShaderResource = m_Error == SetupError::InvalidRange;
        const RGResourceID id = builder.CreateTexture(desc);
        if (m_Error == SetupError::InvalidState)
        {
            builder.Read(id, nvrhi::ResourceStates::CopyDest);
        }
        else if (m_Error == SetupError::InvalidRange)
        {
            builder.Read(id, nvrhi::ResourceStates::ShaderResource, nvrhi::TextureSubresourceSet{3, 1, 0, 1});
        }
        else
        {
            builder.Write(id);
            builder.Write(id);
        }
    }
    bool Execute(nvrhi::ICommandList *, const Engine::Renderer::RenderContext &) override
    {
        return true;
    }

  private:
    SetupError m_Error;
};

class InvalidOutputPass final : public Engine::Renderer::IRenderPass
{
  public:
    std::string GetName() const override
    {
        return "InvalidOutput";
    }
    void Setup(Engine::Renderer::RGBuilder &, Engine::Renderer::FrameBlackboard &) override
    {
    }
    bool Execute(nvrhi::ICommandList *, const Engine::Renderer::RenderContext &) override
    {
        return true;
    }
    Engine::Renderer::RGResourceID GetOutputID() const override
    {
        return {7};
    }
};

TEST(RenderGraphCpuPathTests, CompilesOutputAndRejectsReadWithoutProducer)
{
    Engine::Renderer::FrameContext frameContext(nullptr);
    Engine::Renderer::RenderGraph graph(nvrhi::DeviceHandle{}, frameContext);
    const auto resource = std::make_shared<SharedGraphResource>();

    ASSERT_TRUE(graph.AddPass(std::make_unique<WriteTexturePass>(resource)));
    EXPECT_TRUE(graph.Compile()) << graph.GetLastError();
    EXPECT_TRUE(resource->Id.isValid());
    EXPECT_FALSE(graph.Execute(nullptr));

    graph.Reset();
    ASSERT_TRUE(graph.AddPass(std::make_unique<ReadUnproducedTexturePass>()));
    EXPECT_FALSE(graph.Compile());
    EXPECT_NE(graph.GetLastError().find("no producer"), std::string::npos);
}

TEST(RenderGraphCpuPathTests, HandlesEmptyGraphsAndRejectsInvalidPassSetup)
{
    Engine::Renderer::FrameContext frameContext(nullptr);
    Engine::Renderer::RenderGraph emptyGraph(nvrhi::DeviceHandle{}, frameContext);
    EXPECT_TRUE(emptyGraph.Compile());
    EXPECT_FALSE(emptyGraph.AddPass(nullptr));
    EXPECT_NE(emptyGraph.GetLastError().find("null render pass"), std::string::npos);
    EXPECT_FALSE(emptyGraph.Execute(nullptr));

    const std::vector<InvalidGraphSetupPass::SetupError> invalidSetups = {
        InvalidGraphSetupPass::SetupError::InvalidDescriptor,
        InvalidGraphSetupPass::SetupError::InvalidResource, InvalidGraphSetupPass::SetupError::InvalidState,
        InvalidGraphSetupPass::SetupError::InvalidRange,
        InvalidGraphSetupPass::SetupError::OverlappingWrites};
    for (const auto invalidSetup : invalidSetups)
    {
        Engine::Renderer::RenderGraph graph(nvrhi::DeviceHandle{}, frameContext);
        EXPECT_FALSE(graph.AddPass(std::make_unique<InvalidGraphSetupPass>(invalidSetup)));
        EXPECT_FALSE(graph.GetLastError().empty());
    }

    Engine::Renderer::RenderGraph graph(nvrhi::DeviceHandle{}, frameContext);
    ASSERT_TRUE(graph.AddPass(std::make_unique<InvalidOutputPass>()));
    EXPECT_FALSE(graph.Compile());
    EXPECT_NE(graph.GetLastError().find("invalid resource"), std::string::npos);
}

TEST(RenderGraphCpuPathTests, KeepsOnlySideEffectsAndTheirResourceDependencies)
{
    Engine::Renderer::FrameContext frameContext(nullptr);
    Engine::Renderer::RenderGraph graph(nvrhi::DeviceHandle{}, frameContext);
    const auto resource = std::make_shared<SharedGraphResource>();
    ASSERT_TRUE(graph.AddPass(std::make_unique<WriteTexturePass>(resource)));
    ASSERT_TRUE(graph.AddPass(std::make_unique<ReadProducedTexturePass>(resource)));
    ASSERT_TRUE(graph.Compile()) << graph.GetLastError();
    const std::string dump = graph.Dump();
    EXPECT_NE(dump.find("WriteTexture"), std::string::npos);
    EXPECT_NE(dump.find("ReadProducedTexture"), std::string::npos);

    graph.Reset();
    class UnusedPass final : public Engine::Renderer::IRenderPass
    {
      public:
        std::string GetName() const override
        {
            return "UnusedPass";
        }
        void Setup(Engine::Renderer::RGBuilder &, Engine::Renderer::FrameBlackboard &) override
        {
        }
        bool Execute(nvrhi::ICommandList *, const Engine::Renderer::RenderContext &) override
        {
            return true;
        }
    };
    ASSERT_TRUE(graph.AddPass(std::make_unique<UnusedPass>()));
    ASSERT_TRUE(graph.Compile()) << graph.GetLastError();
    EXPECT_EQ(graph.Dump().find("UnusedPass"), std::string::npos);
}

TEST(SceneRendererCpuPathTests, BuildsShadowDataAndLightingWithinCasterLimits)
{
    Engine::Scene scene;
    auto shadowLight = std::make_shared<Engine::Renderer::SpotLight>();
    shadowLight->SetUsePosition(true);
    shadowLight->SetCastShadows(true);
    scene.AddLight(shadowLight);

    auto nonShadowLight = std::make_shared<Engine::Renderer::SpotLight>();
    nonShadowLight->SetUsePosition(false);
    nonShadowLight->SetCastShadows(true);
    scene.AddLight(nonShadowLight);

    Engine::SceneRenderer renderer(nullptr);
    EXPECT_EQ(renderer.CountShadowCasters(&scene, 0), 0U);
    EXPECT_EQ(renderer.CountShadowCasters(&scene, 1), 1U);
    EXPECT_EQ(renderer.CountShadowCasters(&scene, Engine::MaxShadowedLights), 1U);
    EXPECT_EQ(renderer.CountShadowCasters(nullptr, Engine::MaxShadowedLights), 0U);

    Engine::Renderer::RenderView shadowView;
    std::vector<Engine::Renderer::RenderView *> shadowViews{&shadowView};
    renderer.BuildShadowData(&scene, shadowViews, 0, 1);
    EXPECT_EQ(shadowView.TargetScene, &scene);

    std::vector<Engine::Renderer::DirectionalLightData> lighting;
    renderer.BuildLighting(&scene, shadowViews, lighting);
    ASSERT_EQ(lighting.size(), 2U);
    EXPECT_FLOAT_EQ(lighting[0].ShadowParams.x, 0.0F);
}

TEST(SceneRendererCpuPathTests, PicksLightConesAndRejectsInvalidOrOutOfRangeViewportCoordinates)
{
    Engine::SceneRenderer renderer(nullptr);
    Engine::Renderer::RenderView view;
    view.ViewportWidth = 100;
    view.ViewportHeight = 100;

    EXPECT_FALSE(renderer.PickLight(view, 50.0f, 50.0f).has_value());
    EXPECT_FALSE(renderer.PickLight(view, -1.0f, 50.0f).has_value());
    EXPECT_FALSE(renderer.PickLight(view, 100.0f, 50.0f).has_value());
    EXPECT_FALSE(renderer.PickLight(view, 50.0f, 100.0f).has_value());
    view.ViewportWidth = 0;
    EXPECT_FALSE(renderer.PickLight(view, 0.0f, 0.0f).has_value());
    view.ViewportWidth = 100;
    view.ViewportHeight = 0;
    EXPECT_FALSE(renderer.PickLight(view, 0.0f, 0.0f).has_value());
    view.ViewportHeight = 100;

    view.DebugGeometryData.AddLightTarget(
        {41, {0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}, 10.0f, DirectX::XM_PIDIV4});
    view.DebugGeometryData.AddLightTarget(
        {42, {0.0f, 0.0f, 3.0f}, {0.0f, 0.0f, -1.0f}, 10.0f, DirectX::XM_PIDIV4});
    EXPECT_EQ(renderer.PickLight(view, 50.0f, 50.0f), std::optional<Engine::LightId>{42});

    view.DebugGeometryData.Clear();
    view.DebugGeometryData.AddLightTarget({43, {0.1f, 0.0f, 5.0f}, {0.0f, 0.0f, -1.0f}, 10.0f, 0.1f});
    EXPECT_EQ(renderer.PickLight(view, 50.0f, 50.0f), std::optional<Engine::LightId>{43});

    view.DebugGeometryData.Clear();
    view.DebugGeometryData.AddLightTarget(
        {44, {0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 1.0f}, 1.0f, DirectX::XM_PIDIV4});
    EXPECT_FALSE(renderer.PickLight(view, 50.0f, 50.0f).has_value());
}

TEST(SceneRendererCpuPathTests, MatchesShadowViewsToTheirOwningSceneInOrder)
{
    Engine::Scene scene;
    auto first = std::make_shared<Engine::Renderer::SpotLight>();
    first->SetUsePosition(true);
    first->SetCastShadows(true);
    scene.AddLight(first);
    auto second = std::make_shared<Engine::Renderer::SpotLight>();
    second->SetUsePosition(true);
    second->SetCastShadows(true);
    scene.AddLight(second);

    Engine::Scene unrelatedScene;
    Engine::Renderer::RenderView unrelatedView;
    unrelatedView.TargetScene = &unrelatedScene;
    Engine::Renderer::RenderView matchingView;
    matchingView.TargetScene = &scene;
    std::vector<Engine::Renderer::RenderView *> shadowViews{nullptr, &unrelatedView, &matchingView};

    Engine::SceneRenderer renderer(nullptr);
    std::vector<Engine::Renderer::DirectionalLightData> lighting;
    renderer.BuildLighting(&scene, shadowViews, lighting);
    ASSERT_EQ(lighting.size(), 2U);
    EXPECT_FLOAT_EQ(lighting[0].ShadowParams.x, 2.0f);
    EXPECT_FLOAT_EQ(lighting[1].ShadowParams.x, -1.0f);

    const auto previousLighting = lighting;
    renderer.BuildLighting(nullptr, shadowViews, lighting);
    EXPECT_EQ(lighting.size(), previousLighting.size());
    EXPECT_FLOAT_EQ(lighting[0].ShadowParams.x, previousLighting[0].ShadowParams.x);

    renderer.BuildShadowData(&scene, shadowViews, static_cast<uint32_t>(shadowViews.size()), 1);
    EXPECT_EQ(matchingView.TargetScene, &scene);
}
} // namespace
