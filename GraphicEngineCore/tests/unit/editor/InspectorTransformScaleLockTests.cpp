#include "panels/include/TransformScaleLock.h"

#include <gtest/gtest.h>

TEST(InspectorTransformScaleLockTests, UnlockedEditsRemainIndependent) {
  std::array<float, 3> scale = {1.0F, 2.0F, 3.0F};
  const std::array<float, 3> before = scale;
  scale[1] = 4.0F;

  EXPECT_FALSE(Editor::TransformScaleLock::Apply(scale, before, false));
  EXPECT_EQ(scale, (std::array<float, 3>{1.0F, 4.0F, 3.0F}));
}

TEST(InspectorTransformScaleLockTests, LockedEditSetsAllAxesToTheEditedValue) {
  for (std::size_t editedAxis = 0; editedAxis < 3; ++editedAxis) {
    std::array<float, 3> scale = {1.0F, 2.0F, 3.0F};
    const std::array<float, 3> before = scale;
    scale[editedAxis] = 5.0F;

    EXPECT_TRUE(Editor::TransformScaleLock::Apply(scale, before, true));
    EXPECT_EQ(scale, (std::array<float, 3>{5.0F, 5.0F, 5.0F}));
  }
}

TEST(InspectorTransformScaleLockTests, TogglingLockWithoutAnEditDoesNotChangeScale) {
  std::array<float, 3> scale = {1.0F, 2.0F, 3.0F};
  const std::array<float, 3> before = scale;

  EXPECT_FALSE(Editor::TransformScaleLock::Apply(scale, before, true));
  EXPECT_EQ(scale, before);
}
