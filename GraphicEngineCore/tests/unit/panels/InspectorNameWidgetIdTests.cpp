#include "InspectorWidgetIds.h"

#include <gtest/gtest.h>
#include <imgui_internal.h>

#include <string_view>

TEST(InspectorNameWidgetIdTests, NameFieldHasDistinctEntityScopedIdAndVisibleNameLabel) {
  const ImGuiID entityScope = ImHashStr("ID: 12:3");
  const ImGuiID componentHeaderId = ImHashStr("Name", 0, entityScope);
  const ImGuiID nameFieldId = ImHashStr(Editor::Panels::Detail::kEntityNameInputLabel, 0, entityScope);
  constexpr std::string_view nameInputLabel = Editor::Panels::Detail::kEntityNameInputLabel;
  const size_t hiddenIdDelimiter = nameInputLabel.find("##");

  EXPECT_NE(nameFieldId, componentHeaderId);
  ASSERT_NE(hiddenIdDelimiter, std::string_view::npos);
  EXPECT_EQ(nameInputLabel.substr(0, hiddenIdDelimiter), "Name");
  EXPECT_NE(nameFieldId, ImHashStr(Editor::Panels::Detail::kEntityNameInputLabel, 0, ImHashStr("ID: 13:3")));
}
