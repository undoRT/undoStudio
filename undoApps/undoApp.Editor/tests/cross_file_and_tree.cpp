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
static ImGuiIO& io() { return ImGui::GetIO(); }
static int failures = 0;
static void check(bool ok, const std::string& w) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", w.c_str());
  if (!ok) ++failures;
}
static void frame(STApp& app) {
  io().AddMousePosEvent(io().MousePos.x, io().MousePos.y);
  ImGui::NewFrame();
  ImGui::SetNextWindowSize(ImVec2(1200, 800));
  app.renderEditorPanel();
  app.renderWorkspacePanel();
  ImGui::EndFrame();
}

// The fixture is written by the test. This used to point at the real
// ~/Desktop/progettiUndoStudio project, so the whole cross-file behaviour was
// only ever exercised against one machine's files.
static std::string g_dir;

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  io().DisplaySize = ImVec2(1280, 800);
  io().Fonts->AddFontDefault();
  io().Fonts->Build();

  g_dir = undoApp::tests::writeBlockFixture("undoStudio-cross-file").string();

  std::printf("== 1. cross-file: an FB used in a PRG ==\n");
  {
    STApp app;
    app.setupEditors();
    app.requestOpenFile(g_dir + "/undoPRG.st");
    frame(app);
    std::printf("   errors: %d\n", (int)app.m_errors.size());
    for (const auto& e : app.m_errors) std::printf("     L%d %s\n", e.line, e.message.c_str());
    check(app.m_errors.empty(), "no error for 'undoFB_ : undoFB' declared in the sibling file");
  }

  std::printf("\n== 2. methods shown under the FB in the tree ==\n");
  {
    STApp app;
    app.setupEditors();
    app.requestOpenFile(g_dir + "/undoFB.st");
    frame(app);
    app.ensureWorkspaceIndex();
    const std::string fb = g_dir + "/undoFB.st";
    auto it = app.m_workspaceMethods.find(fb);
    check(it != app.m_workspaceMethods.end(), "the FB file has methods recorded");
    if (it != app.m_workspaceMethods.end()) {
      std::printf("   methods found: %zu\n", it->second.size());
      for (const auto& m : it->second)
        std::printf("     '%s' : %s (line %d)\n", m.name.c_str(), m.returnType.c_str(), m.line);
      check(!it->second.empty(), "at least one method listed");
    }
  }

  std::printf("\n== 3. clicking a method in the tree opens its tab ==\n");
  {
    STApp app;
    app.setupEditors();
    app.requestOpenFile(g_dir + "/undoPRG.st"); // a DIFFERENT file
    frame(app);
    check(app.m_activeTab.empty(), "starts on the PRG");
    const std::string fb = g_dir + "/undoFB.st";
    app.ensureWorkspaceIndex();
    if (app.m_workspaceMethods.count(fb) && !app.m_workspaceMethods[fb].empty()) {
      const std::string method = app.m_workspaceMethods[fb].front().name;
      app.openMethodOf(fb, method);
      check(app.m_currentFilePath == fb, "the FB file was opened");
      frame(app); frame(app);
      std::printf("   activeTab='%s' pending='%s'\n", app.m_activeTab.c_str(),
                  app.m_pendingTabSelection.c_str());
      check(app.m_activeTab == method, "the method tab is selected after the switch");
    }
  }

  std::printf("\n== 4. tabs still switch cleanly ==\n");
  {
    STApp app;
    app.setupEditors();
    app.requestOpenFile(g_dir + "/undoFB.st");
    frame(app);
    app.revealMethodTab(app.m_methods.empty() ? "" : app.m_methods.front().name);
    frame(app); frame(app);
    check(!app.m_activeTab.empty(), "switched to a method tab");
    app.requestTabSelection("");
    frame(app); frame(app);
    check(app.m_activeTab.empty(), "switched back to the POU tab");
  }

  std::printf("\n%s\n", failures ? "RESULT: FAILURES" : "RESULT: all checks passed");
  std::error_code ec;
  std::filesystem::remove_all(g_dir, ec);
  ImGui::DestroyContext();
  return failures ? 1 : 0;
}
