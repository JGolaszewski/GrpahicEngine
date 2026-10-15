#include "rendering/include/renderer/Camera.h"
#include "rendering/include/renderer/Entity.h"
#include "rendering/include/renderer/Light.h"
#include "scene/include/Scene.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>

namespace
{
TEST(SceneLifecycleTests, RemovesEntitiesByIdAndPointerAndKeepsStableIndices)
{
    Engine::Scene scene;
    auto first = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
    auto middle = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
    auto last = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
    ASSERT_EQ(scene.AddEntity(first), 1U);
    ASSERT_EQ(scene.AddEntity(middle), 2U);
    ASSERT_EQ(scene.AddEntity(last), 3U);

    scene.RemoveEntity(static_cast<Engine::Renderer::Entity *>(nullptr));
    scene.RemoveEntity(999U);
    EXPECT_EQ(scene.GetEntityId(static_cast<Engine::Renderer::Entity *>(nullptr)), Engine::InvalidEntityId);
    EXPECT_EQ(scene.FindEntity(999U), nullptr);
    EXPECT_EQ(scene.GetEntityIdAt(99), Engine::InvalidEntityId);

    scene.RemoveEntity(2U);
    EXPECT_EQ(scene.GetEntityCount(), 2U);
    EXPECT_EQ(scene.GetEntityIdAt(0), 1U);
    EXPECT_EQ(scene.GetEntityIdAt(1), 3U);
    EXPECT_EQ(scene.FindEntity(3U), last);
    scene.RemoveEntity(first.get());
    EXPECT_EQ(scene.GetEntityCount(), 1U);
    EXPECT_EQ(scene.GetEntityIdAt(0), 3U);
    EXPECT_EQ(scene.AddEntity(std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr)), 4U);

    EXPECT_EQ(scene.AddEntity(nullptr), Engine::InvalidEntityId);
    EXPECT_EQ(scene.AddEntity(std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr),
                              std::numeric_limits<Engine::EntityId>::max()),
              Engine::InvalidEntityId);
}

TEST(SceneLifecycleTests, RemovesLightsAndCamerasByIdAndPointer)
{
    Engine::Scene scene;
    auto firstLight = std::make_shared<Engine::Renderer::SpotLight>();
    auto middleLight = std::make_shared<Engine::Renderer::SpotLight>();
    auto lastLight = std::make_shared<Engine::Renderer::SpotLight>();
    ASSERT_EQ(scene.AddLight(firstLight), 1U);
    ASSERT_EQ(scene.AddLight(middleLight), 2U);
    ASSERT_EQ(scene.AddLight(lastLight), 3U);
    scene.RemoveLight(static_cast<Engine::Renderer::SpotLight *>(nullptr));
    scene.RemoveLight(99U);
    EXPECT_EQ(scene.GetLightId(static_cast<Engine::Renderer::SpotLight *>(nullptr)), Engine::InvalidLightId);
    EXPECT_EQ(scene.FindLight(99U), nullptr);
    EXPECT_EQ(scene.GetLightIdAt(99), Engine::InvalidLightId);
    scene.RemoveLight(2U);
    scene.RemoveLight(firstLight.get());
    EXPECT_EQ(scene.GetLights().size(), 1U);
    EXPECT_EQ(scene.GetLightIdAt(0), 3U);
    EXPECT_EQ(scene.FindLight(3U), lastLight);
    EXPECT_EQ(scene.AddLight(nullptr), Engine::InvalidLightId);
    EXPECT_EQ(scene.AddLight(std::make_shared<Engine::Renderer::SpotLight>(),
                             std::numeric_limits<Engine::LightId>::max()),
              Engine::InvalidLightId);

    auto firstCamera = std::make_shared<Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    auto middleCamera = std::make_shared<Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    auto lastCamera = std::make_shared<Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f);
    ASSERT_EQ(scene.AddCamera(firstCamera), 1U);
    ASSERT_EQ(scene.AddCamera(middleCamera), 2U);
    ASSERT_EQ(scene.AddCamera(lastCamera), 3U);
    scene.RemoveCamera(static_cast<Engine::Renderer::Camera *>(nullptr));
    scene.RemoveCamera(99U);
    EXPECT_EQ(scene.GetCameraId(static_cast<Engine::Renderer::Camera *>(nullptr)), Engine::InvalidCameraId);
    EXPECT_EQ(scene.FindCamera(99U), nullptr);
    EXPECT_EQ(scene.GetCameraIdAt(99), Engine::InvalidCameraId);
    EXPECT_EQ(scene.GetCamera(99), nullptr);
    EXPECT_EQ(scene.GetCamera(0), firstCamera.get());
    scene.RemoveCamera(2U);
    scene.RemoveCamera(firstCamera.get());
    EXPECT_EQ(scene.GetCameras().size(), 1U);
    EXPECT_EQ(scene.GetCameraIdAt(0), 3U);
    EXPECT_EQ(scene.FindCamera(3U), lastCamera);
    EXPECT_EQ(scene.AddCamera(nullptr), Engine::InvalidCameraId);
    EXPECT_EQ(scene.AddCamera(std::make_shared<Engine::Renderer::Camera>(DirectX::XM_PIDIV4, 0.1f, 100.0f),
                              std::numeric_limits<Engine::CameraId>::max()),
              Engine::InvalidCameraId);
    scene.Update(1.0f / 60.0f);
}

TEST(SceneLifecycleTests, SearchesForwardForSpawnSpaceAndHonorsAttemptLimits)
{
    Engine::Scene scene;
    auto occupied = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
    occupied->transform.Position = {0.0f, 4.0f, 0.0f};
    ASSERT_NE(scene.AddEntity(occupied), Engine::InvalidEntityId);

    const DirectX::XMFLOAT3 origin{0.0f, 4.0f, 0.0f};
    DirectX::XMFLOAT3 candidate = scene.FindFreeSpawnPosition(origin, 2.0f, 2.5f, 8);
    EXPECT_FLOAT_EQ(candidate.x, 2.5f);
    EXPECT_FLOAT_EQ(candidate.y, origin.y);

    auto second = std::make_shared<Engine::Renderer::Entity>(nullptr, nullptr);
    second->transform.Position = candidate;
    ASSERT_NE(scene.AddEntity(second), Engine::InvalidEntityId);
    candidate = scene.FindFreeSpawnPosition(origin, 2.0f, 2.5f, 8);
    EXPECT_FLOAT_EQ(candidate.x, 5.0f);

    EXPECT_EQ(scene.FindFreeSpawnPosition(origin, 2.0f, 2.5f, 0).x, origin.x);
    EXPECT_EQ(scene.FindFreeSpawnPosition(origin, 0.0f, -5.0f, 8).x, origin.x);
    EXPECT_EQ(scene.FindFreeSpawnPosition({10.0f, 4.0f, 0.0f}, 2.0f, 2.5f, 8).x, 10.0f);
}
} // namespace
