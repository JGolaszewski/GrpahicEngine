#include "scene/include/components/Transform.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace {

void BM_TransformWorldMatrixCacheHit(benchmark::State& state) {
  Engine::Components::Transform transform;
  transform.SetPosition(1.0F, 2.0F, 3.0F);
  for (auto _ : state) {
    const DirectX::XMMATRIX matrix = transform.GetWorldMat();
    benchmark::DoNotOptimize(matrix);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TransformWorldMatrixCacheHit);

void BM_TransformWorldMatrixParentDepth(benchmark::State& state) {
  const auto depth = static_cast<std::size_t>(state.range(0));
  std::vector<std::unique_ptr<Engine::Components::Transform>> transforms;
  transforms.reserve(depth);
  for (std::size_t index = 0; index < depth; ++index) {
    transforms.push_back(std::make_unique<Engine::Components::Transform>());
    transforms.back()->SetPosition(0.1F, 0.2F, 0.3F);
    if (index > 0) {
      transforms.back()->SetParent(transforms[index - 1].get());
    }
  }
  for (auto _ : state) {
    const DirectX::XMMATRIX matrix = transforms.back()->GetWorldMat();
    benchmark::DoNotOptimize(matrix);
  }
  state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_TransformWorldMatrixParentDepth)->Arg(1)->Arg(8)->Arg(32);

} // namespace
