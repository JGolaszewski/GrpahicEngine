#include "platform/include/EditorFileDialogs.h"

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <fstream>
#include <system_error>
#include <string>
#include <vector>

namespace EditorPlatform
{
namespace
{
int CALLBACK InitializeFolderDialog(HWND window, UINT message, LPARAM, LPARAM initialPath)
{
    if (message == BFFM_INITIALIZED && initialPath)
        SendMessageW(window, BFFM_SETSELECTIONW, TRUE, initialPath);
    return 0;
}

std::filesystem::path ProjectPreferencePath()
{
    const DWORD bufferSize = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (bufferSize == 0)
        return {};

    std::wstring appData(bufferSize, L'\0');
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", appData.data(), bufferSize);
    if (length == 0 || length >= bufferSize)
        return {};

    appData.resize(length);
    return std::filesystem::path(appData) / L"GraphicEngine" / L"project.txt";
}
} // namespace

std::optional<std::filesystem::path> ChooseSceneFile(bool save, const std::filesystem::path &initialPath,
                                                     const std::filesystem::path &projectRoot)
{
    std::vector<wchar_t> filename(32768);
    if (!initialPath.empty())
    {
        const auto initial = initialPath.wstring();
        wcsncpy_s(filename.data(), filename.size(), initial.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrFilter =
        L"Scene files (*.scene.json)\0*.scene.json\0JSON files (*.json)\0*.json\0All files (*.*)\0*.*\0\0";
    dialog.nFilterIndex = 1;
    const auto initialDirectory = (initialPath.empty() ? projectRoot : initialPath.parent_path()).wstring();
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    dialog.lpstrDefExt = L"json";
    const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    return accepted ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

std::optional<std::filesystem::path> ChooseModelFile(const std::filesystem::path &projectRoot)
{
    std::vector<wchar_t> filename(32768);
    const auto initialDirectory = projectRoot.wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrFilter = L"glTF models (*.gltf;*.glb)\0*.gltf;*.glb\0\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

std::optional<std::filesystem::path> ChooseTextureFile(const std::filesystem::path &projectRoot)
{
    std::vector<wchar_t> filename(32768);
    const auto initialDirectory = projectRoot.wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0\0";
    dialog.nFilterIndex = 1;
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&dialog) ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

std::optional<std::filesystem::path> ChooseLogExportFile(const std::filesystem::path &initialPath)
{
    std::vector<wchar_t> filename(32768);
    if (!initialPath.empty())
    {
        const auto initial = initialPath.wstring();
        wcsncpy_s(filename.data(), filename.size(), initial.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.lpstrFilter = L"JSONL logs (*.jsonl)\0*.jsonl\0All files (*.*)\0*.*\0\0";
    dialog.nFilterIndex = 1;
    const auto initialDirectory =
        initialPath.empty() ? std::filesystem::current_path().wstring() : initialPath.parent_path().wstring();
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    dialog.lpstrDefExt = L"jsonl";
    return GetSaveFileNameW(&dialog) ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

bool OpenSourceFile(const std::filesystem::path &path)
{
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error) || error)
        return false;
    const std::wstring sourcePath = path.wstring();
    return reinterpret_cast<INT_PTR>(
               ShellExecuteW(nullptr, L"open", sourcePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

std::optional<std::filesystem::path> ChooseProjectFolder(const std::filesystem::path &initialPath)
{
    const auto initial = initialPath.wstring();
    BROWSEINFOW dialog{};
    dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    dialog.lpszTitle = L"Choose a GraphicEngine project folder";
    dialog.lpfn = InitializeFolderDialog;
    dialog.lParam = reinterpret_cast<LPARAM>(initial.c_str());
    PIDLIST_ABSOLUTE selection = SHBrowseForFolderW(&dialog);
    if (!selection)
        return std::nullopt;
    wchar_t path[MAX_PATH]{};
    const bool selected = SHGetPathFromIDListW(selection, path) != FALSE;
    CoTaskMemFree(selection);
    return selected ? std::optional(std::filesystem::path(path)) : std::nullopt;
}

std::filesystem::path LoadRememberedProjectRoot()
{
    const auto preference = ProjectPreferencePath();
    if (preference.empty())
        return std::filesystem::current_path();
    std::wifstream input(preference);
    std::wstring saved;
    if (input && std::getline(input, saved) && !saved.empty())
        return std::filesystem::path(saved);
    return std::filesystem::current_path();
}

void RememberProjectRoot(const std::filesystem::path &root)
{
    const auto preference = ProjectPreferencePath();
    if (preference.empty())
        return;
    std::error_code ec;
    std::filesystem::create_directories(preference.parent_path(), ec);
    if (ec)
        return;
    std::wofstream output(preference, std::ios::trunc);
    if (output)
        output << root.native() << L'\n';
}
} // namespace EditorPlatform
