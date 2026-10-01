/**
 * @file file_tabs_render.cpp
 * @brief The bar of open files, drawn
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The bar is drawn before the editor it sits above, so anything it does to the
// style stack is still held when the editor runs. A push without its pop, or a
// pop without its push, does not stay a small mistake: the next frame starts on a
// stack the previous one left behind, and the assertion fires against whatever
// ran in between. That is how a tab bar takes the editor down with it.
//
// So the check is that the stack is the same size before and after, with the
// bar drawn in between, over several frames and with a document that has unsaved
// changes on it — the case that draws the extra marker and so the extra line.

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#define private public
#include "undoAppEditor.hpp"
#undef private

using namespace undoApp::Editor;
using undoApp::ST::STApp;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

static int colorStackSize() { return ImGui::GetCurrentContext()->ColorStack.Size; }
static int styleVarStackSize() { return ImGui::GetCurrentContext()->StyleVarStack.Size; }


static void writeFile(const fs::path& p, const std::string& body) {
   std::ofstream out(p);
   out << body;
}

/// One frame with the bar drawn in it, as the Editor panel does.
static void frameWithBar(EditorApp& app) {
   ImGui::NewFrame();
   ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse);
   app.renderFileTabs();
   ImGui::End();
   ImGui::EndFrame();
}

/// The vertical room the bar leaves for the editor drawn under it.
///
/// This is the check that was missing, and its absence is why the bug survived:
/// every other check here passes with the bar sized to the whole panel. The bar is
/// drawn by a child window whose size is a zero, and a zero is not "no height" but
/// "all of it", so the bar grew to fill the panel and pushed the editor off the
/// bottom of it. Nothing about the style stack noticed, because the stack was
/// balanced throughout.
static float roomLeftForEditor(EditorApp& app, float panelHeight) {
   float left = -1.0f;
   for (int f = 0; f < 3; ++f) {
      ImGui::NewFrame();
      ImGui::SetNextWindowSize(ImVec2(1200.0f, panelHeight));
      ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse);
      app.renderFileTabs();
      left = ImGui::GetContentRegionAvail().y;
      ImGui::End();
      ImGui::EndFrame();
   }
   return left;
}

int main() {
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
   io.DisplaySize = ImVec2(1600, 900);
   io.Fonts->AddFontDefault();
   io.Fonts->Build();

   const fs::path dir = fs::temp_directory_path() / "undoStudio-file-tabs";
   std::error_code ec;
   fs::remove_all(dir, ec);
   fs::create_directories(dir);

   const fs::path a = dir / "Alpha.st";
   const fs::path b = dir / "Beta.st";
   writeFile(a, "PROGRAM Alpha\nVAR\n av : INT;\nEND_VAR\nav := 1;\nEND_PROGRAM\n");
   writeFile(b, "PROGRAM Beta\nVAR\n bv : INT;\nEND_VAR\nbv := 2;\nEND_PROGRAM\n");

   auto& app = EditorApp::getInstance();
   check(app.initialize(), "the editor app initializes");

   // --- no files open: the bar draws nothing and says so ---
   {
      const int colors = colorStackSize();
      const int vars = styleVarStackSize();
      frameWithBar(app);
      check(colorStackSize() == colors, "with nothing open the style stack is left alone");
      check(styleVarStackSize() == vars, "and so is the style-var stack");

      const float left = roomLeftForEditor(app, 600.0f);
      check(left > 400.0f, "with nothing open the panel is left for the editor, got " +
                               std::to_string(static_cast<int>(left)) + "px");
   }

   // --- one pinned tab ---
   app.openFile(a.string());
   app.m_open.pin(a.string());
   {
      const int colors = colorStackSize();
      const int vars = styleVarStackSize();
      for (int f = 0; f < 3; ++f) {
         frameWithBar(app);
      }
      check(colorStackSize() == colors, "a pinned tab leaves the colour stack as it was");
      check(styleVarStackSize() == vars, "and the style-var stack");

      const float left = roomLeftForEditor(app, 600.0f);
      check(left > 500.0f, "one tab leaves the panel for the editor, got " +
                               std::to_string(static_cast<int>(left)) + "px");
   }

   // --- a preview beside it, and one with unsaved changes ---
   app.openFile(b.string());
   app.m_open.setDirty(b.string(), true);
   check(app.m_open.size() == 2, "two tabs open, got " + std::to_string(app.m_open.size()));

   {
      const int colors = colorStackSize();
      const int vars = styleVarStackSize();
      for (int f = 0; f < 3; ++f) {
         frameWithBar(app);
      }
      check(colorStackSize() == colors,
            "a pinned tab, a preview and an unsaved mark leave the colour stack as it was, got " +
               std::to_string(colorStackSize()) + " against " + std::to_string(colors));
      check(styleVarStackSize() == vars, "and the style-var stack, got " +
            std::to_string(styleVarStackSize()) + " against " + std::to_string(vars));

      const float left = roomLeftForEditor(app, 600.0f);
      check(left > 500.0f, "a preview and an unsaved mark leave the panel for the editor, got " +
                               std::to_string(static_cast<int>(left)) + "px");
   }

   // --- many tabs, which is what the bar is for ---
   for (int i = 0; i < 30; ++i) {
      const fs::path p = dir / ("File" + std::to_string(i) + ".st");
      writeFile(p, "PROGRAM F" + std::to_string(i) + "\nVAR\n v : INT;\nEND_VAR\nv := 1;\nEND_PROGRAM\n");
      app.openFile(p.string());
      app.m_open.pin(p.string());
   }
   check(app.m_open.size() == 32, "thirty-odd tabs are open, got " + std::to_string(app.m_open.size()));
   {
      const int colors = colorStackSize();
      const int vars = styleVarStackSize();
      for (int f = 0; f < 3; ++f) {
         frameWithBar(app);
      }
      check(colorStackSize() == colors, "thirty tabs leave the colour stack as it was, got " +
             std::to_string(colorStackSize()) + " against " + std::to_string(colors));
      check(styleVarStackSize() == vars, "and the style-var stack");

      // Thirty tabs are wider than any panel, so the bar is on its scrollbar here.
      // The scrollbar is inside the bar's own height, so this is the case where a
      // bar that grew to fit it would push the editor down -- and the one where a
      // bar given only a row would clip the buttons it holds.
      const float left = roomLeftForEditor(app, 600.0f);
      check(left > 500.0f, "thirty tabs still leave the panel for the editor, got " +
                               std::to_string(static_cast<int>(left)) + "px");
   }


   // --- closing them all, back to nothing ---
   app.m_open.closeAll();
   {
      const int colors = colorStackSize();
      frameWithBar(app);
      check(colorStackSize() == colors, "closing every tab leaves the colour stack as it was");
      check(app.m_open.size() == 0, "and nothing open, got " + std::to_string(app.m_open.size()));
   }

   fs::remove_all(dir, ec);
   ImGui::DestroyContext();

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}