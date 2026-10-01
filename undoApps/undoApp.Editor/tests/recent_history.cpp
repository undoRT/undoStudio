// What the editor remembers, and which opens keep a tab.
//
// Two things are covered here, and they are covered together because they are the
// same decision seen from the two ends. The editor has one preview slot, so a file
// opened by browsing past it is gone from every piece of UI the moment the next click
// arrives — which is what makes clicking through twenty files bearable, and what makes
// the file you were looking for two minutes ago unreachable. So two answers to that:
// an open that names a file keeps its tab, and every open is remembered somewhere it
// can be asked for again.
//
// The list itself is covered by recent_files.cpp in the core, and the popup that shows
// it by recent_files_ui.cpp there too. What is only answerable from in here is that the
// editor feeds it: that every route into the editor passes through the one call that
// records, and that the routes which mean "I want this one" are the ones that pin.
//
// The tree's own branches are read as text rather than clicked. A click there would
// need the file tree built from a project on disk and a cursor placed on a row, and
// what it would prove is the same thing spelled out: which of the tree's opens ask to
// keep the tab. Reading the branch answers that question directly and cannot pass
// because a click landed somewhere else.

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#define private public
#include <TextEditor.h>
#include "undoAppEditor.hpp"
#undef private

#include "undoStudio/core/RecentFiles.hpp"
#include "undoStudio/core/Settings.hpp"

using namespace undoApp::Editor;
using undoStudio::core::RecentFiles;
using undoApp::ST::STApp;
namespace settings = undoStudio::core::settings;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

static void writeFile(const fs::path& p, const std::string& body) {
   fs::create_directories(p.parent_path());
   std::ofstream out(p);
   out << body;
}

/// Whether the text after a marker asks for the tab to be kept.
///
/// A window of a few hundred characters rather than the next line alone, so that the
/// argument may sit on the line after the call and the check is not about formatting.
static bool afterAsksToPin(const std::string& source, const std::string& marker) {
   const size_t at = source.find(marker);
   if (at == std::string::npos) {
      return false;
   }
   const std::string window = source.substr(at, 400);
   return window.find("pin=*/true") != std::string::npos;
}

int main() {
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::GetIO().Fonts->AddFontDefault();
   ImGui::GetIO().Fonts->Build();

   // A working directory of its own, before anything is written: the recent files list
   // is kept in the state file beside the working directory, and a test that ran in the
   // repository root would put its own entries in the developer's real history.
   const fs::path state = fs::temp_directory_path() / "undoStudio-recent-history";
   fs::remove_all(state);
   fs::create_directories(state);
   const fs::path cwd = fs::current_path();
   fs::current_path(state);

   const fs::path dir = fs::temp_directory_path() / "undoStudio-recent-history-files";
   fs::remove_all(dir);
   const fs::path a = dir / "Alpha.st";
   const fs::path b = dir / "Beta.st";
   writeFile(a, "FUNCTION_BLOCK Alpha\nVAR\n  av : INT;\nEND_VAR\nav := 1;\nEND_FUNCTION_BLOCK\n");
   writeFile(b, "PROGRAM Beta\nVAR\n  bv : INT;\nEND_VAR\nbv := 2;\nEND_PROGRAM\n");

   auto& app = EditorApp::getInstance();
   check(app.initialize(), "the editor app initializes");
   STApp::getInstance().setupEditors();

   auto& recents = RecentFiles::getInstance();
   recents.clearRecentFiles();

   // --- browsing is remembered ---
   app.openFile(a.string(), /*pin=*/false);
   check(recents.recentFiles().size() == 1, "opening a file records it, got " +
                                               std::to_string(recents.recentFiles().size()));
   check(!recents.recentFiles().empty() && recents.recentFiles().front() == fs::canonical(a).string(),
         "and it is recorded under its own name, got '" +
             (recents.recentFiles().empty() ? std::string("none") : recents.recentFiles().front()) + "'");

   // --- and it reached the file, not just the memory of this process ---
   //
   // Read through settings rather than through the list, which would answer from its
   // own cache and prove nothing about the file the next run will read.
   check(settings::getList(settings::kCoreFile, "recent", "file").size() == 1,
         "the entry is in the state file, got " +
             std::to_string(settings::getList(settings::kCoreFile, "recent", "file").size()) + " there");

   // --- browsing past it replaces the tab, and both are remembered ---
   app.openFile(b.string(), /*pin=*/false);
   check(app.m_open.size() == 1, "browsing past a file still takes the preview slot, got " +
                                     std::to_string(app.m_open.size()));
   check(recents.recentFiles().size() == 2, "and both files are remembered, got " +
                                                 std::to_string(recents.recentFiles().size()));
   check(recents.recentFiles().size() == 2 && recents.recentFiles().front() == fs::canonical(b).string(),
         "the newest first");

   // This is the case the list exists for: browsing has taken the file off screen and
   // out of the tab bar, and it is the one that was being looked at a moment ago.
   check(!app.m_open.isOpen(a.string()), "the file passed over is not left open in a tab");

   // --- a file looked at again moves rather than joining ---
   app.openFile(a.string(), /*pin=*/false);
   check(recents.recentFiles().size() == 2, "looking at a remembered file does not list it twice, got " +
                                                 std::to_string(recents.recentFiles().size()));
   check(recents.recentFiles().front() == fs::canonical(a).string(),
         "and it moves to the head, as the most recently looked at");

   // --- an open that names the file keeps its tab ---
   //
   // This is the second half of the same answer: the list is what makes a replaced file
   // reachable later, and pinning is what keeps it on screen in the meantime.
   app.openFile(a.string(), /*pin=*/true);
   check(app.m_open.pinnedCount() == 1, "an open that names the file keeps the tab, got " +
                                           std::to_string(app.m_open.pinnedCount()) + " pinned");
   app.openFile(b.string(), /*pin=*/false);
   check(app.m_open.size() == 2, "and browsing past it now leaves it open, got " +
                                     std::to_string(app.m_open.size()));
   check(app.m_open.isOpen(a.string()), "the named file is still there");

   // --- browsing is still browsing ---
   //
   // The rule is not "every open pins", which would put a tab back for every file
   // clicked past and make the preview slot pointless.
   app.openFile((dir / "Gamma.st").string(), /*pin=*/false);
   check(app.m_open.pinnedCount() == 1,
         "browsing does not pin what it passes over, got " + std::to_string(app.m_open.pinnedCount()) +
             " pinned");

   // --- the tree's branches ask for the right thing ---
   //
   // Read out of the editor's own source, found from this file's own path so that the
   // tree being read is the one that was built rather than a copy beside it.
   {
      const fs::path source = fs::path(__FILE__).parent_path().parent_path() / "src" / "undoAppEditor.cpp";
      std::ifstream in(source);
      const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      check(!text.empty(), "the editor's source is there to read, at " + source.string());

      check(afterAsksToPin(text, "ImGui::MenuItem(\"Open\")"),
            "Open in the tree's context menu keeps the tab");
      // A single click keeps its tab too, which is the whole change: the click used to
      // open a preview, so the file being read was gone from the bar as soon as
      // another was opened, and double clicking was the only way to hold on to it.
      const size_t single = text.find("openFile(node.path, /*pin=*/true);");
      check(single != std::string::npos, "a single click in the tree keeps the tab");
      if (single != std::string::npos) {
         const size_t branch = text.rfind("IsItemClicked(0)", single);
         check(branch != std::string::npos && branch < single,
               "and it is the click that does so");
      }
      // The other way round: a tree where every open pins is not what this checks for,
      // so the preview is still expressible, just not what a click asks for.
      check(!afterAsksToPin(text, "IsMouseDoubleClicked"),
            "there is no double click branch left deciding the same thing twice");
   }

   recents.clearRecentFiles();
   fs::current_path(cwd);
   fs::remove_all(dir);
   fs::remove_all(state);

   ImGui::DestroyContext();

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}