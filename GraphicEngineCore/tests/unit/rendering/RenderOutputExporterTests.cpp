#include "RenderOutputExporterInternal.h"

#ifdef _MSC_VER
#  pragma warning(push)
#  pragma warning(disable : 4702) // OpenImageIO's template code triggers MSVC's unreachable-code warning.
#endif
#include <OpenImageIO/imageio.h>
#ifdef _MSC_VER
#  pragma warning(pop)
#endif

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace {

class TemporaryOutputDirectory final {
public:
  TemporaryOutputDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    m_Root = std::filesystem::temp_directory_path() / ("GraphicEngineRenderOutputTests-" + std::to_string(nonce));
    std::filesystem::create_directories(m_Root);
  }

  TemporaryOutputDirectory(const TemporaryOutputDirectory&) = delete;
  auto operator=(const TemporaryOutputDirectory&) -> TemporaryOutputDirectory& = delete;
  ~TemporaryOutputDirectory() noexcept { // NOLINT(bugprone-exception-escape): uses remove_all's error_code overload.
    std::error_code ignored;
    std::filesystem::remove_all(m_Root, ignored);
  }

  [[nodiscard]] auto File(const char* name) const -> std::filesystem::path { return m_Root / name; }

private:
  std::filesystem::path m_Root;
};

auto ReadInput(const std::filesystem::path& path) -> OIIO::ImageInput::unique_ptr {
  return OIIO::ImageInput::open(path.wstring());
}

TEST(RenderOutputExporterTests, WritesToneMappedSrgbPngWithDimensionsAndDecodablePixels) {
  TemporaryOutputDirectory directory;
  const std::array<float, 12> finalColor{0.0F, 0.18F, 1.0F, 1.0F, 4.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.5F};
  const auto path = directory.File("final.png");

  const auto status = Engine::Rendering::Detail::WritePngFile(3, 1, finalColor, path);
  ASSERT_TRUE(status) << (status ? "" : status.error().Message);

  auto input = ReadInput(path);
  ASSERT_NE(input, nullptr) << OIIO::geterror();
  const auto& spec = input->spec();
  EXPECT_EQ(spec.width, 3);
  EXPECT_EQ(spec.height, 1);
  EXPECT_EQ(spec.nchannels, 4);
  EXPECT_EQ(spec.channelnames, (std::vector<std::string>{"R", "G", "B", "A"}));

  std::array<float, 12> decoded{};
  ASSERT_TRUE(input->read_image(0, 0, 0, -1, OIIO::span<float>(decoded.data(), decoded.size()))) << input->geterror();
  // OIIO decodes the byte samples as normalized sRGB code values for float.
  EXPECT_NEAR(decoded[0], 0.0F, 1.0F / 255.0F);
  EXPECT_NEAR(decoded[1], 109.0F / 255.0F, 2.0F / 255.0F);
  EXPECT_NEAR(decoded[2], 188.0F / 255.0F, 2.0F / 255.0F);
  EXPECT_NEAR(decoded[4], 231.0F / 255.0F, 2.0F / 255.0F);
  EXPECT_NEAR(decoded[7], 1.0F, 1.0F / 255.0F);
  EXPECT_NEAR(decoded[11], 128.0F / 255.0F, 1.0F / 255.0F);
}

TEST(RenderOutputExporterTests, WritesSelectedAovsAsNamedFloat32ExrParts) {
  TemporaryOutputDirectory directory;
  const std::vector<Engine::RenderOutputPlane> planes{
      {Engine::RenderAov::Depth, 1, {0.25F, 1.0F}},
      {Engine::RenderAov::WorldNormal, 3, {0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F}},
      {Engine::RenderAov::FinalColor, 4, {0.5F, 0.25F, 0.125F, 1.0F, 0.2F, 0.3F, 0.4F, 0.5F}},
      {Engine::RenderAov::SpecularLighting, 3, {0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F}},
      {Engine::RenderAov::DiffuseLighting, 3, {0.7F, 0.8F, 0.9F, 1.0F, 1.1F, 1.2F}},
  };
  const std::array selected{Engine::RenderAov::Depth, Engine::RenderAov::WorldNormal, Engine::RenderAov::FinalColor,
                            Engine::RenderAov::SpecularLighting, Engine::RenderAov::DiffuseLighting};
  const auto path = directory.File("selected.exr");

  const auto status = Engine::Rendering::Detail::WriteMultilayerExrFile(2, 1, planes, selected, path);
  ASSERT_TRUE(status) << (status ? "" : status.error().Message);

  auto input = ReadInput(path);
  ASSERT_NE(input, nullptr) << OIIO::geterror();
  const std::array expectedPartNames{"depth", "world_normal", "final_color", "specular_lighting", "diffuse_lighting"};
  const std::array<std::vector<std::string>, 5> expectedChannels{
      std::vector<std::string>{"Z"}, std::vector<std::string>{"X", "Y", "Z"},
      std::vector<std::string>{"R", "G", "B", "A"}, std::vector<std::string>{"R", "G", "B"},
      std::vector<std::string>{"R", "G", "B"}};
  const std::array<std::vector<float>, 5> expectedPixels{
      std::vector<float>{0.25F, 1.0F}, std::vector<float>{0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.0F},
      std::vector<float>{0.5F, 0.25F, 0.125F, 1.0F, 0.2F, 0.3F, 0.4F, 0.5F},
      std::vector<float>{0.1F, 0.2F, 0.3F, 0.4F, 0.5F, 0.6F}, std::vector<float>{0.7F, 0.8F, 0.9F, 1.0F, 1.1F, 1.2F}};
  for (size_t part = 0; part < expectedPartNames.size(); ++part) {
    ASSERT_TRUE(input->seek_subimage(static_cast<int>(part), 0)) << input->geterror();
    const auto& spec = input->spec();
    EXPECT_EQ(spec.width, 2);
    EXPECT_EQ(spec.height, 1);
    EXPECT_EQ(spec.format, OIIO::TypeDesc::FLOAT);
    EXPECT_EQ(spec.get_string_attribute("oiio:subimagename"), expectedPartNames[part]);
    EXPECT_EQ(spec.channelnames, expectedChannels[part]);

    std::vector<float> decoded(expectedPixels[part].size());
    ASSERT_TRUE(input->read_image(static_cast<int>(part), 0, 0, -1, OIIO::span<float>(decoded.data(), decoded.size())))
        << input->geterror();
    ASSERT_EQ(decoded.size(), expectedPixels[part].size());
    for (size_t sample = 0; sample < decoded.size(); ++sample) {
      EXPECT_FLOAT_EQ(decoded[sample], expectedPixels[part][sample]);
    }
  }
  EXPECT_FALSE(input->seek_subimage(static_cast<int>(expectedPartNames.size()), 0));

  const std::array selectedSubset{Engine::RenderAov::SpecularLighting};
  const auto subsetPath = directory.File("one_layer.exr");
  ASSERT_TRUE(Engine::Rendering::Detail::WriteMultilayerExrFile(2, 1, planes, selectedSubset, subsetPath));
  auto subsetInput = ReadInput(subsetPath);
  ASSERT_NE(subsetInput, nullptr) << OIIO::geterror();
  EXPECT_EQ(subsetInput->spec().get_string_attribute("oiio:subimagename"), "specular_lighting");
  EXPECT_FALSE(subsetInput->seek_subimage(1, 0));
}

TEST(RenderOutputExporterTests, RejectsUnavailableDuplicateAndEmptyAovSelections) {
  TemporaryOutputDirectory directory;
  const std::vector<Engine::RenderOutputPlane> planes{{Engine::RenderAov::Depth, 1, {0.5F}}};
  const std::array missing{Engine::RenderAov::WorldNormal};
  const std::array duplicate{Engine::RenderAov::Depth, Engine::RenderAov::Depth};
  const auto path = directory.File("invalid.exr");

  EXPECT_FALSE(Engine::Rendering::Detail::WriteMultilayerExrFile(1, 1, planes, missing, path));
  EXPECT_FALSE(Engine::Rendering::Detail::WriteMultilayerExrFile(1, 1, planes, duplicate, path));
  EXPECT_FALSE(Engine::Rendering::Detail::WriteMultilayerExrFile(1, 1, planes, {}, path));
}

TEST(RenderOutputExporterTests, RejectsInvalidDimensionsAndNonFinitePngSamples) {
  TemporaryOutputDirectory directory;
  const std::array<float, 4> finalColor{0.0F, 0.0F, 0.0F, 1.0F};
  const std::array<float, 4> invalidColor{0.0F, 0.0F, std::numeric_limits<float>::quiet_NaN(), 1.0F};

  EXPECT_FALSE(Engine::Rendering::Detail::WritePngFile(0, 1, finalColor, directory.File("zero.png")));
  EXPECT_FALSE(Engine::Rendering::Detail::WritePngFile(1, 1, invalidColor, directory.File("nan.png")));
}

} // namespace
