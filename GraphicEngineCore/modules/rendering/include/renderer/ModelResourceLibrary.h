#pragma once

#include "rendering/include/renderer/RenderResources.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine::Renderer {

// GPU resources are indexed by asset identity and kept outside scene storage.
class ModelResourceLibrary {
public:
  void Register(std::string key, const std::vector<Submesh>& submeshes) {
    m_Submeshes.insert_or_assign(std::move(key), submeshes);
  }

  [[nodiscard]] auto Find(std::string_view key) const noexcept -> const std::vector<Submesh>* {
    const auto found = m_Submeshes.find(std::string(key));
    return found == m_Submeshes.end() ? nullptr : &found->second;
  }

private:
  std::unordered_map<std::string, std::vector<Submesh>> m_Submeshes;
};

} // namespace Engine::Renderer
