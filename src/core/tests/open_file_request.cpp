// A path handed to the IDE has to reach the editor that opens it.
//
// The core cannot open a file: the editor that does is a plugin, and a plugin is
// loaded at run time, so the core can only leave a request and the undoApp that
// owns the workspace picks it up. That crossing is the whole of what this covers.
//
// It is worth a test because the failure is silent in both directions. A request
// that is never taken opens nothing and says nothing. A request taken twice opens
// the file twice, and the second open is the one that looks like a bug, because
// the first one worked. Neither shows up on screen as an error.
//
// The queue is a queue because a drop can carry several files and they have to be
// opened in the order they were given: the last one dropped is the one the user
// was still holding, and it is the one they expect to see.

#include <imgui.h>

#include <cstdio>
#include <string>

#define private public
#include "undoStudio/ui/ImGuiManager.hpp"
#undef private

using undoStudio::ui::ImGuiManager;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

/// Drain the queue the way the Workspace panel does, one file per iteration.
static std::vector<std::string> drain(ImGuiManager& mgr) {
   std::vector<std::string> taken;
   while (true) {
      std::string path;
      const std::string* next = mgr.consumeOpenFileRequest(path);
      if (next == nullptr) {
         break;
      }
      taken.push_back(*next);
   }
   return taken;
}

int main() {
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
   ImGui::GetIO().Fonts->AddFontDefault();
   ImGui::GetIO().Fonts->Build();

   auto& mgr = ImGuiManager::getInstance();

   // --- nothing asked for ---
   check(drain(mgr).empty(), "an empty queue hands nothing to the panel");

   // --- one file ---
   mgr.requestOpenFile("/tmp/one.st");
   {
      const std::vector<std::string> taken = drain(mgr);
      check(taken.size() == 1 && taken[0] == "/tmp/one.st", "a file asked for is handed over");
   }
   check(drain(mgr).empty(), "and is not handed over a second time");

   // --- order ---
   //
   // A drop of several files: the one the user let go of last is the one that
   // should be on screen, which is the last one taken.
   mgr.requestOpenFile("/tmp/a.st");
   mgr.requestOpenFile("/tmp/b.st");
   mgr.requestOpenFile("/tmp/c.st");
   {
      const std::vector<std::string> taken = drain(mgr);
      check(taken.size() == 3, "three files asked for are all handed over");
      check(taken.size() == 3 && taken[0] == "/tmp/a.st" && taken[1] == "/tmp/b.st" && taken[2] == "/tmp/c.st",
            "and in the order they were given");
   }

   // --- a path that is empty is not a file to open ---
   //
   // Opening "" is how a file manager handing over a selection with nothing in it
   // ends up logging an error every frame the panel looks.
   mgr.requestOpenFile("");
   check(drain(mgr).empty(), "an empty path is not queued");

   // --- the queue does not grow without bound ---
   //
   // A shell glob or a directory dropped from a file manager can name thousands of
   // files, and holding them until the panel gets to them is a way to spend a lot
   // of memory on something nobody asked to open. What is kept is a prefix of what
   // was asked for, so the first files still open.
   // The message for each discarded file is what the flood is meant to produce,
   // and four thousand lines of it would bury the checks. The stream is turned
   // aside for the duration and put back after, so the message is still produced
   // by the same code path and only the volume is hidden.
   std::fflush(stderr);
   std::FILE* kept = std::freopen("/dev/null", "w", stderr);
   for (int i = 0; i < 5000; ++i) {
      mgr.requestOpenFile("/tmp/f" + std::to_string(i) + ".st");
   }
   if (kept != nullptr) {
      std::fflush(stderr);
   }
   {
      const std::vector<std::string> taken = drain(mgr);
      check(taken.size() > 0 && taken.size() < 5000,
            "a flood of files is bounded rather than all held: " + std::to_string(taken.size()) + " kept");
      check(taken.size() > 0 && taken[0] == "/tmp/f0.st",
            "and what is kept is the first of them, in order");
   }

   // --- a project request is a separate queue, not one of these ---
   //
   // They are separate because they are taken by different calls: a project
   // rebuilds the tree and a file goes to the editor, and mixing them would let
   // the panel open a folder as a file.
   mgr.requestOpenProject("/tmp/someProject");
   mgr.requestOpenFile("/tmp/a.st");
   {
      std::string projectPath;
      const std::string* project = mgr.consumeOpenProjectRequest(projectPath);
      check(project != nullptr && *project == "/tmp/someProject", "a project asked for is handed over");
      const std::vector<std::string> files = drain(mgr);
      check(files.size() == 1 && files[0] == "/tmp/a.st", "and the file asked for is still separate");
      std::string none;
      check(mgr.consumeOpenProjectRequest(none) == nullptr, "taking the project twice yields nothing");
   }

   ImGui::DestroyContext();

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}