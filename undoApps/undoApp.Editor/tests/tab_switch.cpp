/**
 * @file tab_switch.cpp
 * @brief A tab selected behind ImGui's back stays selected
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <imgui.h>
#include <imgui_internal.h>
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
  stEditorFrame(app);
  ImGui::EndFrame();
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  io().DisplaySize = ImVec2(1280, 800);
  io().Fonts->AddFontDefault();
  io().Fonts->Build();

  // The fixture is written by the test. This pointed at the real
  // ~/Desktop/progettiUndoStudio project, so the test could only pass where that
  // project existed, and the failure mode was misleading: the tab flipped to the
  // POU on frame 0 because undoMeth did not exist, not because tab selection broke.
  const std::string root = undoApp::tests::writeBlockFixture("undoStudio-tab-switch").string();
  const std::string f = root + "/undoFB.st";
  STApp app;
  app.setupEditors();
  app.requestOpenFile(f);
  frame(app); frame(app);
  std::printf("POU '%s' methods:", app.m_pouName.c_str());
  for (auto& m : app.m_methods) std::printf(" '%s'", m.name.c_str());
  std::printf("\nstart activeTab='%s'\n", app.m_activeTab.c_str());

  // Programmatic switch must stick and must not oscillate.
  app.revealMethodTab("undoMeth");
  check(app.m_pendingTabSelection == "undoMeth", "a one-shot selection was requested");
  frame(app);
  check(app.m_pendingTabSelection.empty(), "the request is consumed after one frame");
  for (int i = 0; i < 5; ++i) {
    frame(app);
    if (app.m_activeTab != "undoMeth") {
      check(false, "activeTab stayed on undoMeth (flipped to '" + app.m_activeTab + "' on frame " + std::to_string(i) + ")");
      break;
    }
  }
  check(app.m_activeTab == "undoMeth", "the METHOD tab stays selected across frames");
  check(app.m_methodEditors.count("undoMeth") == 1, "the METHOD tab content was rendered");

  // Switching back to the POU must also work and must stick.
  app.requestTabSelection(std::string());
  frame(app);
  for (int i = 0; i < 5; ++i) {
    frame(app);
    if (!app.m_activeTab.empty()) {
      check(false, "activeTab stayed on the POU (became '" + app.m_activeTab + "')");
      break;
    }
  }
  check(app.m_activeTab.empty(), "the POU tab stays selected across frames");

  // Alternating many times must not drift.
  // ImGui applies SetSelected on the following frame, so let it settle.
  for (int i = 0; i < 6; ++i) {
    app.revealMethodTab("meth2");
    frame(app); frame(app);
    if (app.m_activeTab != "meth2") {
      check(false, "settled on meth2 (got '" + app.m_activeTab + "')");
      break;
    }
    app.requestTabSelection("");
    frame(app); frame(app);
    if (!app.m_activeTab.empty()) {
      check(false, "settled back on the POU (got '" + app.m_activeTab + "')");
      break;
    }
  }
    check(app.m_activeTab.empty(), "repeated toggling ends on the POU tab");

  std::printf("\n%s\n", failures ? "RESULT: FAILURES" : "RESULT: all checks passed");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  ImGui::DestroyContext();
  return failures ? 1 : 0;
}
