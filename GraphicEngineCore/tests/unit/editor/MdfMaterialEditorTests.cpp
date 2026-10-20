#include "panels/include/MdfMaterialEditorFields.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

TEST(MdfMaterialEditorTests, ControlInventoryCoversEverySupportedMaterialParameter) {
  constexpr std::array<std::string_view, 21> expected = {"BaseColor",         "Emissive",
                                                         "Metallic",          "Roughness",
                                                         "GtrGamma",          "Ior",
                                                         "SpecularF0",        "Model",
                                                         "Workflow",          "SpecularMode",
                                                         "AlphaMode",         "AlphaCutoff",
                                                         "DoubleSided",       "BaseColorTexCoord",
                                                         "NormalTexCoord",    "MetallicRoughnessTexCoord",
                                                         "OcclusionTexCoord", "EmissiveTexCoord",
                                                         "NormalScale",       "OcclusionStrength",
                                                         "TextureTransforms"};
  EXPECT_EQ(MdfMaterialEditorFields, expected);
  for (const auto field : MdfMaterialEditorFields) {
    EXPECT_NE(field, "Legacy");
  }
}
