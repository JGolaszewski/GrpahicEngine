#include "runtime/include/PrismNdfInferenceService.h"

#include <gtest/gtest.h>

#include <filesystem>

namespace Engine {

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
