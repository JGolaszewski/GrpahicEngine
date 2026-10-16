#pragma once

#include "rendering/include/renderer/RenderResources.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine::Renderer {

/// Stores GPU submesh resources by asset identity, outside scene storage.
class ModelResourceLibrary {
public:
  /// Replaces the registered submeshes for an asset identity.
  /// @param key Stable asset identity.
  /// @param submeshes Meshes and materials to associate with the identity.
  void Register(std::string key, const std::vector<Submesh>& submeshes) {
    m_Submeshes.insert_or_assign(std::move(key), submeshes);
  }

  /// Finds the submeshes registered for an asset identity.
  /// @param key Stable asset identity to look up.
  /// @return A pointer to the stored submeshes, or null when the key is absent.
  [[nodiscard]] auto Find(std::string_view key) const noexcept -> const std::vector<Submesh>* {
    const auto found = m_Submeshes.find(std::string(key));
    return found == m_Submeshes.end() ? nullptr : &found->second;
  }

private:
  std::unordered_map<std::string, std::vector<Submesh>> m_Submeshes;
};

} // namespace Engine::Renderer
