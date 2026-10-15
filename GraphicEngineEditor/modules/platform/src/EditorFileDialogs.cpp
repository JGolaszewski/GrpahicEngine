#include "platform/include/EditorFileDialogs.h"

#define NOMINMAX
#include <windows.h>

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <array>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace EditorPlatform {
namespace {
auto CALLBACK InitializeFolderDialog(HWND window, UINT message, LPARAM /*unused*/, LPARAM initialPath) -> int {
  if (message == BFFM_INITIALIZED && (initialPath != 0)) {
    SendMessageW(window, BFFM_SETSELECTIONW, TRUE, initialPath);
  }
  return 0;
}

auto ProjectPreferencePath() -> std::filesystem::path {
  const DWORD bufferSize = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
  if (bufferSize == 0) {
    return {};
  }

  std::wstring appData(bufferSize, L'\0');
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", appData.data(), bufferSize);
  if (length == 0 || length >= bufferSize) {
    return {};
  }

  appData.resize(length);
  return std::filesystem::path(appData) / L"GraphicEngine" / L"project.txt";
}
} // namespace

auto ChooseSceneFile(bool save, const std::filesystem::path& initialPath, const std::filesystem::path& projectRoot)
    -> std::optional<std::filesystem::path> {
  std::vector<wchar_t> filename(32768);
  if (!initialPath.empty()) {
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
  return (accepted != 0) ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

auto ChooseModelFile(const std::filesystem::path& projectRoot) -> std::optional<std::filesystem::path> {
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
  return (GetOpenFileNameW(&dialog) != 0) ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

auto ChooseTextureFile(const std::filesystem::path& projectRoot) -> std::optional<std::filesystem::path> {
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
  return (GetOpenFileNameW(&dialog) != 0) ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

auto ChooseLogExportFile(const std::filesystem::path& initialPath) -> std::optional<std::filesystem::path> {
  std::vector<wchar_t> filename(32768);
  if (!initialPath.empty()) {
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
  return (GetSaveFileNameW(&dialog) != 0) ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

auto OpenSourceFile(const std::filesystem::path& path) -> bool {
  std::error_code error;
  if (path.empty() || !std::filesystem::exists(path, error) || error) {
    return false;
  }
  const std::wstring sourcePath = path.wstring();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // ShellExecuteW documents values <= 32 as errors.
  return reinterpret_cast<INT_PTR>(
             ShellExecuteW(nullptr, L"open", sourcePath.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

auto ChooseProjectFolder(const std::filesystem::path& initialPath) -> std::optional<std::filesystem::path> {
  const auto initial = initialPath.wstring();
  BROWSEINFOW dialog{};
  dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  dialog.lpszTitle = L"Choose a GraphicEngine project folder";
  dialog.lpfn = InitializeFolderDialog;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast) // BROWSEINFO passes this borrowed path through LPARAM.
  dialog.lParam = reinterpret_cast<LPARAM>(initial.c_str());
  PIDLIST_ABSOLUTE selection = SHBrowseForFolderW(&dialog);
  if (selection == nullptr) {
    return std::nullopt;
  }
  std::array<wchar_t, MAX_PATH> path{};
  const bool selected = SHGetPathFromIDListW(selection, path.data()) != FALSE;
  CoTaskMemFree(selection);
  return selected ? std::optional(std::filesystem::path(path.data())) : std::nullopt;
}

auto LoadRememberedProjectRoot() -> std::filesystem::path {
  const auto preference = ProjectPreferencePath();
  if (preference.empty()) {
    return std::filesystem::current_path();
  }
  std::wifstream input(preference);
  std::wstring saved;
  if (input && std::getline(input, saved) && !saved.empty()) {
    return {saved};
  }
  return std::filesystem::current_path();
}

void RememberProjectRoot(const std::filesystem::path& root) {
  const auto preference = ProjectPreferencePath();
  if (preference.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(preference.parent_path(), ec);
  if (ec) {
    return;
  }
  std::wofstream output(preference, std::ios::trunc);
  if (output) {
    output << root.native() << L'\n';
  }
}
} // namespace EditorPlatform
