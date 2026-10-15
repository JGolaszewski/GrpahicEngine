#include "rendering/src/render_graph/FrameContext.h"
#include "rendering/src/render_graph/RenderGraph.h"
#include "rendering/src/render_graph/RenderPass.h"
#include "scene/include/Scene.h"
#include "scene/include/SceneDocument.h"
#include "rendering/include/renderer/Entity.h"
#include "scene/include/components/Transform.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{

void BM_TransformWorldMatrixCacheHit(benchmark::State &state)
{
    Engine::Components::Transform transform;
    transform.SetPosition(1.0F, 2.0F, 3.0F);
    for (auto _ : state)
    {
        DirectX::XMMATRIX matrix = transform.GetWorldMat();
        benchmark::DoNotOptimize(matrix);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TransformWorldMatrixCacheHit);

void BM_TransformWorldMatrixParentDepth(benchmark::State &state)
{
    const auto depth = static_cast<std::size_t>(state.range(0));
    std::vector<std::unique_ptr<Engine::Components::Transform>> transforms;
    transforms.reserve(depth);
    for (std::size_t index = 0; index < depth; ++index)
    {
        transforms.push_back(std::make_unique<Engine::Components::Transform>());
        transforms.back()->SetPosition(0.1F, 0.2F, 0.3F);
        if (index > 0)
        {
            transforms.back()->SetParent(transforms[index - 1].get());
        }
    }
    for (auto _ : state)
    {
        DirectX::XMMATRIX matrix = transforms.back()->GetWorldMat();
        benchmark::DoNotOptimize(matrix);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TransformWorldMatrixParentDepth)->Arg(1)->Arg(8)->Arg(32);

void BM_SceneTraversal(benchmark::State &state)
{
    Engine::Scene scene;
    const auto entityCount = static_cast<std::size_t>(state.range(0));
    for (std::size_t index = 0; index < entityCount; ++index)
    {
        auto entity = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
        entity->transform.SetPosition(static_cast<float>(index), 0.0f, 0.0f);
        scene.AddEntity(std::move(entity));
    }

    for (auto _ : state)
    {
        for (const auto &entity : scene.GetEntities())
        {
            DirectX::XMMATRIX world = entity->transform.GetWorldMat();
            benchmark::DoNotOptimize(world);
        }
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(entityCount));
}
BENCHMARK(BM_SceneTraversal)->Arg(100)->Arg(1000)->Arg(10000);

void BM_SceneDocumentSave(benchmark::State &state)
{
    Engine::SceneDocument document;
    const auto entityCount = static_cast<std::size_t>(state.range(0));
    document.entities.reserve(entityCount);
    for (std::size_t index = 0; index < entityCount; ++index)
    {
        Engine::SceneEntityDocument entity;
        entity.id = index + 1;
        entity.name = "BenchmarkEntity";
        entity.primitiveType = "cube";
        entity.position = {static_cast<float>(index), 0.0f, 0.0f};
        document.entities.push_back(std::move(entity));
    }

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "graphic-engine-scene-benchmark.json";
    Engine::SceneSerializer serializer;
    std::string error;
    for (auto _ : state)
    {
        if (!serializer.SaveAtomic(path, document, error))
        {
            state.SkipWithError(error.c_str());
            break;
        }
        benchmark::ClobberMemory();
    }
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(entityCount));
}
BENCHMARK(BM_SceneDocumentSave)->Arg(100)->Arg(1000);

struct RenderGraphBenchResources
{
    std::vector<Engine::Renderer::RGResourceID> Outputs;
};

class BenchmarkGraphPass final : public Engine::Renderer::IRenderPass
{
  public:
    BenchmarkGraphPass(std::shared_ptr<RenderGraphBenchResources> resources, std::size_t index)
        : m_Resources(std::move(resources)), m_Index(index)
    {
    }

    std::string GetName() const override
    {
        return "BenchmarkGraphPass";
    }
    bool HasSideEffect() const override
    {
        return true;
    }
    void Setup(Engine::Renderer::RGBuilder &builder, Engine::Renderer::FrameBlackboard &) override
    {
        if (m_Index > 0)
            builder.Read(m_Resources->Outputs[m_Index - 1]);
        Engine::Renderer::RGTextureDesc desc;
        desc.width = 256;
        desc.height = 256;
        desc.format = nvrhi::Format::RGBA8_UNORM;
        desc.isRenderTarget = true;
        m_Output = builder.CreateAndWriteTexture(desc);
        m_Resources->Outputs.push_back(m_Output);
    }
    bool Execute(nvrhi::ICommandList *, const Engine::Renderer::RenderContext &) override
    {
        return true;
    }
    Engine::Renderer::RGResourceID GetOutputID() const override
    {
        return m_Output;
    }

  private:
    std::shared_ptr<RenderGraphBenchResources> m_Resources;
    std::size_t m_Index;
    Engine::Renderer::RGResourceID m_Output;
};

void BM_RenderGraphBuildAndCompile(benchmark::State &state)
{
    const auto passCount = static_cast<std::size_t>(state.range(0));
    Engine::Renderer::FrameContext frameContext(nullptr);
    Engine::Renderer::RenderGraph graph(nvrhi::DeviceHandle{}, frameContext);
    auto resources = std::make_shared<RenderGraphBenchResources>();
    resources->Outputs.reserve(passCount);

    for (auto _ : state)
    {
        graph.Reset();
        resources->Outputs.clear();
        for (std::size_t index = 0; index < passCount; ++index)
        {
            if (!graph.AddPass(std::make_unique<BenchmarkGraphPass>(resources, index)))
            {
                state.SkipWithError(graph.GetLastError().c_str());
                return;
            }
        }
        if (!graph.Compile())
        {
            state.SkipWithError(graph.GetLastError().c_str());
            return;
        }
        benchmark::DoNotOptimize(graph);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(passCount));
}
BENCHMARK(BM_RenderGraphBuildAndCompile)->Arg(8)->Arg(32)->Arg(128);

} // namespace
