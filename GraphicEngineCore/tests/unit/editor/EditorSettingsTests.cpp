#include "platform/include/EditorSettings.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

namespace {
class TemporarySettingsDirectory final {
public:
  TemporarySettingsDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Root = std::filesystem::temp_directory_path() / ("GraphicEngineEditorSettingsTests-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Root);
  }

  TemporarySettingsDirectory(const TemporarySettingsDirectory&) = delete;
  auto operator=(const TemporarySettingsDirectory&) -> TemporarySettingsDirectory& = delete;
  ~TemporarySettingsDirectory() noexcept {
    std::error_code ignored;
    std::filesystem::remove_all(m_Root, ignored);
  }

  [[nodiscard]] auto Root() const noexcept -> const std::filesystem::path& { return m_Root; }

private:
  std::filesystem::path m_Root;
};

TEST(EditorSettingsTests, MissingFileUsesDocumentedDefaults) {
  TemporarySettingsDirectory directory;
  const auto loaded = EditorPlatform::LoadEditorSettingsFile(directory.Root() / "missing.json");

  EXPECT_FALSE(loaded.Found);
  EXPECT_TRUE(loaded.Error.empty());
  EXPECT_TRUE(loaded.Preferences.ShowLightGizmos);
  EXPECT_TRUE(loaded.Preferences.ShowCameraFrustums);
  EXPECT_TRUE(loaded.Preferences.ShowWireframePlane);
  EXPECT_FLOAT_EQ(loaded.Preferences.CameraPreviewScale, 0.35F);
  EXPECT_FLOAT_EQ(loaded.Preferences.CameraPreviewQuality, 0.25F);
  EXPECT_EQ(loaded.Preferences.FrameLimiterHz, 0U);
  EXPECT_EQ(loaded.Preferences.ShadowMapResolution, 2048U);
  EXPECT_EQ(loaded.Preferences.MaxShadowCasters, 4U);
  EXPECT_TRUE(loaded.Preferences.VSyncEnabled);
}

TEST(EditorSettingsTests, SavesCategorizedJsonAndRestoresPreferences) {
  TemporarySettingsDirectory directory;
  const auto path = directory.Root() / "nested" / "editor-settings.json";
  EditorPlatform::EditorPreferences preferences;
  preferences.ShowLightGizmos = false;
  preferences.ShowCameraFrustums = false;
  preferences.ShowWireframePlane = true;
  preferences.CameraPreviewScale = 0.7F;
  preferences.CameraPreviewQuality = 0.8F;
  preferences.FrameLimiterHz = 120;
  preferences.ShadowMapResolution = 4096;
  preferences.MaxShadowCasters = 2;
  preferences.VSyncEnabled = false;
  std::string error;

  ASSERT_TRUE(EditorPlatform::SaveEditorSettingsFile(path, preferences, error)) << error;
  std::ifstream input(path, std::ios::binary);
  ASSERT_TRUE(input);
  const std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  EXPECT_NE(json.find("\"rendering\""), std::string::npos);
  EXPECT_NE(json.find("\"camera_preview\""), std::string::npos);
  EXPECT_NE(json.find("\"runtime\""), std::string::npos);

  const auto loaded = EditorPlatform::LoadEditorSettingsFile(path);
  ASSERT_TRUE(loaded.Error.empty()) << loaded.Error;
  EXPECT_TRUE(loaded.Found);
  EXPECT_EQ(loaded.Preferences.ShowLightGizmos, preferences.ShowLightGizmos);
  EXPECT_EQ(loaded.Preferences.ShowCameraFrustums, preferences.ShowCameraFrustums);
  EXPECT_EQ(loaded.Preferences.ShowWireframePlane, preferences.ShowWireframePlane);
  EXPECT_FLOAT_EQ(loaded.Preferences.CameraPreviewScale, preferences.CameraPreviewScale);
  EXPECT_FLOAT_EQ(loaded.Preferences.CameraPreviewQuality, preferences.CameraPreviewQuality);
  EXPECT_EQ(loaded.Preferences.FrameLimiterHz, preferences.FrameLimiterHz);
  EXPECT_EQ(loaded.Preferences.ShadowMapResolution, preferences.ShadowMapResolution);
  EXPECT_EQ(loaded.Preferences.MaxShadowCasters, preferences.MaxShadowCasters);
  EXPECT_EQ(loaded.Preferences.VSyncEnabled, preferences.VSyncEnabled);
}

TEST(EditorSettingsTests, InvalidJsonReturnsDefaultsAndDiagnostic) {
  TemporarySettingsDirectory directory;
  const auto path = directory.Root() / "editor-settings.json";
  {
    std::ofstream output(path, std::ios::binary);
    output << R"({"version":1,"runtime":{},"rendering":{"show_light_gizmos":"sometimes"},"camera_preview":{}})";
  }

  const auto loaded = EditorPlatform::LoadEditorSettingsFile(path);
  EXPECT_TRUE(loaded.Found);
  EXPECT_NE(loaded.Error.find("show_light_gizmos"), std::string::npos);
  EXPECT_TRUE(loaded.Preferences.ShowLightGizmos);
  EXPECT_TRUE(loaded.Preferences.ShowCameraFrustums);
  EXPECT_TRUE(loaded.Preferences.ShowWireframePlane);
  EXPECT_FLOAT_EQ(loaded.Preferences.CameraPreviewScale, 0.35F);
  EXPECT_FLOAT_EQ(loaded.Preferences.CameraPreviewQuality, 0.25F);
}

TEST(EditorSettingsTests, UnreadableSettingsPathReturnsDefaultsAndDiagnostic) {
  TemporarySettingsDirectory directory;
  const auto loaded = EditorPlatform::LoadEditorSettingsFile(directory.Root());

  EXPECT_TRUE(loaded.Found);
  EXPECT_FALSE(loaded.Error.empty());
  EXPECT_TRUE(loaded.Preferences.ShowLightGizmos);
}
} // namespace
