/**
 * @file display_report.cpp
 * @brief What the display log records, and how often it is written
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// A popup is a platform viewport of its own, and on a mixed-DPI setup the numbers
// that decide whether it is drawn once or redrawn every frame are the scale of the
// monitor the window is on and the scale ImGui gives the viewport. Neither is
// visible from the code, so the report writes them, and a report that cannot be read
// is the same as no report at all.
//
// The second thing checked here is that the report is written once per change and not
// once per frame, because that is the way a diagnostic becomes the bug: at sixty
// frames a second the numbers bury everything else in the log, and the reading that
// was meant to explain a flicker becomes unreadable. The output is captured from
// std::cout rather than read back from the cache, so what is checked is the lines the
// user would paste, not the field the function happens to keep.

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define private public
#include "undoStudio/ui/ImGuiManager.hpp"
#undef private

using undoStudio::ui::ImGuiManager;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

/// Everything reportDisplayIfChanged() wrote, and the number of lines.
///
/// The stream is put back by the destructor, so a check() that fails half way
/// through still leaves the log usable for whatever is read next.
struct Captured {
   std::ostringstream text;
   std::streambuf* saved;
   Captured() : saved(std::cout.rdbuf(text.rdbuf())) {}
   ~Captured() { std::cout.rdbuf(saved); }
   int lines() const {
      int n = 0;
      for (char c : text.str()) { if (c == '\n') ++n; }
      return n;
   }
   bool contains(const std::string& what) const { return text.str().find(what) != std::string::npos; }
   void clear() { text.str(std::string()); text.clear(); }
};

int main() {
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.DisplaySize = ImVec2(1600, 900);
   io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
   io.Fonts->AddFontDefault();
   io.Fonts->Build();

   ImGuiPlatformIO& platform = ImGui::GetPlatformIO();

   // Two monitors at different scales, which is the arrangement the report exists
   // for, and a platform viewport for a popup that landed on the scaled one.
   ImGuiPlatformMonitor left;
   left.MainPos = ImVec2(0, 0);
   left.MainSize = ImVec2(1920, 1080);
   left.WorkPos = ImVec2(0, 24);
   left.WorkSize = ImVec2(1920, 1056);
   left.DpiScale = 1.0f;

   ImGuiPlatformMonitor right;
   right.MainPos = ImVec2(1920, 0);
   right.MainSize = ImVec2(2560, 1440);
   right.WorkPos = ImVec2(1920, 24);
   right.WorkSize = ImVec2(2560, 1416);
   right.DpiScale = 1.25f;

   platform.Monitors.push_back(left);
   platform.Monitors.push_back(right);

   ImGuiViewport popup;
   popup.ID = 0x0F0F0001;
   popup.Pos = ImVec2(400, 300);
   popup.Size = ImVec2(320, 180);
   popup.FramebufferScale = ImVec2(1.25f, 1.25f);
   popup.DpiScale = 1.25f;
   platform.Viewports.push_back(&popup);

   ImGuiManager& ui = ImGuiManager::getInstance();

   // The first report: one line for the window, one per monitor, one per viewport.
   Captured first;
   ui.reportDisplayIfChanged();
   check(first.lines() == 1 + 2 + 2, "the first report is one line for the window, one per monitor, one per viewport, got " +
                                       std::to_string(first.lines()));
   check(first.contains("framebuffer 1.00x1.00"), "the window's framebuffer scale is reported");
   check(first.contains("2 monitor(s)"), "the monitor count is reported");
   check(first.contains("scale 1.25"), "a monitor's scale is reported");
   check(first.contains("(main)"), "the main window is marked as the main one");
   check(first.contains("(platform)"), "a popup is marked as a platform viewport");
   check(first.contains("dpi 1.25"), "a viewport's dpi is reported, which is what a popup is given");

   // The same geometry, a hundred frames later: nothing. A report that is written
   // every frame is a report nobody can read, and it is also what turns a
   // diagnostic into the reason the machine is slow.
   Captured quiet;
   for (int frame = 0; frame < 100; ++frame) {
      ui.reportDisplayIfChanged();
   }
   check(quiet.lines() == 0, "an unchanged display writes nothing over 100 calls, wrote " +
                                  std::to_string(quiet.lines()));

   // The window moved to the scaled monitor. The window's own size is unchanged,
   // which is the case a comparison of DisplaySize alone would miss. A change
   // rewrites the whole block rather than the line that changed: the lines are
   // only read next to each other, and a diff would be more code for less.
   io.DisplayFramebufferScale = ImVec2(1.25f, 1.25f);
   Captured moved;
   ui.reportDisplayIfChanged();
   check(moved.lines() == 5, "a change of framebuffer scale rewrites the report, wrote " + std::to_string(moved.lines()));
   check(moved.contains("framebuffer 1.25x1.25"), "the new scale is the one reported");

   // A window a pixel taller is a change too, and is reported.
   io.DisplaySize = ImVec2(1600, 901);
   Captured taller;
   ui.reportDisplayIfChanged();
   check(taller.lines() == 5, "a resized window rewrites the report, wrote " + std::to_string(taller.lines()));

   // And the case the whole thing is for: the popup's scale disagreeing with the
   // main window's, which is what a mixed-DPI popup looks like from in here.
   popup.FramebufferScale = ImVec2(2.0f, 2.0f);
   Captured popupScale;
   ui.reportDisplayIfChanged();
   check(popupScale.lines() == 5, "a popup whose scale changed rewrites the report, wrote " +
                                      std::to_string(popupScale.lines()));
   check(popupScale.contains("framebuffer 2.00x2.00"), "the popup's own scale is in the report");

   ImGui::DestroyContext();

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d checks failed\n", failures);
   return 1;
}
