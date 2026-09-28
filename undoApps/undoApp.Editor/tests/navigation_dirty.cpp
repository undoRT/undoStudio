// End-to-end check of go-to-declaration: index, resolution, tab switch, dirty.
#include <imgui.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  // The fixture is written by the test: the old default pointed at
  // /tmp/opencode/ws2, which only existed on the machine that wrote the test, so
  // the index came back empty and every lookup below failed.
  const std::string root = undoApp::tests::writeWorkspaceFixture("undoStudio-navigation-dirty").string();

  STApp app;
  app.setupEditors();
  app.loadWorkspace(root);

  // --- workspace index ---
  app.ensureWorkspaceIndex();
  std::printf("workspace indexed: %zu names\n", app.m_workspaceDecls.size());
  auto hits = app.findWorkspaceDeclarations("Helper");
  check(hits.size() == 1, "Helper found once in workspace");
  if (!hits.empty()) {
    check(hits[0].file.find("Lib.st") != std::string::npos, "Helper declared in Lib.st");
    check(hits[0].kindText == "FUNCTION_BLOCK", "Helper is a FUNCTION_BLOCK");
    check(hits[0].line == 1, "Helper on line 1, got " + std::to_string(hits[0].line));
  }
  check(app.findWorkspaceDeclarations("Twice").size() == 1, "method Twice indexed");
  check(app.findWorkspaceDeclarations("seed").size() == 1, "parameter seed indexed (or skipped)");
  check(app.findWorkspaceDeclarations("nope").empty(), "unknown name not found");

  // --- open a file and navigate inside it ---
  app.requestOpenFile(root + "/User.st");
  check(app.m_currentFilePath.find("User.st") != std::string::npos, "User.st open");
  check(!app.m_isDirty, "clean after load");

  // 'h' is declared in the VAR block; find its declaration.
  const Declaration* h = app.findDeclaration("h", "");
  check(h != nullptr, "'h' declaration found");
  if (h) {
    std::printf("      h -> line %d col %d type '%s' kind '%s'\n", h->line, h->col, h->typeText.c_str(), h->kindText.c_str());
    check(h->typeText == "Helper", "h typed Helper (cross-file type kept as name)");
  }
  const Declaration* n = app.findDeclaration("n", "");
  check(n != nullptr && n->typeText == "INT", "n typed INT");

  // revealing the 'h' declaration should land on the VAR line of the var editor
  if (h) {
    app.revealGeneratedLine(h->line, h->col < 0 ? 0 : h->col);
    check(app.m_variablesEditor->GetCursorPosition().mLine ==
          h->line - app.segmentForLine(h->line)->fullStart,
          "cursor moved to the declaration line");
  }

  // --- dirty tracking ---
  app.m_isDirty = true;
  app.requestOpenFile(root + "/Lib.st");
  check(app.m_showDirtyPrompt, "dirty switch raises the prompt");
  check(app.m_pendingOpenPath.find("Lib.st") != std::string::npos, "pending path recorded");
  check(app.m_currentFilePath.find("User.st") != std::string::npos, "file not switched while dirty");
  app.m_showDirtyPrompt = false;
  app.m_isDirty = false;
  app.m_pendingOpenPath.clear();
  app.requestOpenFile(root + "/Lib.st");
  check(app.m_currentFilePath.find("Lib.st") != std::string::npos, "switch proceeds when clean");

  // --- after switching, the pending jump resolves inside the new file ---
  // Simulate what a cross-file Ctrl+Click queues before switching.
  app.m_pendingJumpName = "Twice";
  app.applyPendingJump();
  const Declaration* twice = app.findDeclaration("Twice", "");
  check(twice != nullptr, "Twice found in the newly opened file");
  if (twice) {
    check(app.findMethodIndex("Twice") >= 0,
          "the method is registered under its bare name, got '" +
             (app.m_methods.empty() ? std::string("<none>") : app.m_methods[0].name) + "'");
    check(!app.m_methods.empty() && app.m_methods[0].visibility == "PUBLIC",
          "the access specifier is kept in the visibility, not in the name");
    check(app.m_activeTab == "Twice", "switched to the METHOD tab, got '" + app.m_activeTab + "'");
  }

  // --- cache invalidation ---
  app.invalidateWorkspaceIndex("");
  check(!app.m_workspaceIndexValid, "index invalidated");

  std::printf("\n%s\n", failures ? "RESULT: FAILURES" : "RESULT: all checks passed");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  ImGui::DestroyContext();
  return failures ? 1 : 0;
}
