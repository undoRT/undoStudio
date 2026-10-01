/**
 * @file method_tab.cpp
 * @brief The METHOD tab: opening one from a call in the POU body, and what it resolves to
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

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
#include "st_editor_frame.hpp"
#include "workspace_fixture.hpp"
#undef private

using namespace undoApp::ST;
static int failures = 0;
static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

static void frame(STApp& app) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(io.MousePos.x, io.MousePos.y);
  ImGui::NewFrame();
  ImGui::SetNextWindowSize(ImVec2(1200, 800));
  stEditorFrame(app);
  ImGui::EndFrame();
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  // The fixture is written by the test. This used to open the real
  // ~/Desktop/progettiUndoStudio project, so the test could only pass on the
  // machine where it was written, and it silently depended on that project's
  // undoFB.st keeping the shape asserted below.
  const std::string root = undoApp::tests::writeBlockFixture("undoStudio-method-tab").string();
  const std::string f = root + "/undoFB.st";
  STApp app;
  app.setupEditors();
  app.requestOpenFile(f);
  frame(app);

  check(app.m_activeTab.empty(), "starts on the POU tab");

  // Simulate the Ctrl+Click path on the method call at POU body level.
  const Declaration* meth = app.findDeclaration("undoMeth", "");
  check(meth != nullptr, "undoMeth resolves to a declaration");
  if (meth) {
    check(meth->kindText == "METHOD", "it is the METHOD, not something else");
    app.revealDeclaration(*meth);
  }
  check(app.m_activeTab == "undoMeth", "m_activeTab switched to undoMeth");

  // The tab bar only re-evaluates on the next frame.
  frame(app);
  std::printf("      method editors materialised: %zu\n", app.m_methodEditors.size());
  check(app.m_methodEditors.count("undoMeth") == 1, "the METHOD tab was opened and rendered");
  if (app.m_methodEditors.count("undoMeth")) {
    const auto& e = app.m_methodEditors.at("undoMeth");
    std::printf("      method body: [%s]\n", e.body->GetText().c_str());
    check(e.body->GetText().find("undoMeth := undoVar") != std::string::npos,
          "the method body is the one being shown");
  }

  // Shadowing still resolves per scope after all of this.
  check(app.findDeclaration("undoVar", "")->line == 10, "POU undoVar -> line 10");
  check(app.findDeclaration("undoVar", "undoMeth")->line == 16, "method undoVar -> line 16");

  // And going back to the POU tab works.
  app.m_activeTab.clear();
  frame(app);
  check(app.m_methodEditors.count("undoMeth") == 1, "switching back keeps the method tab alive");

  std::printf("\n%s\n", failures ? "RESULT: FAILURES" : "RESULT: all checks passed");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  ImGui::DestroyContext();
  return failures ? 1 : 0;
}
