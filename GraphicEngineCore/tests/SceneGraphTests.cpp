#include "scene/include/Object.h"
#include "scene/include/Scene.h"
#include "scene/include/SceneDocument.h"
#include "rendering/include/renderer/Entity.h"

#include <DirectXMath.h>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <exception>
#include <iostream>
#include <memory>

namespace
{
void Require(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

auto WorldPosition(const Engine::Object &object) -> DirectX::XMFLOAT3
{
    DirectX::XMFLOAT3 position{};
    DirectX::XMStoreFloat3(
        &position, DirectX::XMVector3TransformCoord(DirectX::XMVectorZero(), object.transform.GetWorldMat()));
    return position;
}

void TestHierarchyAndWorldTransforms()
{
    Engine::Object root;
    auto parent = std::make_shared<Engine::Object>();
    auto child = std::make_shared<Engine::Object>();
    auto grandchild = std::make_shared<Engine::Object>();
    parent->transform.SetPosition(10.0F, 0.0F, 0.0F);
    parent->transform.SetScale(2.0F, 2.0F, 2.0F);
    child->transform.SetPosition(1.0F, 0.0F, 0.0F);
    grandchild->transform.SetPosition(1.0F, 0.0F, 0.0F);
    Require(root.AddChild(parent), "root must accept a child");
    Require(parent->AddChild(child), "parent must accept a child");
    Require(child->AddChild(grandchild), "child must accept a grandchild");
    Require(!child->AddChild(parent), "hierarchy must reject cycles");
    Require(!parent->AddChild(child), "hierarchy must reject duplicate membership");
    const auto initial = WorldPosition(*grandchild);
    Require(std::abs(initial.x - 14.0F) < 1.0e-4F,
            "nested local transforms must compose in row-vector order");
    parent->transform.SetPosition(20.0F, 0.0F, 0.0F);
    const auto afterAncestorEdit = WorldPosition(*grandchild);
    Require(std::abs(afterAncestorEdit.x - 24.0F) < 1.0e-4F,
            "ancestor edits must invalidate descendant world-transform caches immediately");
    Require(child->SetParent(&root), "valid reparenting must succeed");
    const auto afterReparent = WorldPosition(*child);
    Require(std::abs(afterReparent.x - 1.0F) < 1.0e-4F, "reparenting must preserve local transform values");
    Require(grandchild->GetParent() == child.get(), "reparenting a node must preserve its subtree");
    Require(root.RemoveChild(child.get()), "parent removal must detach the child");
    Require(child->GetParent() == nullptr, "removed child must have no stale parent pointer");
    Require(grandchild->GetParent() == child.get(), "removing a node must keep its owned branch intact");

    auto rotatingParent = std::make_shared<Engine::Object>();
    auto rotatedChild = std::make_shared<Engine::Object>();
    rotatingParent->transform.SetRotation(0.0F, 0.0F, DirectX::XM_PIDIV2);
    rotatedChild->transform.SetPosition(2.0F, 0.0F, 0.0F);
    Require(root.AddChild(rotatingParent) && rotatingParent->AddChild(rotatedChild),
            "rotated parent hierarchy must be constructible");
    const auto rotatedPosition = WorldPosition(*rotatedChild);
    Require(std::abs(rotatedPosition.x) < 1.0e-4F && std::abs(rotatedPosition.y - 2.0F) < 1.0e-4F,
            "parent rotation must propagate to child world position");
    rotatingParent->transform.SetRotation(0.0F, 0.0F, 0.0F);
    const auto updatedRotationPosition = WorldPosition(*rotatedChild);
    Require(std::abs(updatedRotationPosition.x - 2.0F) < 1.0e-4F &&
                std::abs(updatedRotationPosition.y) < 1.0e-4F,
            "parent rotation edits must invalidate cached child world transforms immediately");
}

void TestParentDestructionDetachesChildren()
{
    auto parent = std::make_shared<Engine::Object>();
    auto child = std::make_shared<Engine::Object>();
    Require(parent->AddChild(child), "parent must accept its child");
    parent.reset();
    Require(child->GetParent() == nullptr, "destroying a parent must clear child parent pointers");
    Require(child->transform.GetParent() == nullptr, "destroying a parent must clear transform links");
}

void TestSceneRegistryKeepsHierarchyConsistent()
{
    Engine::Scene scene;
    auto parent = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
    auto child = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
    const Engine::EntityId parentId = scene.AddEntity(parent, 101);
    const Engine::EntityId childId = scene.AddEntity(child, 202);
    Require(parentId == 101 && childId == 202, "scene registry must preserve stable entity IDs");
    Require(child->SetParent(parent.get()), "registered entities must support hierarchical reparenting");
    scene.RemoveEntity(parentId);
    Require(scene.FindEntity(childId) == child && child->GetParent() == &scene.GetRoot(),
            "removing a registered parent must promote registered children to the scene root");
    Require(scene.GetRoot().GetChildren().size() == 1,
            "scene root membership must match the entity registry");
}

void TestSceneDocumentHierarchyRoundTrip()
{
    const auto path = std::filesystem::temp_directory_path() / "engine_scene_graph_round_trip.json";
    Engine::SceneDocument document;
    Engine::SceneEntityDocument parent;
    parent.id = 10;
    parent.primitiveType = "cube";
    Engine::SceneEntityDocument child;
    child.id = 20;
    child.parentId = 10;
    child.primitiveType = "sphere";
    child.position = {1.0F, 2.0F, 3.0F};
    document.entities = {parent, child};
    Engine::SceneSerializer serializer;
    std::string error;
    Require(serializer.SaveAtomic(path, document, error), "hierarchy document must save");
    const auto loaded = serializer.Load(path);
    Require(static_cast<bool>(loaded), "hierarchy document must load");
    Require(loaded.document.entities.size() == 2 && loaded.document.entities.at(1).parentId == 10,
            "hierarchy relationships must round-trip");
    std::filesystem::remove(path);

    const auto flatPath = std::filesystem::temp_directory_path() / "engine_scene_graph_flat.json";
    {
        std::ofstream output(flatPath);
        output << R"({"schemaVersion":1,"entities":[{"id":3,"primitiveType":"cube"}]})";
    }
    const auto flat = serializer.Load(flatPath);
    Require(static_cast<bool>(flat) && !flat.document.entities.front().parentId,
            "legacy flat scenes must load entities at the root");
    std::filesystem::remove(flatPath);

    const auto malformedPath = std::filesystem::temp_directory_path() / "engine_scene_graph_malformed.json";
    {
        std::ofstream output(malformedPath);
        output
            << R"({"schemaVersion":1,"entities":[{"id":1,"parentId":2,"primitiveType":"cube"},{"id":2,"parentId":1,"primitiveType":"sphere"},{"id":3,"parentId":99,"primitiveType":"plane"}]})";
    }
    const auto malformed = serializer.Load(malformedPath);
    Require(static_cast<bool>(malformed) && !malformed.warnings.empty(),
            "malformed hierarchy data must be recovered with warnings");
    Require(!malformed.document.entities.at(2).parentId,
            "an entity with a missing parent must be recovered at the root");
    Require(!malformed.document.entities.at(0).parentId || !malformed.document.entities.at(1).parentId,
            "a cyclic hierarchy must be broken at the root");
    std::filesystem::remove(malformedPath);
}
} // namespace

auto RunTests() -> int
{
    try
    {
        TestHierarchyAndWorldTransforms();
        TestParentDestructionDetachesChildren();
        TestSceneRegistryKeepsHierarchyConsistent();
        TestSceneDocumentHierarchyRoundTrip();
        return EXIT_SUCCESS;
    }
    catch (...)
    {
        std::cerr << "scene graph test failed with an unexpected exception\n";
        return EXIT_FAILURE;
    }
}

auto main() -> int
{
    try
    {
        return RunTests();
    }
    catch (...)
    {
        return EXIT_FAILURE;
    }
}
