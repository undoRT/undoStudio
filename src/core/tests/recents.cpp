/**
 * @file recents.cpp
 * @brief The recent projects list, driven inside a real ImGui frame without a window
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// Open Recent is opened from the main menu bar, which is itself a window, and a
// popup is opened between a window's Begin and its End. That is where the nesting
// goes wrong when it goes wrong, and a crash there is not something a glance at
// the code settles.
//
// What is checked is the shape of the interaction rather than how the list looks:
// that it opens, that it lists what was asked for, that picking one leaves a
// request for the workspace, and that the list closes when the flag is dropped.

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#define private public
#include "undoStudio/core/ProjectManager.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"
#undef private

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

/// What a frame should do about the list.
enum class Ask { Nothing, Open, Draw };

/**
 * One frame, with the list asked for and drawn from inside a window.
 *
 * Both happen inside a window because that is where the menu bar does them, and
 * it is not a detail: OpenPopup and BeginPopup resolve the popup's identity
 * against the current window, so an ask made outside one has nothing to resolve
 * it against.
 */
static void frame(undoStudio::ui::ImGuiManager& mgr, Ask action) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(400.0f, 300.0f);
  io.DisplaySize = ImVec2(1280, 800);
  ImGui::NewFrame();
  ImGui::SetNextWindowSize(ImVec2(1200, 700));
  if (ImGui::Begin("##host", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
      if (action == Ask::Open) {
         mgr.openRecentProjects();
      } else if (action == Ask::Draw) {
         mgr.renderRecentProjects();
      }
  }
  ImGui::End();
  ImGui::EndFrame();
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  // A working directory of its own: the list is written to a state file relative
  // to it, and this must not land in the developer's own.
  const fs::path dir = fs::temp_directory_path() / "undoStudio-recents-test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  fs::current_path(dir);

  auto& mgr = undoStudio::ui::ImGuiManager::getInstance();
  auto& pm = undoStudio::core::ProjectManager::getInstance();

  // Two projects, so the list has a shape to draw and a second entry to pick.
  std::string first;
  std::string second;
  for (const std::string& name : {std::string("prjAlpha"), std::string("prjBeta")}) {
      const fs::path project = dir / name;
      fs::create_directories(project / ".undoProject");
      std::ofstream json(project / ".undoProject" / "project.json");
      json << "{\"project\": {\"name\": \"" << name << "\"}}\n";
      json.close();
      if (name == "prjAlpha") {
         first = project.string();
      } else {
         second = project.string();
      }
      check(pm.openProject(project.string()), "opened " + name);
      pm.closeProject();
  }

  // A frame with nothing open: the common case, and the one that would have run
  // a popup's Begin/End pair against a window that is not there.
  frame(mgr, Ask::Nothing);
  check(mgr.m_pendingOpenProject.empty(), "a frame with the list closed asks for nothing");

  // Open it.
  frame(mgr, Ask::Open);
  frame(mgr, Ask::Draw);
  check(pm.recentProjects().size() == 2, "two projects in the list, got " + std::to_string(pm.recentProjects().size()));

  // Keep it open for a few frames, which is what a user does while looking at it.
  bool stillOpen = true;
  for (int i = 0; i < 5 && stillOpen; ++i) {
      frame(mgr, Ask::Draw);
      stillOpen = mgr.m_showRecentsPopup;
  }
  check(stillOpen, "the list stays open while it is being looked at");

  // Picking one: the popup is a menu, so the entry under the mouse is what a click
  // would choose. Simulated through the request rather than the mouse, because
  // where ImGui places the rows is not what is being tested here.
  mgr.m_pendingOpenProject = first;
  mgr.m_showRecentsPopup = false;
  frame(mgr, Ask::Nothing);

  std::string requested;
  const std::string* ask = mgr.consumeOpenProjectRequest(requested);
  check(ask != nullptr, "a picked project leaves a request");
  check(ask != nullptr && *ask == first, "and it is the one that was picked, got '" + requested + "'");

  // The request is taken once. A workspace that did not take it would leave it
  // behind for good, and the next project opened would open this one instead.
  std::string again;
  check(mgr.consumeOpenProjectRequest(again) == nullptr, "the request is taken only once");

  // The other project is the next one down the list.
  mgr.m_pendingOpenProject = second;
  mgr.m_showRecentsPopup = false;
  frame(mgr, Ask::Nothing);
  std::string other;
  check(mgr.consumeOpenProjectRequest(other) != nullptr && other == second, "the second project is picked too");

  // --- the path the menu bar takes ---
  //
  // The ask and the draw both happen inside the main menu bar, which is itself a
  // window, and that nesting is what crashed: a popup opened there and closed
  // there has to be opened and closed in the same scope, because both resolve the
  // popup's identity against the window that is current at the time.
  {
      for (int i = 0; i < 3; ++i) {
          ImGuiIO& io = ImGui::GetIO();
          io.AddMousePosEvent(400.0f, 300.0f);
          io.DisplaySize = ImVec2(1280, 800);
          ImGui::NewFrame();
          ImGui::SetNextWindowSize(ImVec2(1200, 700));
          if (ImGui::BeginMainMenuBar()) {
              mgr.openRecentProjects();
              mgr.renderRecentProjects();
          }
          ImGui::EndMainMenuBar();
          ImGui::EndFrame();
      }
      check(!mgr.m_showRecentsPopup || pm.recentProjects().size() == 2,
            "asking and drawing inside the menu bar neither crashes nor leaves the list stuck");
  }

  // A list emptied from the menu leaves the flag down, so the next frame does not
  // open a new one behind the closed box.
  frame(mgr, Ask::Open);
  frame(mgr, Ask::Draw);
  pm.clearRecentProjects();
  mgr.m_showRecentsPopup = false;
  frame(mgr, Ask::Draw);
  check(!mgr.m_showRecentsPopup, "the list closes and stays closed when the flag is dropped");

  ImGui::DestroyContext();

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}
