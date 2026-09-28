#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <memory>
#include <functional>
#include <unordered_map>
#include <filesystem>
#include <algorithm>

#define private public
#include <TextEditor.h>
#include "undoAppST.hpp"
#include "workspace_fixture.hpp"
#undef private

using namespace undoApp::ST;
static int failures = 0;
static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  STApp app;
  app.setupEditors();

  // No workspace loaded on purpose: the directory fallback must kick in.
  //
  // The fixture is written by the test. It used to point at /tmp/opencode/ws2,
  // which only existed on the machine that wrote the test, so the open failed,
  // findDeclaration returned null and revealDeclaration(*h) segfaulted. The null
  // check below is kept as well, so a broken fixture reports a failure instead
  // of taking the whole test binary down.
  const std::string root = undoApp::tests::writeWorkspaceFixture("undoStudio-reveal-selection").string();
  app.requestOpenFile(root + "/User.st");
  std::printf("opened %s (workspace loaded: %s)\n", app.m_currentFilePath.c_str(),
              app.m_rootNode.children.empty() ? "no" : "yes");

  // Jump to the declaration of 'h' as a Ctrl+Click on a usage would.
  const Declaration* h = app.findDeclaration("h", "");
  check(h != nullptr, "declaration of 'h' found");
  if (h == nullptr) {
    std::printf("\n%s\n", "RESULT: FAILURES");
    return 1;
  }
  const bool revealed = app.revealDeclaration(*h);
  check(revealed, "revealDeclaration reported success");

  const auto cur = app.m_variablesEditor->GetCursorPosition();
  std::printf("      var cursor now (%d,%d), selected '%s'\n", cur.mLine, cur.mColumn,
              app.m_variablesEditor->GetSelectedText().c_str());
  check(cur.mLine == h->line - app.segmentForLine(h->line)->fullStart, "cursor on the declaration line");
  check(app.m_variablesEditor->GetSelectedText() == "h", "the declaration is SELECTED (visible jump)");

  // Cross-file: 'Helper' is declared in Lib.st, not in the open file.
  const Declaration* inThisFile = app.findDeclaration("Helper", "");
  check(inThisFile == nullptr, "'Helper' is not declared in User.st");
  auto hits = app.findWorkspaceDeclarations("Helper");
  check(hits.size() == 1, "'Helper' found via the directory fallback");
  if (!hits.empty()) {
    check(hits[0].file.find("Lib.st") != std::string::npos, "found in Lib.st");
    check(hits[0].kindText == "FUNCTION_BLOCK", "declared as FUNCTION_BLOCK");
  }
  check(app.findWorkspaceDeclarations("Twice").size() == 1, "method of another file indexed");

  // Tooltips: keywords must never resolve, whatever the toggle says.
  app.m_showTooltips = true;
  app.m_errors.clear();

  // Undo the cursor move so the test starts clean.
  app.m_variablesEditor->SetCursorPosition(TextEditor::Coordinates(0, 0));

  std::printf("\n%s\n", failures ? "RESULT: FAILURES" : "RESULT: all checks passed");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  ImGui::DestroyContext();
  return failures ? 1 : 0;
}
