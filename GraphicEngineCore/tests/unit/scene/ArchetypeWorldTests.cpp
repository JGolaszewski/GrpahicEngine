#include "scene/include/ArchetypeWorld.h"
#include "scene/include/Scene.h"
#include "scene/include/SystemScheduler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <typeindex>

namespace Engine::Tests {
namespace {
struct Position {
  int X = 0;
};
struct Velocity {
  int X = 0;
};
struct Renderable {
  uint32_t Mesh = 0;
};
struct AlignedComponent {
  alignas(64) std::array<uint64_t, 8> Values{};
};

template <typename... Types> auto ComponentTypes() -> std::vector<std::type_index> {
  return {std::type_index(typeid(Types))...};
}
} // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity) // The assertions cover the full query row contract.
TEST(ArchetypeWorldTests, QueriesMatchingSoAChunksAndComposesComponents) {
  ArchetypeWorld world;
  const auto first = world.Create(Position{1}, Velocity{2});
  const auto second = world.Create(Velocity{4}, Position{3}, Renderable{7});
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  int total = 0;
  size_t entities = 0;
  world.ForEachChunk<Position, Velocity>([&](std::span<const EntityHandle> handles, std::span<Position> positions,
                                             std::span<Velocity> velocities) -> void {
    ASSERT_EQ(handles.size(), positions.size());
    ASSERT_EQ(handles.size(), velocities.size());
    entities += handles.size();
    for (size_t row = 0; row < handles.size(); ++row) {
      total += positions[row].X + velocities[row].X;
    }
  });

  EXPECT_EQ(entities, 2U);
  EXPECT_EQ(total, 10);
  EXPECT_TRUE(world.Has<Renderable>(*second));
}

TEST(ArchetypeWorldTests, MigrationPreservesValuesAndRepairsSwapMovedLocations) {
  ArchetypeWorld world;
  const auto first = world.Create(Position{10}, Velocity{20});
  const auto second = world.Create(Position{30}, Velocity{40});
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  ASSERT_TRUE(world.Add(*first, Renderable{5}));
  ASSERT_EQ(world.Get<Position>(*first)->X, 10);
  ASSERT_EQ(world.Get<Velocity>(*first)->X, 20);
  ASSERT_EQ(world.Get<Renderable>(*first)->Mesh, 5U);
  ASSERT_EQ(world.Get<Position>(*second)->X, 30);
  ASSERT_EQ(world.Get<Velocity>(*second)->X, 40);

  ASSERT_TRUE(world.Remove<Velocity>(*first));
  EXPECT_FALSE(world.Has<Velocity>(*first));
  EXPECT_EQ(world.Get<Position>(*first)->X, 10);
  EXPECT_EQ(world.Get<Position>(*second)->X, 30);
}

TEST(ArchetypeWorldTests, DestroyInvalidatesGenerationAndReusesSlot) {
  ArchetypeWorld world;
  const auto first = world.Create(Position{11});
  const auto second = world.Create(Position{22});
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);

  ASSERT_TRUE(world.Destroy(*first));
  EXPECT_FALSE(world.IsAlive(*first));
  EXPECT_EQ(world.Get<Position>(*second)->X, 22);
  const auto replacement = world.Create(Position{33});
  ASSERT_TRUE(replacement);
  EXPECT_EQ(replacement->Index, first->Index);
  EXPECT_NE(replacement->Generation, first->Generation);
  EXPECT_FALSE(world.IsAlive(*first));
  EXPECT_EQ(world.EntityCount(), 2U);
}

TEST(ArchetypeWorldTests, DefersStructuralChangesWhileAQueryIsActive) {
  ArchetypeWorld world;
  const auto entity = world.Create(Position{1});
  ASSERT_TRUE(entity);
  bool structuralMutationRejected = false;

  world.ForEachChunk<Position>([&](std::span<const EntityHandle> handles, std::span<Position>) -> void {
    const auto result = world.Add(handles.front(), Velocity{2});
    structuralMutationRejected = !result;
  });

  EXPECT_TRUE(structuralMutationRejected);
  EXPECT_FALSE(world.Has<Velocity>(*entity));
}

TEST(ArchetypeWorldTests, ProvidesConstSpansForReadOnlyQueries) {
  ArchetypeWorld world;
  ASSERT_TRUE(world.Create(Position{9}));
  const ArchetypeWorld& readOnlyWorld = world;
  int value = 0;
  readOnlyWorld.ForEachChunkReadOnly<Position>(
      [&](std::span<const EntityHandle> handles, std::span<const Position> positions) -> void {
        ASSERT_EQ(handles.size(), positions.size());
        value = positions.front().X;
      });
  EXPECT_EQ(value, 9);
}

TEST(ArchetypeWorldTests, SupportsArchetypeChunksBeyondOneChunkCapacity) {
  ArchetypeWorld world;
  for (size_t index = 0; index <= ArchetypeWorld::ChunkCapacity; ++index) {
    ASSERT_TRUE(world.Create(Position{static_cast<int>(index)}));
  }

  size_t rowCount = 0;
  world.ForEachChunk<Position>([&](std::span<const EntityHandle> handles, std::span<Position> positions) -> void {
    EXPECT_EQ(handles.size(), positions.size());
    rowCount += handles.size();
  });
  EXPECT_EQ(rowCount, ArchetypeWorld::ChunkCapacity + 1);
}

TEST(ArchetypeWorldTests, RejectsDuplicateComponentTypesWithoutPublishingAnEntity) {
  ArchetypeWorld world;
  const auto result = world.Create(Position{1}, Position{2});
  EXPECT_FALSE(result);
  EXPECT_EQ(world.EntityCount(), 0U);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(ArchetypeWorldTests, ComposesMeshLightAndCameraAspectsOnOneEntity) {
  ArchetypeWorld world;
  // NOLINTBEGIN(readability-trailing-comma) // The arguments construct an archetype signature from readable components.
  const auto entity = world.Create(
      Engine::Ecs::Name{.Value = "Vehicle"}, Engine::Ecs::LocalTransform{}, Engine::Ecs::WorldTransform{},
      Engine::Ecs::PersistentIdentity{}, Engine::Ecs::Parent{}, Engine::Ecs::ChildList{},
      Engine::Ecs::LocalVisibility{}, Engine::Ecs::GlobalVisibility{},
      Engine::Ecs::MeshRenderer{.ModelGuid = "model-guid"}, Engine::Ecs::SpotLight{}, Engine::Ecs::Camera{});
  // NOLINTEND(readability-trailing-comma)
  ASSERT_TRUE(entity);
  EXPECT_TRUE(world.Has<Engine::Ecs::MeshRenderer>(*entity));
  EXPECT_TRUE(world.Has<Engine::Ecs::SpotLight>(*entity));
  EXPECT_TRUE(world.Has<Engine::Ecs::Camera>(*entity));

  size_t matchingEntities = 0;
  world.ForEachChunkReadOnly<Engine::Ecs::Name, Engine::Ecs::MeshRenderer, Engine::Ecs::SpotLight, Engine::Ecs::Camera>(
      [&matchingEntities, entity](std::span<const EntityHandle> handles, std::span<const Engine::Ecs::Name> names,
                                  std::span<const Engine::Ecs::MeshRenderer> meshes,
                                  std::span<const Engine::Ecs::SpotLight> lights,
                                  std::span<const Engine::Ecs::Camera> cameras) -> void {
        ASSERT_EQ(handles.size(), 1U);
        EXPECT_EQ(handles.front(), *entity);
        EXPECT_EQ(names.front().Value, "Vehicle");
        EXPECT_EQ(meshes.front().ModelGuid, "model-guid");
        EXPECT_EQ(lights.size(), cameras.size());
        matchingEntities += handles.size();
      });
  EXPECT_EQ(matchingEntities, 1U);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(ArchetypeWorldTests, SwapAndPopRepairsRowsDuringAddAndRemoveMigration) {
  ArchetypeWorld world;
  const auto first = world.Create(Position{10}, Velocity{100});
  const auto second = world.Create(Position{20}, Velocity{200});
  const auto third = world.Create(Position{30}, Velocity{300});
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  ASSERT_TRUE(third);

  ASSERT_TRUE(world.Add(*first, Renderable{1}));
  EXPECT_EQ(world.Get<Position>(*second)->X, 20);
  EXPECT_EQ(world.Get<Velocity>(*second)->X, 200);
  EXPECT_EQ(world.Get<Position>(*third)->X, 30);
  EXPECT_EQ(world.Get<Velocity>(*third)->X, 300);

  ASSERT_TRUE(world.Remove<Renderable>(*first));
  ASSERT_TRUE(world.Remove<Velocity>(*second));
  EXPECT_EQ(world.Get<Position>(*first)->X, 10);
  EXPECT_EQ(world.Get<Velocity>(*first)->X, 100);
  EXPECT_EQ(world.Get<Position>(*third)->X, 30);
  EXPECT_EQ(world.Get<Velocity>(*third)->X, 300);
  EXPECT_EQ(world.Get<Position>(*second)->X, 20);
  EXPECT_EQ(world.Get<Velocity>(*second), nullptr);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(ArchetypeWorldTests, SwapAndPopPreservesAllRowsAcrossRepeatedArchetypeTransitions) {
  ArchetypeWorld world;
  std::array<EntityHandle, 3> entities{};
  for (size_t index = 0; index < entities.size(); ++index) {
    const auto created =
        world.Create(Position{static_cast<int>((index + 1) * 10)}, Velocity{static_cast<int>((index + 1) * 100)});
    ASSERT_TRUE(created);
    entities[index] = *created;
  }

  ASSERT_TRUE(world.Add(entities[1], Renderable{2}));
  ASSERT_TRUE(world.Destroy(entities[0]));
  ASSERT_TRUE(world.Remove<Renderable>(entities[1]));
  ASSERT_TRUE(world.Add(entities[2], Renderable{3}));

  EXPECT_FALSE(world.IsAlive(entities[0]));
  EXPECT_EQ(world.Get<Position>(entities[1])->X, 20);
  EXPECT_EQ(world.Get<Velocity>(entities[1])->X, 200);
  EXPECT_EQ(world.Get<Position>(entities[2])->X, 30);
  EXPECT_EQ(world.Get<Velocity>(entities[2])->X, 300);
  EXPECT_EQ(world.Get<Renderable>(entities[2])->Mesh, 3U);
}

TEST(ArchetypeWorldTests, PreservesOverAlignedComponentsAcrossQueriesAndMigration) {
  ArchetypeWorld world;
  const auto entity = world.Create(AlignedComponent{.Values = {0x1234U}}, Position{4});
  ASSERT_TRUE(entity);
  auto* stored = world.Get<AlignedComponent>(*entity);
  ASSERT_NE(stored, nullptr);
  void* storedAddress = stored;
  size_t availableSpace = sizeof(AlignedComponent);
  EXPECT_EQ(std::align(alignof(AlignedComponent), sizeof(AlignedComponent), storedAddress, availableSpace), stored);
  ASSERT_TRUE(world.Add(*entity, Velocity{8}));
  stored = world.Get<AlignedComponent>(*entity);
  ASSERT_NE(stored, nullptr);
  storedAddress = stored;
  availableSpace = sizeof(AlignedComponent);
  EXPECT_EQ(std::align(alignof(AlignedComponent), sizeof(AlignedComponent), storedAddress, availableSpace), stored);
  EXPECT_EQ(stored->Values[0], 0x1234U);
}

TEST(SystemSchedulerTests, ExecutesDeclaredSystemsOverMatchingArchetypes) {
  ArchetypeWorld world;
  ASSERT_TRUE(world.Create(Position{1}, Velocity{2}));
  ASSERT_TRUE(world.Create(Position{3}, Velocity{4}));
  SystemScheduler scheduler;
  ASSERT_TRUE(scheduler.AddSystem({
      .Name = "Integrate position",
      .Reads = ComponentTypes<Velocity>(),
      .Writes = ComponentTypes<Position>(),
      .Execute = [](ArchetypeWorld& target) -> void {
        target.ForEachChunk<Position, Velocity>(
            [](std::span<const EntityHandle>, std::span<Position> positions, std::span<Velocity> velocities) -> void {
              for (size_t row = 0; row < positions.size(); ++row) {
                positions[row].X += velocities[row].X;
              }
            });
      },
  }));

  ASSERT_TRUE(scheduler.Execute(world));
  EXPECT_EQ(world.Get<Position>({0, 1})->X, 3);
  EXPECT_EQ(world.Get<Position>({1, 1})->X, 7);
}

TEST(SystemSchedulerTests, PlacesSimultaneousReadWriteConflictsInSeparateBatches) {
  SystemScheduler scheduler;
  ASSERT_TRUE(scheduler.AddSystem({
      .Name = "Position reader",
      .Reads = ComponentTypes<Position>(),
      .Execute = [](ArchetypeWorld&) -> void {},
  }));
  ASSERT_TRUE(scheduler.AddSystem({
      .Name = "Position writer",
      .Writes = ComponentTypes<Position>(),
      .Execute = [](ArchetypeWorld&) -> void {},
  }));
  ASSERT_TRUE(scheduler.AddSystem({
      .Name = "Second position writer",
      .Writes = ComponentTypes<Position>(),
      .Execute = [](ArchetypeWorld&) -> void {},
  }));
  ASSERT_TRUE(scheduler.AddSystem({
      .Name = "Velocity writer",
      .Writes = ComponentTypes<Velocity>(),
      .Execute = [](ArchetypeWorld&) -> void {},
  }));

  const auto batches = scheduler.BuildBatches();
  ASSERT_EQ(batches.size(), 3U);
  ASSERT_EQ(batches[0].size(), 1U);
  ASSERT_EQ(batches[1].size(), 1U);
  ASSERT_EQ(batches[2].size(), 2U);
  EXPECT_TRUE(SystemScheduler::Conflicts(SystemScheduler::System{.Reads = ComponentTypes<Position>()},
                                         SystemScheduler::System{.Writes = ComponentTypes<Position>()}));
  EXPECT_TRUE(SystemScheduler::Conflicts(SystemScheduler::System{.Writes = ComponentTypes<Position>()},
                                         SystemScheduler::System{.Writes = ComponentTypes<Position>()}));
  EXPECT_FALSE(SystemScheduler::Conflicts(SystemScheduler::System{.Writes = ComponentTypes<Velocity>()},
                                          SystemScheduler::System{.Reads = ComponentTypes<Position>()}));
}

TEST(SystemSchedulerTests, SynchronizesStructuralWritesBeforeLaterSystemsReadTheWorld) {
  Engine::Scene scene;
  SystemScheduler& scheduler = scene.Scheduler();
  size_t synchronizedCount = 0;
  bool observedBeforeSync = false;
  ASSERT_TRUE(scheduler.AddSystem({
      .Name = "Queue entity creation",
      .WritesStructure = true,
      .Execute = [&scene](ArchetypeWorld&) -> void {
        EXPECT_EQ(scene.EntityCount(), 0U);
        (void)scene.QueueCreate("Staged entity");
        EXPECT_EQ(scene.EntityCount(), 0U);
      },
  }));
  ASSERT_TRUE(scheduler.AddSystem({
      .Name = "Read newly synchronized entities",
      .Reads = ComponentTypes<Engine::Ecs::Name>(),
      .Execute = [&observedBeforeSync, &synchronizedCount](ArchetypeWorld& target) -> void {
        target.ForEachChunkReadOnly<Engine::Ecs::Name>(
            [&synchronizedCount](std::span<const EntityHandle> entities, std::span<const Engine::Ecs::Name>) -> void {
              synchronizedCount += entities.size();
            });
        observedBeforeSync = target.EntityCount() == 1U;
      },
  }));

  ASSERT_TRUE(scene.Update(0.0F));
  EXPECT_TRUE(observedBeforeSync);
  EXPECT_EQ(synchronizedCount, 1U);
  EXPECT_EQ(scene.EntityCount(), 1U);
}

TEST(SystemSchedulerTests, RejectsStructuralSchedulesWithoutABarrierBeforeExecution) {
  ArchetypeWorld world;
  SystemScheduler scheduler;
  bool executed = false;
  ASSERT_TRUE(scheduler.AddSystem({
      .Name = "Structural system",
      .WritesStructure = true,
      .Execute = [&executed](ArchetypeWorld&) -> void { executed = true; },
  }));

  const Status result = scheduler.Execute(world);
  EXPECT_FALSE(result);
  EXPECT_FALSE(executed);
}

TEST(SystemSchedulerTests, SceneUpdateRunsItsRegisteredEcsSystems) {
  Engine::Scene scene;
  (void)scene.QueueCreate("System target");
  auto created = scene.sync();
  ASSERT_EQ(created.size(), 1U);
  ASSERT_TRUE(created.front().Outcome);
  const EntityHandle entity = created.front().Entity;
  (void)scene.QueueAdd(entity, Position{5});
  (void)scene.QueueAdd(entity, Velocity{7});
  const auto added = scene.sync();
  ASSERT_EQ(added.size(), 2U);
  EXPECT_TRUE(added[0].Outcome);
  EXPECT_TRUE(added[1].Outcome);
  ASSERT_TRUE(scene.GetSystemScheduler().AddSystem({
      .Name = "Scene update integration",
      .Reads = ComponentTypes<Velocity>(),
      .Writes = ComponentTypes<Position>(),
      .Execute = [entity](ArchetypeWorld& world) -> void {
        world.Get<Position>(entity)->X += world.Get<Velocity>(entity)->X;
      },
  }));

  ASSERT_TRUE(scene.Update(0.0F));
  EXPECT_EQ(scene.GetArchetypeWorld().Get<Position>(entity)->X, 12);
}

TEST(SceneEcsMutationQueueTests, CommitsCreationAtTheStructuralBarrier) {
  Engine::Scene scene;
  const uint64_t sequence = scene.QueueCreateEcsEntity("Camera rig");
  EXPECT_EQ(sequence, 1U);
  EXPECT_EQ(scene.GetArchetypeWorld().EntityCount(), 0U);

  const auto results = scene.DrainStructuralCommands();
  ASSERT_EQ(results.size(), 1U);
  ASSERT_TRUE(results.front().Outcome);
  ASSERT_TRUE(results.front().EntityCreated);
  const auto entity = results.front().Entity;
  EXPECT_TRUE(scene.GetArchetypeWorld().IsAlive(entity));
  EXPECT_TRUE(scene.GetArchetypeWorld().Has<Engine::Ecs::LocalTransform>(entity));
  EXPECT_TRUE(scene.GetArchetypeWorld().Has<Engine::Ecs::Name>(entity));
  EXPECT_TRUE(scene.GetArchetypeWorld().Has<Engine::Ecs::PersistentIdentity>(entity));
  EXPECT_EQ(scene.GetEcsHierarchySnapshot().front().Name, "Camera rig");
}

TEST(SceneEcsMutationQueueTests, AppliesOwnedComponentEditsAndRejectsHierarchyCycles) {
  Engine::Scene scene;
  const uint64_t parentSequence = scene.QueueCreateEcsEntity("Parent");
  const uint64_t childSequence = scene.QueueCreateEcsEntity("Child");
  EXPECT_LT(parentSequence, childSequence);
  auto created = scene.DrainStructuralCommands();
  ASSERT_EQ(created.size(), 2U);
  ASSERT_TRUE(created[0].Outcome);
  ASSERT_TRUE(created[1].Outcome);
  const auto parent = created[0].Entity;
  const auto child = created[1].Entity;

  const uint64_t reparentSequence = scene.QueueReparentEcsEntity(child, parent);
  const uint64_t addSequence = scene.QueueAddEcsComponent(child, Engine::Ecs::Camera{});
  Engine::Ecs::Camera camera;
  camera.FieldOfViewDegrees = 72.0F;
  const uint64_t setSequence = scene.QueueSetEcsComponent(child, camera);
  EXPECT_LT(reparentSequence, addSequence);
  EXPECT_LT(addSequence, setSequence);
  auto applied = scene.DrainStructuralCommands();
  ASSERT_EQ(applied.size(), 3U);
  EXPECT_TRUE(applied[0].Outcome);
  EXPECT_TRUE(applied[1].Outcome);
  EXPECT_TRUE(applied[2].Outcome);
  ASSERT_NE(scene.GetArchetypeWorld().Get<Engine::Ecs::Camera>(child), nullptr);
  EXPECT_FLOAT_EQ(scene.GetArchetypeWorld().Get<Engine::Ecs::Camera>(child)->FieldOfViewDegrees, 72.0F);

  // NOLINTNEXTLINE(readability-suspicious-call-argument)
  const uint64_t cycleSequence = scene.QueueReparentEcsEntity(parent, child);
  EXPECT_GT(cycleSequence, setSequence);
  const auto cycleResult = scene.DrainStructuralCommands();
  ASSERT_EQ(cycleResult.size(), 1U);
  EXPECT_FALSE(cycleResult.front().Outcome);
  const auto hierarchy = scene.GetEcsHierarchySnapshot();
  ASSERT_EQ(hierarchy.size(), 2U);
  EXPECT_EQ(hierarchy[0].Handle, parent);
  EXPECT_EQ(hierarchy[1].Handle, child);
  EXPECT_EQ(hierarchy[1].Parent, parent);
}

TEST(SceneEcsMutationQueueTests, DestroyingANodeCascadesThroughItsDescendants) {
  Engine::Scene scene;
  const uint64_t firstSequence = scene.QueueCreateEcsEntity("First");
  const uint64_t containerSequence = scene.QueueCreateEcsEntity("Container");
  const uint64_t lastSequence = scene.QueueCreateEcsEntity("Last");
  EXPECT_LT(firstSequence, containerSequence);
  EXPECT_LT(containerSequence, lastSequence);
  auto roots = scene.DrainStructuralCommands();
  ASSERT_EQ(roots.size(), 3U);
  const auto first = roots[0].Entity;
  const auto container = roots[1].Entity;
  const auto last = roots[2].Entity;
  const uint64_t childSequence = scene.QueueCreateEcsEntity("Child", container);
  auto childResult = scene.DrainStructuralCommands();
  ASSERT_EQ(childResult.size(), 1U);
  const auto child = childResult.front().Entity;

  const uint64_t destroySequence = scene.QueueDestroyEcsEntity(container);
  EXPECT_GT(destroySequence, childSequence);
  const auto destroyed = scene.DrainStructuralCommands();
  ASSERT_EQ(destroyed.size(), 2U);
  ASSERT_TRUE(destroyed.front().Outcome);
  const auto hierarchy = scene.GetEcsHierarchySnapshot();
  ASSERT_EQ(hierarchy.size(), 2U);
  EXPECT_EQ(hierarchy[0].Handle, first);
  EXPECT_EQ(hierarchy[1].Handle, last);
  EXPECT_FALSE(scene.GetArchetypeWorld().IsAlive(child));
}

TEST(SceneEcsMutationQueueTests, PropagatesDirtyTransformsAndInheritedVisibility) {
  Engine::Scene scene;
  (void)scene.QueueCreateEcsEntity("Parent");
  const auto parentCreate = scene.DrainStructuralCommands();
  ASSERT_EQ(parentCreate.size(), 1U);
  ASSERT_TRUE(parentCreate.front().Outcome);
  const Engine::EntityHandle parent = parentCreate.front().Entity;

  (void)scene.QueueCreateEcsEntity("Child", parent);
  const auto childCreate = scene.DrainStructuralCommands();
  ASSERT_EQ(childCreate.size(), 1U);
  ASSERT_TRUE(childCreate.front().Outcome);
  const Engine::EntityHandle child = childCreate.front().Entity;

  Engine::Ecs::LocalTransform parentTransform;
  parentTransform.Position[0] = 10.0F;
  (void)scene.QueueSet(parent, parentTransform);
  Engine::Ecs::LocalTransform childTransform;
  childTransform.Position[0] = 2.0F;
  (void)scene.QueueSet(child, childTransform);
  (void)scene.QueueSet(parent, Engine::Ecs::LocalVisibility{.IsVisible = false});
  (void)scene.sync();

  ASSERT_TRUE(scene.Update(0.0F));
  const auto* childWorld = scene.GetArchetypeWorld().Get<Engine::Ecs::WorldTransform>(child);
  ASSERT_NE(childWorld, nullptr);
  DirectX::XMFLOAT3 childPosition{};
  DirectX::XMStoreFloat3(&childPosition, DirectX::XMVector3TransformCoord(
                                             DirectX::XMVectorZero(), DirectX::XMLoadFloat4x4(&childWorld->Matrix)));
  EXPECT_FLOAT_EQ(childPosition.x, 12.0F);
  const auto* childVisibility = scene.GetArchetypeWorld().Get<Engine::Ecs::GlobalVisibility>(child);
  ASSERT_NE(childVisibility, nullptr);
  EXPECT_FALSE(childVisibility->IsVisible);

  // The second update leaves the clean transform value unchanged.
  ASSERT_TRUE(scene.Update(0.0F));
  const auto* unchangedWorld = scene.GetArchetypeWorld().Get<Engine::Ecs::WorldTransform>(child);
  ASSERT_NE(unchangedWorld, nullptr);
  DirectX::XMStoreFloat3(&childPosition, DirectX::XMVector3TransformCoord(
                                             DirectX::XMVectorZero(), DirectX::XMLoadFloat4x4(&unchangedWorld->Matrix)));
  EXPECT_FLOAT_EQ(childPosition.x, 12.0F);
}

TEST(SceneEcsMutationQueueTests, DuplicateDestroyRequestsAreSafeDuringCascadeWaves) {
  Engine::Scene scene;
  (void)scene.QueueCreateEcsEntity("Parent");
  const auto parentCreate = scene.DrainStructuralCommands();
  ASSERT_EQ(parentCreate.size(), 1U);
  const Engine::EntityHandle parent = parentCreate.front().Entity;
  (void)scene.QueueCreateEcsEntity("Child", parent);
  const auto childCreate = scene.DrainStructuralCommands();
  ASSERT_EQ(childCreate.size(), 1U);
  const Engine::EntityHandle child = childCreate.front().Entity;

  (void)scene.QueueDestroy(parent);
  (void)scene.QueueDestroy(child);
  const auto results = scene.sync();
  ASSERT_EQ(results.size(), 3U);
  EXPECT_TRUE(std::ranges::all_of(results, [](const Engine::Scene::SyncResult& result) -> bool {
    return result.Outcome.has_value();
  }));
  EXPECT_FALSE(scene.GetArchetypeWorld().IsAlive(parent));
  EXPECT_FALSE(scene.GetArchetypeWorld().IsAlive(child));
}
} // namespace Engine::Tests
