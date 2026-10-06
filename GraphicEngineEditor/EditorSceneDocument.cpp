#include "EditorSceneDocument.h"

#include "AssetManager.h"
#include "SceneDocument.h"

#include <system_error>

std::filesystem::path EditorSceneDocument::Canonicalize(const std::filesystem::path &path)
{
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

Engine::SceneDocumentResult EditorSceneDocument::Load(const Engine::AssetManager &assets,
                                                      const std::filesystem::path &path)
{
    return assets.LoadScene(path);
}

Engine::Status EditorSceneDocument::Save(const Engine::AssetManager &assets,
                                         const Engine::SceneDocument &document,
                                         const std::filesystem::path &path)
{
    std::string error;
    if (!assets.SaveScene(path, document, error))
    {
        return std::unexpected(Engine::MakeError(std::move(error)));
    }
    m_Path = Canonicalize(path);
    return {};
}
