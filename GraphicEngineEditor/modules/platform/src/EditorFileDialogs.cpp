#include "platform/include/EditorFileDialogs.h"
#include "runtime/include/LogMacros.h"

#define NOMINMAX
#include <windows.h>

#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cmath>
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

auto EditorPreferencePath() -> std::filesystem::path {
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
  return std::filesystem::path(appData) / L"GraphicEngine" / L"editor-settings.json";
}

auto LegacyEditorPreferencePath() -> std::filesystem::path {
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
  return std::filesystem::path(appData) / L"GraphicEngine" / L"editor-settings.txt";
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

auto ChooseRenderOutputFile(bool exr, const std::filesystem::path& initialPath)
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
  dialog.lpstrFilter = exr ? L"OpenEXR images (*.exr)\0*.exr\0All files (*.*)\0*.*\0\0"
                           : L"PNG images (*.png)\0*.png\0All files (*.*)\0*.*\0\0";
  dialog.nFilterIndex = 1;
  const auto initialDirectory =
      initialPath.empty() ? std::filesystem::current_path().wstring() : initialPath.parent_path().wstring();
  dialog.lpstrInitialDir = initialDirectory.c_str();
  dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
  dialog.lpstrDefExt = exr ? L"exr" : L"png";
  return GetSaveFileNameW(&dialog) != 0 ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

auto ChooseBrdfAnalysisFile(bool json, const std::filesystem::path& initialPath)
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
  dialog.lpstrFilter = json ? L"JSON analysis (*.json)\0*.json\0All files (*.*)\0*.*\0\0"
                            : L"PNG chart (*.png)\0*.png\0All files (*.*)\0*.*\0\0";
  dialog.nFilterIndex = 1;
  const auto initialDirectory =
      initialPath.empty() ? std::filesystem::current_path().wstring() : initialPath.parent_path().wstring();
  dialog.lpstrInitialDir = initialDirectory.c_str();
  dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
  dialog.lpstrDefExt = json ? L"json" : L"png";
  return GetSaveFileNameW(&dialog) != 0 ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

auto ChooseAnalyticalFrameFile(AnalyticalFrameFileOperation operation, const std::filesystem::path& initialPath)
    -> std::optional<std::filesystem::path> {
  using Operation = AnalyticalFrameFileOperation;
  std::vector<wchar_t> filename(32768);
  if (!initialPath.empty()) {
    const auto initial = initialPath.wstring();
    wcsncpy_s(filename.data(), filename.size(), initial.c_str(), _TRUNCATE);
  }
  OPENFILENAMEW dialog{};
  dialog.lStructSize = sizeof(dialog);
  dialog.lpstrFile = filename.data();
  dialog.nMaxFile = static_cast<DWORD>(filename.size());
  dialog.lpstrFilter = operation == Operation::ExportChart ? L"PNG chart (*.png)\0*.png\0All files (*.*)\0*.*\0\0"
                       : operation == Operation::ExportMeasurements
                           ? L"JSON measurements (*.json)\0*.json\0All files (*.*)\0*.*\0\0"
                           : L"Analytical Frame project (*.aframe)\0*.aframe\0All files (*.*)\0*.*\0\0";
  dialog.nFilterIndex = 1;
  const auto initialDirectory =
      initialPath.empty() ? std::filesystem::current_path().wstring() : initialPath.parent_path().wstring();
  dialog.lpstrInitialDir = initialDirectory.c_str();
  dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
                 ((operation == Operation::OpenArchive) ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
  dialog.lpstrDefExt = operation == Operation::ExportChart          ? L"png"
                       : operation == Operation::ExportMeasurements ? L"json"
                                                                    : L"aframe";
  const BOOL selected = (operation == Operation::OpenArchive) ? GetOpenFileNameW(&dialog) : GetSaveFileNameW(&dialog);
  return selected != 0 ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
}

auto ChooseSnapshotFile(bool exr, bool save, const std::filesystem::path& initialPath)
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
  dialog.lpstrFilter = exr ? L"OpenEXR snapshot (*.exr)\0*.exr\0All files (*.*)\0*.*\0\0"
                           : L"Snapshot workspace (*.aframe)\0*.aframe\0All files (*.*)\0*.*\0\0";
  dialog.nFilterIndex = 1;
  const auto initialDirectory =
      initialPath.empty() ? std::filesystem::current_path().wstring() : initialPath.parent_path().wstring();
  dialog.lpstrInitialDir = initialDirectory.c_str();
  dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
  dialog.lpstrDefExt = exr ? L"exr" : L"aframe";
  const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
  return accepted != 0 ? std::optional(std::filesystem::path(filename.data())) : std::nullopt;
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

auto LoadEditorPreferences() -> EditorPreferences {
  const auto path = EditorPreferencePath();
  if (path.empty()) {
    ENGINE_LOG_WARN("Editor", "Could not locate the user settings directory; using default preferences");
    return {};
  }
  auto result = LoadEditorSettingsFile(path);
  if (!result.Found) {
    const auto legacyPath = LegacyEditorPreferencePath();
    std::error_code legacyPathError;
    const bool hasLegacyFile = !legacyPath.empty() && std::filesystem::exists(legacyPath, legacyPathError);
    if (legacyPathError) {
      result.Found = true;
      result.Error = "could not inspect legacy settings file: " + legacyPathError.message();
    } else if (hasLegacyFile) {
      std::wifstream legacy(legacyPath);
      int lightGizmos = 1;
      int cameraFrustums = 1;
      int wireframePlane = 1;
      float previewScale = result.Preferences.CameraPreviewScale;
      float previewQuality = result.Preferences.CameraPreviewQuality;
      if (legacy >> lightGizmos >> cameraFrustums >> wireframePlane >> previewScale >> previewQuality) {
        result.Found = true;
        result.Preferences.ShowLightGizmos = lightGizmos != 0;
        result.Preferences.ShowCameraFrustums = cameraFrustums != 0;
        result.Preferences.ShowWireframePlane = wireframePlane != 0;
        if (std::isfinite(previewScale)) {
          result.Preferences.CameraPreviewScale = std::clamp(previewScale, 0.1F, 0.8F);
        }
        if (std::isfinite(previewQuality)) {
          result.Preferences.CameraPreviewQuality = std::clamp(previewQuality, 0.05F, 1.0F);
        }
        SaveEditorPreferences(result.Preferences);
      } else {
        result.Found = true;
        result.Error = "legacy settings file could not be parsed";
      }
    }
  }
  if (!result.Error.empty()) {
    ENGINE_LOG_ERROR("Editor", "Could not load settings | path: {} | reason: {}", path.generic_string(), result.Error);
  }
  return result.Preferences;
}

void SaveEditorPreferences(const EditorPreferences& preferences) {
  const auto path = EditorPreferencePath();
  if (path.empty()) {
    return;
  }
  std::string error;
  if (!SaveEditorSettingsFile(path, preferences, error)) {
    ENGINE_LOG_ERROR("Editor", "Could not save settings | path: {} | reason: {}", path.generic_string(), error);
  }
}
} // namespace EditorPlatform
