#pragma once

#include "runtime/include/Result.h"
#include "scene/include/SceneDocument.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Engine
{
class AssetManager;
struct SceneDocument;
} // namespace Engine

class EditorSceneDocument
{
  public:
    const std::filesystem::path &GetPath() const noexcept
    {
        return m_Path;
    }
    void Reset() noexcept
    {
        m_Path.clear();
    }
    void CommitPath(const std::filesystem::path &path)
    {
        m_Path = Canonicalize(path);
    }
    Engine::SceneDocumentResult Load(const Engine::AssetManager &assets, const std::filesystem::path &path);
    Engine::Status Save(const Engine::AssetManager &assets, const Engine::SceneDocument &document,
                        const std::filesystem::path &path);

  private:
    std::filesystem::path m_Path;
    static std::filesystem::path Canonicalize(const std::filesystem::path &path);
};
