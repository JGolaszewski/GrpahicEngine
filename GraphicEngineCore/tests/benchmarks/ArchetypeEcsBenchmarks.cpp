#include "scene/include/ArchetypeWorld.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <span>

namespace {
struct Position {
  float X = 0.0F;
  float Y = 0.0F;
  float Z = 0.0F;
};

struct Velocity {
  float X = 1.0F;
  float Y = 2.0F;
  float Z = 3.0F;
};

void IntegrateArchetypeColumns(benchmark::State& state) {
  Engine::ArchetypeWorld world;
  const auto entityCount = static_cast<int64_t>(state.range(0));
  for (int64_t index = 0; index < entityCount; ++index) {
    const auto created = world.Create(Position{static_cast<float>(index)}, Velocity{});
    if (!created) {
      state.SkipWithError("failed to populate archetype world");
      return;
    }
  }

  for (auto _ : state) {
    world.ForEachChunk<Position, Velocity>([](std::span<const Engine::EntityHandle>, std::span<Position> positions,
                                              std::span<Velocity> velocities) -> void {
      for (size_t row = 0; row < positions.size(); ++row) {
        positions[row].X += velocities[row].X;
        positions[row].Y += velocities[row].Y;
        positions[row].Z += velocities[row].Z;
      }
    });
    benchmark::DoNotOptimize(world.EntityCount());
  }
  state.SetItemsProcessed(state.iterations() * entityCount);
}

BENCHMARK(IntegrateArchetypeColumns)->Arg(1000)->Arg(10000)->Arg(100000);
} // namespace
