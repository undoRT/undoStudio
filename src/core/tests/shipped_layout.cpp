/**
 * @file shipped_layout.cpp
 * @brief What a first run of a fresh install opens on
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The archive carries resources/undoStudio_layout.ini and nothing else that holds
// state: undoStudio.ini, with the recent projects and the last window size, is
// written at run time and ignored in the repository. That is what makes a new
// install open on an empty history and a default-sized window rather than on the
// last developer's history and a window sized for their screen.
//
// It is a check on the shipped file rather than on the code, because the file is
// the thing that ships. It found one entry that had no business being there: the
// ImGui metrics window, saved by whoever happened to have it open.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

int main(int argc, char** argv) {
   const std::string root = (argc > 1) ? argv[1] : ".";
   const std::string path = root + "/resources/undoStudio_layout.ini";

   std::ifstream in(path);
   if (!in.good()) {
      std::printf("  FAIL cannot read %s\n", path.c_str());
      std::printf("RESULT: 1 check(s) failed\n");
      return 1;
   }
   std::stringstream buffer;
   buffer << in.rdbuf();
   const std::string layout = buffer.str();

   // --- it holds no run-time state ---
   //
   // The recent projects and the window size live in undoStudio.ini, not here, so
   // anything of that shape in this file is somebody's session that got saved
   // into the shipped copy.
   check(layout.find("[recent]") == std::string::npos,
         "the shipped layout holds no recent-projects section");
   check(layout.find("project=") == std::string::npos,
         "the shipped layout holds no project entries");
   check(layout.find("[window]") == std::string::npos,
         "the shipped layout holds no window size of its own");

   // --- no path off this machine ---
   //
   // The dead one is the one nobody notices: a line of text in a file nobody
   // opens, which only becomes visible on somebody else's machine, where the
   // recent list is full of paths that are not there.
   check(layout.find("/home/") == std::string::npos,
         "the shipped layout holds no home directory");
   check(layout.find("Desktop") == std::string::npos,
         "the shipped layout holds no desktop of a developer");

   // --- the ImGui metrics window is not part of the arrangement ---
   //
   // It appears when whoever saved it had it open, and it is not something a new
   // install should open on. ImGui ignores the entry rather than failing, which is
   // why it can sit there unnoticed.
   check(layout.find("Debug##Default") == std::string::npos,
         "the shipped layout does not carry ImGui's metrics window");

   // --- the arrangement is there ---
   check(layout.find("[Window][DockSpace]") != std::string::npos,
         "the shipped layout has a dockspace");
   check(layout.find("[Docking][Data]") != std::string::npos,
         "the shipped layout has the docking data that goes with it");

   // --- the panels the IDE registers are the ones it arranges ---
   //
   // Every kind of file opens in the one Editor panel, a .st included, so a
   // "ST Editor" here would be an arrangement for a panel that is not registered:
   // ImGui ignores the entry, and the editor lands on top of the Workspace in
   // whatever space it had left. The check is on the absence as much as on the
   // presence, because the stale entry is the one that goes unnoticed.
   for (const char* panel : {"Workspace", "Editor", "ST Output", "ST Outline"}) {
      check(layout.find(std::string("[Window][") + panel + "]") != std::string::npos,
            std::string("the shipped layout places the ") + panel + " panel");
   }
   check(layout.find("[Window][ST Editor]") == std::string::npos,
         "the shipped layout places no ST Editor panel, which is not registered");

   // --- the dockspace fits the window the IDE opens at ---
   //
   // The layout records absolute sizes, so a dockspace wider than the window that
   // opens around it is a layout that does not fit on the first run. The heights
   // are summed with the menu bar, which sits above the dockspace. These are the
   // defaults in WindowConfig; a change there is a change to what ships, and
   // having it fail here is what makes that deliberate.
   const size_t sizeAt = layout.find("[Window][DockSpace]");
   if (sizeAt != std::string::npos) {
      // Offsets are relative to sizeLine, not to layout: sizeAt is an index into
      // the whole file and using it here would slice past the end of the line.
      const size_t sizeKey = layout.find("Size=", sizeAt) - sizeAt;
      const size_t sizeEnd = layout.find('\n', sizeAt + sizeKey) - sizeAt;
      const std::string sizeLine = layout.substr(sizeAt + sizeKey, sizeEnd - sizeKey);
      // "Size=1280,680"
      const size_t comma = sizeLine.find(',');
      int width = 0;
      int height = 0;
      if (comma != std::string::npos) {
         width = std::stoi(sizeLine.substr(5, comma - 5));
         height = std::stoi(sizeLine.substr(comma + 1));
      }
      constexpr int kDefaultWindowWidth = 1280;
      constexpr int kDefaultWindowHeight = 720;
      constexpr int kMenuBarHeight = 40;
      check(width == kDefaultWindowWidth,
            "the dockspace is as wide as the window opens (" + std::to_string(width) + " against " +
                std::to_string(kDefaultWindowWidth) + ")");
      check(height + kMenuBarHeight == kDefaultWindowHeight,
            "the dockspace plus the menu bar is as tall as the window opens (" +
                std::to_string(height + kMenuBarHeight) + " against " +
                std::to_string(kDefaultWindowHeight) + ")");
   }

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}