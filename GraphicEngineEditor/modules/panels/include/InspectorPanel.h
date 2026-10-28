#pragma once

#include "assets/include/AssetManager.h"
#include "events/include/AssetRegistryChangedEvent.h"
#include "events/include/AssetWorkflowFeedbackEvent.h"
#include "events/include/EcsComponentMutationRequestedEvent.h"
#include "events/include/EcsEntitySelectedEvent.h"
#include "events/include/EntityDeselectedEvent.h"
#include "events/include/SceneChangedEvent.h"
#include "panels/include/EditorPanel.h"
#include "scene/include/Scene.h"
#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4251) // The inference service uses a private PIMPL behind its exported API.
#endif
#include "runtime/include/PrismNdfInferenceService.h"
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct PrismNdfGenerationAdapter {
  std::function<Engine::Result<uint64_t>(std::string, bool)> start;
  std::function<std::optional<Engine::PrismNdfInferenceStatus>(uint64_t)> poll;
  std::function<void(uint64_t)> cancel;
  std::function<bool()> isAvailable;
};

class InspectorPanel final : public EditorPanel {
public:
  explicit InspectorPanel(PrismNdfGenerationAdapter generation = {});
  ~InspectorPanel() override;
  void Draw() override;
  void OnEvent(Engine::Events::Event& event) override;

private:
  void DrawEntityInspector();
  void DrawPrismNdfEditor(Engine::EntityHandle entity, const std::string& materialGuid,
                          Engine::SceneMaterialDocument& material);
  void ResetPrismNdfEditor(bool cancelRequest);
  void PollPrismGeneration();
  std::shared_ptr<Engine::Scene> m_Scene;
  Engine::EntityHandle m_SelectedEntity = Engine::InvalidEntityHandle;
  std::array<char, 64> m_AddComponentFilter{};
  Engine::EntityHandle m_CustomResolutionEntity = Engine::InvalidEntityHandle;
  Engine::EntityHandle m_MeshSourceSelectionEntity = Engine::InvalidEntityHandle;
  int m_MeshSourceSelectionIndex = 0;
  bool m_LockTransformScale = false;
  std::vector<Engine::AssetManager::ModelAssetInfo> m_Models;
  std::vector<Engine::AssetManager::TextureAssetInfo> m_Textures;
  std::vector<Engine::AssetManager::MaterialAssetInfo> m_Materials;
  std::vector<Engine::AssetManager::EnvironmentAssetInfo> m_Environments;
  std::string m_AssetFeedback;
  bool m_AssetFeedbackSucceeded = true;
  PrismNdfGenerationAdapter m_PrismGeneration;
  std::array<char, 1024> m_PrismPrompt{};
  bool m_PrismDirectional = false;
  bool m_PrismEditorRequested = false;
  Engine::EntityHandle m_PrismEditorEntity = Engine::InvalidEntityHandle;
  std::string m_PrismMaterialGuid;
  std::optional<uint64_t> m_PrismRequestId;
  Engine::EntityHandle m_PrismRequestEntity = Engine::InvalidEntityHandle;
  std::string m_PrismRequestMaterialGuid;
  std::string m_PrismRequestPrompt;
  bool m_PrismRequestDirectional = false;
  bool m_PrismRequestEligible = false;
  std::optional<Engine::PrismNdfInferenceStatus> m_PrismStatus;
  std::shared_ptr<const Engine::PrismNdfData> m_PrismPreview;
  std::array<float, Engine::PrismNdfSampleCount> m_PrismPreviewHeatmap{};
  std::array<float, 64U * 64U> m_PrismPreviewSphere{};
  float m_PrismPreviewMaximum = 0.0F;
  float m_PrismPreviewSphereMaximum = 0.0F;
  bool m_PrismPreviewDirty = false;
  std::string m_PrismPreviewPrompt;
  bool m_PrismPreviewDirectional = false;
  bool m_PrismPreviewFromSaved = false;
};
