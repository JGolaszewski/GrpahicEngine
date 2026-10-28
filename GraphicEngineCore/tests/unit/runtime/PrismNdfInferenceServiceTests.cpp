#include "runtime/include/PrismNdfInferenceService.h"

#include <gtest/gtest.h>

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
  PrismNdfInferenceService service({});
  auto request = service.Start("rough brushed titanium", false);
  ASSERT_FALSE(request.has_value());
  EXPECT_NE(request.error().Message.find("runtime assets are missing"), std::string::npos);
}
#endif

} // namespace Engine
