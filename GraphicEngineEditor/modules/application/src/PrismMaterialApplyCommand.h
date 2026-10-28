#pragma once

#include "assets/include/AssetManager.h"
#include "commands/include/Command.h"
#include "panels/include/AssetDropAssignment.h"
#include "runtime/include/LogMacros.h"
#include "scene/include/Scene.h"
#include "scene/include/components/EcsComponents.h"

#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

// Private application command exposed to the unit harness without constructing the UI.
class PrismMaterialApplyCommand final : public ICommand {
public:
  PrismMaterialApplyCommand(Engine::AssetManager& assets, std::shared_ptr<Engine::Scene> scene, std::string guid,
                            Engine::SceneMaterialDocument before, Engine::SceneMaterialDocument after,
                            std::function<void()> changed)
      : m_Assets(assets), m_Scene(std::move(scene)), m_Guid(std::move(guid)), m_Before(std::move(before)),
        m_After(std::move(after)), m_Changed(std::move(changed)) {}

  auto Execute() -> bool override { return Apply(m_After, m_Before); }
  auto Undo() -> bool override { return Apply(m_Before, m_After); }

private:
  auto Apply(const Engine::SceneMaterialDocument& target, const Engine::SceneMaterialDocument& rollback) -> bool {
    if (m_Scene == nullptr) {
      return false;
    }
    std::string error;
    if (!m_Assets.SaveMaterial(m_Guid, target, error)) {
      return false;
    }
    std::vector<std::pair<Engine::EntityHandle, Engine::Ecs::MeshRenderer>> updates;
    m_Scene->World().ForEachChunkReadOnly<Engine::Ecs::MeshRenderer>(
        [&updates, this, &target](std::span<const Engine::EntityHandle> entities,
                                  std::span<const Engine::Ecs::MeshRenderer> meshes) -> void {
          for (size_t index = 0; index < entities.size(); ++index) {
            if (meshes[index].MaterialGuid == m_Guid) {
              auto updated = meshes[index];
              if (ApplyMaterialAssetEdit(updated, m_Guid, target)) {
                updates.emplace_back(entities[index], std::move(updated));
              }
            }
          }
        });
    std::vector<std::pair<Engine::EntityHandle, Engine::Ecs::MeshRenderer>> applied;
    for (auto& [entity, updated] : updates) {
      auto* current = m_Scene->World().Get<Engine::Ecs::MeshRenderer>(entity);
      if (current == nullptr) {
        error = "A shared material user was deleted during the transaction";
        break;
      }
      Engine::Ecs::MeshRenderer previous = *current;
      const Engine::Status status = m_Scene->SetInPlace(entity, std::move(updated));
      if (!status) {
        error = status.error().Message;
        break;
      }
      applied.emplace_back(entity, std::move(previous));
    }
    if (!error.empty()) {
      for (auto& [entity, previous] : applied) {
        const Engine::Status rollbackStatus = m_Scene->SetInPlace(entity, std::move(previous));
        if (!rollbackStatus) {
          ENGINE_LOG_ERROR("Asset", "PRISM scene rollback failed | reason: {}", rollbackStatus.error().Message);
        }
      }
      std::string rollbackError;
      if (!m_Assets.SaveMaterial(m_Guid, rollback, rollbackError)) {
        ENGINE_LOG_ERROR("Asset", "PRISM material transaction rollback failed | reason: {}", rollbackError);
      }
      return false;
    }
    if (m_Changed) {
      m_Changed();
    }
    return true;
  }

  Engine::AssetManager& m_Assets;
  std::shared_ptr<Engine::Scene> m_Scene;
  std::string m_Guid;
  Engine::SceneMaterialDocument m_Before;
  Engine::SceneMaterialDocument m_After;
  std::function<void()> m_Changed;
};
