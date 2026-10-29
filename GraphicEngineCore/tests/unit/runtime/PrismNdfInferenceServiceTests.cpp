#include "runtime/include/PrismNdfInferenceService.h"
#include "PrismNdfInferenceControls.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>

namespace Engine {

TEST(PrismNdfInferenceControlsTests, DirectionalModeMapsToDefaultAnisotropy) {
  const auto isotropic = Runtime::Detail::ParsePrismControlFeatures("polished titanium", false);
  const auto directional = Runtime::Detail::ParsePrismControlFeatures("polished titanium", true);
  EXPECT_FLOAT_EQ(isotropic[1], 0.0F);
  EXPECT_FLOAT_EQ(isotropic[5], 0.0F);
  EXPECT_FLOAT_EQ(directional[1], 1.0F);
  EXPECT_FLOAT_EQ(directional[5], 1.0F);
}

TEST(PrismNdfInferenceControlsTests, ExplicitControlsOverrideDirectionalDefault) {
  const auto features = Runtime::Detail::ParsePrismControlFeatures(
      "roughness=25%, anisotropy=35%, orientation=90 degrees", true);
  EXPECT_FLOAT_EQ(features[0], 0.25F);
  EXPECT_FLOAT_EQ(features[1], 0.35F);
  EXPECT_FLOAT_EQ(features[4], 1.0F);
  EXPECT_FLOAT_EQ(features[5], 1.0F);
  EXPECT_NEAR(features[2], 0.0F, 1.0e-6F);
  EXPECT_NEAR(features[3], -1.0F, 1.0e-6F);
  EXPECT_FLOAT_EQ(features[6], 1.0F);
}

TEST(PrismNdfInferenceControlsTests, RejectsOutOfRangeNumericControls) {
  EXPECT_THROW(Runtime::Detail::ParsePrismControlFeatures("anisotropy=120%", false), std::runtime_error);
}

TEST(PrismNdfInferenceServiceTests, PublicApiIsAvailableInEveryBuild) {
  PrismNdfInferenceService service({});
  EXPECT_EQ(PrismNdfInferenceService::IsAvailable(), ENGINE_ENABLE_PRISM_INFERENCE != 0);
  EXPECT_FALSE(service.Poll(1).has_value());
}

#if !ENGINE_ENABLE_PRISM_INFERENCE
TEST(PrismNdfInferenceServiceTests, DisabledBuildReturnsActionableUnavailableStatus) {
  PrismNdfInferenceService service({});
  auto request = service.Start("rough brushed titanium", false);
  ASSERT_FALSE(request.has_value());
  EXPECT_NE(request.error().Message.find("ENGINE_ENABLE_PRISM_INFERENCE=ON"), std::string::npos);
  EXPECT_NE(request.error().Message.find("prism-inference vcpkg feature"), std::string::npos);
}
#else
TEST(PrismNdfInferenceServiceTests, EnabledBuildRejectsMissingAssetsBeforeQueueing) {
  const auto missingRoot = std::filesystem::temp_directory_path() / "GraphicEngine-missing-prism-runtime";
  PrismNdfInferenceService service({.modelDirectory = missingRoot / "models" / "prism",
                                    .pythonExecutable = missingRoot / "python" / "python.exe",
                                    .tokenizerWorker = missingRoot / "python" / "prism_tokenizer_worker.py"});
  auto request = service.Start("rough brushed titanium", false);
  ASSERT_FALSE(request.has_value());
  EXPECT_NE(request.error().Message.find("runtime assets are missing"), std::string::npos);
  EXPECT_NE(request.error().Message.find((missingRoot / "models" / "prism" / "e5_encoder.onnx").string()),
            std::string::npos);
  EXPECT_NE(request.error().Message.find((missingRoot / "models" / "prism" / "e5_encoder.onnx.data").string()),
            std::string::npos);
  EXPECT_NE(request.error().Message.find((missingRoot / "python" / "python.exe").string()), std::string::npos);
  EXPECT_NE(request.error().Message.find("scripts/stage_prism_runtime.py"), std::string::npos);
}
#endif

} // namespace Engine
