/**
 * @file overlay_placement.cpp
 * @brief Where the ST editor's overlays are put when the window is not at the screen's top left corner
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The three overlays — the signature help, the member list and the statement list
// — are windows of their own, placed with SetNextWindowPos(), which takes a
// position relative to the viewport they go into. The editor reports where its
// cursor and its own area are in the viewport's coordinates, which are the
// screen's coordinates once the window has been moved away from (0,0): the main
// viewport's position is added to whatever lies inside it. Handing that to
// SetNextWindowPos() counts the window's origin twice, and the list ends up offset
// by the distance between the window and the corner of the screen.
//
// An overlay whose rectangle is not inside the main viewport's is not merged into
// it. ImGui gives it a viewport of its own, and a viewport is a second OS window:
// one created and destroyed as the list opens and closes, which is what made the
// IDE flicker on one monitor and not on another. Whether the offset rectangle
// happened to stay inside the window depended on the window's size and position,
// and that is what made the symptom monitor-dependent.
//
// The tests drive real frames through a stubbed windowing system (see
// fake_viewports.hpp), with the main viewport away from the origin, which is the
// state no headless test was in before: at (0,0) the offset is zero, so the bug
// had nothing to show.
//
// Uses an ImGui context without a window, so it runs headless.
#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#define private public
#include <TextEditor.h>
#include "undoAppST.hpp"
#include "st_editor_frame.hpp"
#undef private

#include "fake_viewports.hpp"

using namespace undoApp::ST;

static int failures = 0;
static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

/// One frame with the body editor holding the keyboard, and the platform windows
/// brought up to date afterwards.
///
/// UpdatePlatformWindows() is what creates the second OS window, and the tests
/// below are about whether it is asked to. RenderPlatformWindowsDefault() is
/// called with it because it is the pair ImGui expects, and it needs a renderer:
/// the stub installs the backend flag that says one is present.
static void frameWithBodyEditorFocused(STApp& app) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(600.0f, 700.0f);
  ImGui::NewFrame();
  io.WantTextInput = true;
  ImGui::SetNextWindowSize(ImVec2(1200, 800));
  // The panel into the main viewport, which is where the dockspace keeps it. The
  // test calls the panel directly instead of going through the dockspace, and a
  // top-level window that is not docked is given a viewport of its own, which
  // would be a second window before the overlays had done anything at all.
  ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
  stEditorFrame(app);
  ImGui::EndFrame();
  ImGui::UpdatePlatformWindows();
}

/// Whether the inner rectangle is inside the outer one, on both axes
static bool contains(const ImRect& outer, const ImRect& inner) {
  return inner.Min.x >= outer.Min.x - 0.5f && inner.Max.x <= outer.Max.x + 0.5f
         && inner.Min.y >= outer.Min.y - 0.5f && inner.Max.y <= outer.Max.y + 0.5f;
}

static std::string rectText(const ImRect& rect) {
  return "x=[" + std::to_string(static_cast<int>(rect.Min.x)) + ".." +
         std::to_string(static_cast<int>(rect.Max.x)) + "] y=[" +
         std::to_string(static_cast<int>(rect.Min.y)) + ".." +
         std::to_string(static_cast<int>(rect.Max.y)) + "]";
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  // The window is told it is 1920 pixels to the right of the screen's corner,
  // which is what a second monitor does to it.
  installFakeViewportBackend(ImVec2(1280, 800));
  io.Fonts->AddFontDefault();
  io.Fonts->Build();

  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "undoStudio-overlay-placement";
  std::filesystem::create_directories(dir);

  {
    std::ofstream out(dir / "undoFB.st");
    out << "FUNCTION_BLOCK undoFB\n"
           "VAR_INPUT\n"
           "    abilita : BOOL;\n"
           "END_VAR\n"
           "VAR\n"
           "    ticks : INT;\n"
           "END_VAR\n"
           "VAR CONSTANT\n"
           "    limite : INT := 10;\n"
           "END_VAR\n"
           "METHOD PUBLIC Reset : BOOL\n"
           "    Reset := TRUE;\n"
           "END_METHOD\n"
           "END_FUNCTION_BLOCK\n";
  }
  const auto pouPath = dir / "undoPOU.st";
  {
    std::ofstream out(pouPath);
    out << "PROGRAM undoPOU\n"
           "VAR\n"
           "    motore : undoFB;\n"
           "END_VAR\n"
           "END_PROGRAM\n";
  }

  STApp app;
  app.setupEditors();
  app.requestOpenFile(pouPath.string());
  frameWithBodyEditorFocused(app);
  check(app.m_pouName == "undoPOU", "the POU opened, got '" + app.m_pouName + "'");

  // Read after a frame, because the main viewport's position is filled in by the
  // platform callback during NewFrame and the viewport is still zero before that.
  const ImRect screen = fakeMainViewportRect();
  check(screen.Min.x == 1920.0f, "the main viewport is away from the screen's corner, got " +
                                     rectText(screen));
  check(fakeWindowCount() == 1, "nothing but the main window exists yet, there are " +
                                     std::to_string(fakeWindowCount()));

  // --- the member list, on a line long enough for its position to matter ---
  //
  // The list is 520 pixels wide and the window is 1280, so a cursor near the left
  // edge would leave it inside the window even with the origin counted twice, and
  // the bug would go unnoticed. The line is padded to put the cursor past the
  // middle of the window, which is where the doubled origin puts it outside.
  {
    const std::string line =
        "\nmotore := motore; abilita := TRUE; abilita := TRUE; abilita := TRUE; motore.\n";
    app.m_bodyEditor->SetText(line);
    TextEditor::Coordinates at;
    at.mLine = 1;
    at.mColumn = static_cast<int>(line.size());
    app.m_bodyEditor->SetCursorPosition(at);
  }
  frameWithBodyEditorFocused(app);
  check(!app.m_completionCandidates.empty(), "'.' at the end of a long line opens the list");

  const ImRect list = ImRect(app.m_completionRectMin, app.m_completionRectMax);
  const ImRect editorArea = ImRect(app.m_completionEditorMin, app.m_completionEditorMax);
  check(list.Max.x > list.Min.x, "the list drew, at " + rectText(list));
  check(contains(screen, list), "the list is inside the window, at " + rectText(list) +
                                    " the window is " + rectText(screen));
  check(contains(editorArea, list), "the list is inside the editor's own area, at " + rectText(list) +
                                         " the area is " + rectText(editorArea));
  // This is the check that is about the flicker rather than about geometry: a
  // viewport is a second OS window, and one opening and closing with the list is
  // the whole symptom.
  check(fakeWindowCount() == 1, "the list did not open a second OS window, there are " +
                                     std::to_string(fakeWindowCount()));

  // --- the signature help, on the same terms ---
  //
  // It is the other of the two overlays anchored to the cursor and it is placed by
  // the same helper, so it was placed wrongly for the same reason.
  app.clearMemberCompletion();
  app.m_bodyEditor->SetText("\nmotore.Reset(\n");
  {
    TextEditor::Coordinates at;
    at.mLine = 1;
    at.mColumn = 13;
    app.m_bodyEditor->SetCursorPosition(at);
  }
  frameWithBodyEditorFocused(app);
  if (app.m_signatureEditor == app.m_bodyEditor.get()) {
    const ImRect signature = ImRect(app.m_signatureRectMin, app.m_signatureRectMax);
    check(signature.Max.x > signature.Min.x, "the signature help drew, at " + rectText(signature));
    check(contains(screen, signature), "the signature help is inside the window, at " +
                                          rectText(signature) + " the window is " + rectText(screen));
  } else {
    check(false, "the signature help did not open for a method call");
  }

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}
