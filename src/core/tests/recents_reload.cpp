// The recent list as a second run of the IDE sees it.
//
// The list is read from the state file once and then kept in memory. What this
// covers is the moment that read happens, because getting it wrong loses the list
// on screen without touching the file: a fresh process has an empty list in
// memory, and anything that reads the list before something else has loaded it
// sees nothing.
//
// The symptom is that the list is empty on the first start of the day and fills
// in once a project has been opened. That reads as "my history was lost" and
// sends people looking for a setting that was never changed, when the entries are
// sitting in the file the whole time.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "undoStudio/core/ProjectManager.hpp"
#include "undoStudio/core/Settings.hpp"

using undoStudio::core::ProjectManager;
namespace settings = undoStudio::core::settings;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

/// Write the state file a first run would have left behind.
static void writeState(const std::vector<std::string>& projects) {
   settings::setList(settings::kCoreFile, "recent", "project", projects);
}

int main() {
   // A working directory of its own: these tests are about what is read from the
   // state file, and one that picked up the developer's own would be testing
   // their session rather than the code.
   const fs::path work = fs::temp_directory_path() / "undoStudio-recents-reload";
   fs::remove_all(work);
   fs::create_directories(work);
   std::filesystem::current_path(work);

   // --- the first thing a second run does is look at the list ---
   //
   // Nothing has been opened yet, so nothing has loaded the file. This is the
   // whole of the bug: the accessor hands back what is in memory without asking
   // for the file.
   //
   // Written newest first, which is the order the file is kept in: opening a
   // project puts it at the head, so "tre" was the last one opened and "uno" the
   // first of these three.
   writeState({"/work/progetti/tre", "/work/progetti/due", "/work/progetti/uno"});
   check(settings::getList(settings::kCoreFile, "recent", "project").size() == 3,
         "three projects are in the state file to begin with");

   auto& pm = ProjectManager::getInstance();
   check(pm.recentProjects().size() == 3,
         "a fresh run sees the three projects the last run left, got " +
             std::to_string(pm.recentProjects().size()));

   // --- the order is the one they were left in ---
   check(pm.recentProjects().size() == 3 && pm.recentProjects().front() == "/work/progetti/tre",
         "and the newest is first, got '" +
             (pm.recentProjects().empty() ? std::string("none") : pm.recentProjects().front()) + "'");

   // --- asking twice does not load twice, nor reorder ---
   {
      const std::vector<std::string> first = pm.recentProjects();
      const std::vector<std::string> second = pm.recentProjects();
      check(first.size() == 3 && second.size() == 3 && first == second,
            "reading the list again gives the same thing");
   }

   // --- opening a project does not lose the ones that were there ---
   pm.rememberProject("/work/progetti/nuovo");
   check(pm.recentProjects().size() == 4,
         "opening a project keeps the ones already listed, got " +
             std::to_string(pm.recentProjects().size()));
   check(pm.recentProjects().size() == 4 && pm.recentProjects().front() == "/work/progetti/nuovo",
         "and the new one is at the head");
   {
      size_t times = 0;
      for (const std::string& entry : pm.recentProjects()) {
         if (entry == "/work/progetti/tre") ++times;
      }
      check(times == 1, "and none of the old ones is repeated, found " + std::to_string(times));
   }

   // --- and it survives a round trip through the file ---
   //
   // Read the file back directly rather than through the manager, which would
   // answer from the cache and prove nothing.
   const std::vector<std::string> onDisk = settings::getList(settings::kCoreFile, "recent", "project");
   check(onDisk.size() == 4, "the four are on disk, got " + std::to_string(onDisk.size()));
   check(onDisk.size() == 4 && onDisk.front() == "/work/progetti/nuovo",
         "newest first on disk too, got '" + (onDisk.empty() ? std::string("none") : onDisk.front()) + "'");

   std::filesystem::current_path(fs::temp_directory_path());
   fs::remove_all(work);

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}