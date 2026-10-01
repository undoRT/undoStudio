// The list of files opened recently, as the state file holds it.
//
// The tabs answer "what is open now". This list answers "what was open before",
// and it is the only place that answer survives: the editor has a single preview
// slot, so browsing a tree in it replaces what was there, and a file opened ten
// minutes ago is gone from every piece of UI the IDE has once it has been
// replaced. A user who closes the tree expecting to get a file back has no way to.
//
// What this covers is the file, because the list is only worth having if it
// outlives the process that wrote it. Everything that could go wrong here is
// silent: a list kept in memory but not written leaves a working menu that is
// empty every morning, which reads as a bug in the list rather than in the save.
//
// The round trip is read back through settings rather than through the list, which
// would answer from its cache and prove nothing: the cache is the thing under
// suspicion.

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "undoStudio/core/ProjectManager.hpp"
#include "undoStudio/core/RecentFiles.hpp"
#include "undoStudio/core/Settings.hpp"

using undoStudio::core::ProjectManager;
using undoStudio::core::RecentFiles;
namespace settings = undoStudio::core::settings;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

/// Read the list the way a second run of the IDE would: from the file, not the cache.
static std::vector<std::string> onDisk() {
   return settings::getList(settings::kCoreFile, "recent", "file");
}

static void writeFile(const fs::path& path) {
   fs::create_directories(path.parent_path());
   std::ofstream out(path);
   out << "METHOD FB : nome\nEND_PROGRAM\n";
}

/// How many times a path appears in the list, so "moves rather than repeats" is
/// answerable rather than assumed from the size.
static size_t occurrences(const std::vector<std::string>& list, const std::string& path) {
   size_t times = 0;
   for (const std::string& entry : list) {
      if (entry == path) ++times;
   }
   return times;
}

int main() {
   // A working directory of its own: these tests are about what is read and written
   // relative to the working directory, and one that picked up the developer's own
   // would be testing their session rather than the code.
   const fs::path work = fs::temp_directory_path() / "undoStudio-recent-files";
   fs::remove_all(work);
   fs::create_directories(work);
   fs::current_path(work);

   auto& recents = RecentFiles::getInstance();

   // --- a second run sees what the last one left ---
   //
   // The list is read once and then kept in memory, so the moment that read
   // happens is the whole of this bug: a fresh process holds an empty list, and
   // anything that asks before something else has loaded the file sees nothing.
   // The symptom is a list that is empty on the first start of the day and fills
   // in once a file has been opened, which reads as "my history was lost".
   //
   // Written newest first, which is the order the file is kept in.
   settings::setList(settings::kCoreFile, "recent", "file",
                     {"/work/progetti/tre.st", "/work/progetti/due.st", "/work/progetti/uno.st"});
   check(recents.recentFiles().size() == 3,
         "a fresh run sees the three files the last run left, got " +
             std::to_string(recents.recentFiles().size()));
   check(!recents.recentFiles().empty() && recents.recentFiles().front() == "/work/progetti/tre.st",
         "and the newest is first, got '" +
             (recents.recentFiles().empty() ? std::string("none") : recents.recentFiles().front()) + "'");

   // --- the recent projects are a different list in the same file ---
   //
   // Same section, different key. Sharing the section is what lets one write keep
   // the other's values; sharing the key would mean the two lists were one list.
   check(settings::getList(settings::kCoreFile, "recent", "project").empty(),
         "the recent projects are untouched by the file list");
   settings::setList(settings::kCoreFile, "recent", "project", {"/work/progetti/alpha"});
   check(recents.recentFiles().size() == 3,
         "and writing the project list leaves the file list alone, got " +
             std::to_string(recents.recentFiles().size()));

   recents.clearRecentFiles();
   check(recents.recentFiles().empty() && onDisk().empty(), "the list can be emptied");

   // --- a rename moves the entry, and does not make it the newest thing in the list ---
   //
   // The list is ordered by when a file was last looked at. Renaming one from an hour
   // ago is not looking at it again, so an entry that moved to the head would be
   // answering a question nobody asked. The other way round, a rename is how a stale
   // entry would appear: the path in the list would name a file that is not there.
   {
      const fs::path dir = work / "sposta";
      writeFile(dir / "uno.st");
      writeFile(dir / "due.st");
      writeFile(dir / "tre.st");
      recents.clearRecentFiles();
      recents.rememberFile((dir / "uno.st").string());
      recents.rememberFile((dir / "due.st").string());
      recents.rememberFile((dir / "tre.st").string());
      const std::vector<std::string> before = recents.recentFiles();

      fs::rename(dir / "uno.st", dir / "uno-rinominato.st");
      recents.renameFile((dir / "uno.st").string(), (dir / "uno-rinominato.st").string());
      check(recents.recentFiles().size() == before.size(),
            "a rename does not add an entry, got " + std::to_string(recents.recentFiles().size()));
      check(!recents.recentFiles().empty() && recents.recentFiles().back() == fs::canonical(dir / "uno-rinominato.st").string(),
            "and the entry points at the new path, got '" +
                (recents.recentFiles().empty() ? std::string("none") : recents.recentFiles().back()) + "'");
      check(recents.recentFiles().size() > 1 &&
                recents.recentFiles()[1] == fs::canonical(dir / "due.st").string(),
            "with everything else where it was");
      check(std::find(onDisk().begin(), onDisk().end(), fs::canonical(dir / "uno-rinominato.st").string()) != onDisk().end(),
            "and the new path is the one on disk");

      // A file moved into a name it already had in the list is one entry, not two.
      recents.rememberFile((dir / "uno-rinominato.st").string());
      fs::rename(dir / "due.st", dir / "uno-rinominato.st");
      recents.clearRecentFiles();
      recents.rememberFile((dir / "uno-rinominato.st").string());
      writeFile(dir / "uno-rinominato-2.st");
      recents.rememberFile((dir / "tre.st").string());
      recents.rememberFile((dir / "uno-rinominato-2.st").string());
      fs::rename(dir / "uno-rinominato-2.st", dir / "uno-rinominato.st");
      recents.renameFile((dir / "uno-rinominato-2.st").string(), (dir / "uno-rinominato.st").string());
      check(recents.recentFiles().size() == 2,
            "a move onto a path already in the list leaves one entry, got " +
                std::to_string(recents.recentFiles().size()));

      // A file nobody opened is not added by being renamed.
      const size_t sizeBefore = recents.recentFiles().size();
      recents.renameFile((dir / "mai-visto-a.st").string(), (dir / "mai-visto-b.st").string());
      check(recents.recentFiles().size() == sizeBefore,
            "renaming a file that was never opened changes nothing, got " +
                std::to_string(recents.recentFiles().size()));
      recents.clearRecentFiles();
   }

   // --- opening a file records it, and records it on disk ---
   const fs::path libDir = work / "progetto" / "lib";
   const fs::path main = libDir / "MAIN.st";
   const fs::path alu = libDir / "ALU.st";
   const fs::path json = work / "progetto" / "plc.json";
   writeFile(main);
   writeFile(alu);
   writeFile(json);

   recents.rememberFile(main.string());
   check(recents.recentFiles().size() == 1, "one file opened is one entry, got " +
                                               std::to_string(recents.recentFiles().size()));
   check(onDisk().size() == 1, "and it is on disk, got " + std::to_string(onDisk().size()));

   // --- the order is newest first ---
   recents.rememberFile(alu.string());
   check(recents.recentFiles().size() == 2 &&
             recents.recentFiles().front() == fs::canonical(alu).string(),
         "the file opened last is at the head, got '" +
             (recents.recentFiles().empty() ? std::string("none") : recents.recentFiles().front()) + "'");

   // --- opening the same file again moves it, and does not repeat it ---
   //
   // The list is ordered by when a file was last looked at, so a file already in it
   // moves to the head. An entry appearing twice is the first thing a user notices
   // about the list, and it also makes the limit count the same file twice.
   recents.rememberFile(main.string());
   check(recents.recentFiles().size() == 2, "opening a file already listed does not add an entry, got " +
                                                 std::to_string(recents.recentFiles().size()));
   check(recents.recentFiles().size() == 2 && recents.recentFiles().front() == fs::canonical(main).string(),
         "and the file looked at last is at the head");
   check(occurrences(recents.recentFiles(), fs::canonical(main).string()) == 1,
         "and it appears exactly once");

   // --- the same file reached by two names is one entry ---
   //
   // The editor hands over whatever path the click came from, and a project can be
   // reached through a symlink or a relative stretch. Two spellings of one file in
   // the list is one file, and it is a duplicate the user cannot tell apart.
   const fs::path alias = libDir / "alias.st";
   std::error_code ec;
   fs::create_symlink(alu, alias, ec);
   if (!ec) {
      recents.rememberFile(alias.string());
      check(recents.recentFiles().size() == 2, "the same file under another name is not a second entry, got " +
                                                   std::to_string(recents.recentFiles().size()));
   } else {
      std::printf("  skip  symlinks unavailable, the second-spelling check did not run\n");
   }

   // --- a file that is no longer there is still remembered ---
   //
   // This is a history, and the question it answers is "what did I open?", which a
   // deleted file has an answer to. Refusing to record it would lose the one entry
   // that would let a user find out the file they are looking for is the file they
   // deleted. The menu says "(missing)" next to it rather than pretending.
   const std::string gone = "/work/progetti/assenti/vecchio.st";
   recents.rememberFile(gone);
   check(occurrences(recents.recentFiles(), gone) == 1,
         "a file that no longer exists is recorded as it was given, found " +
             std::to_string(occurrences(recents.recentFiles(), gone)));
   check(occurrences(onDisk(), gone) == 1, "and is on disk, found " +
                                              std::to_string(occurrences(onDisk(), gone)));

   // --- nothing is not a file ---
   //
   // The editor logs and returns on an empty path rather than opening it, but a
   // caller that gets this far with nothing should not fill the list with it.
   const size_t before = recents.recentFiles().size();
   recents.rememberFile("");
   check(recents.recentFiles().size() == before, "an empty path is not recorded");

   // --- the limit ---
   //
   // Lowered here rather than only reached by opening more than ten files, because
   // a limit a user cannot reach deliberately is a limit they cannot change.
   // Trimmed at once: fifty entries of which forty-five appear in no list would go
   // on being written to the file forever.
   recents.setMaxRecentFiles(3);
   check(recents.maxRecentFiles() == 3, "the limit is stored, got " +
                                            std::to_string(recents.maxRecentFiles()));
   check(recents.recentFiles().size() == 3 && onDisk().size() == 3,
         "and lowering it leaves three, in memory and on disk, got " +
             std::to_string(recents.recentFiles().size()) + " and " + std::to_string(onDisk().size()));
   check(onDisk().size() == 3 && onDisk().front() == gone, "and the newest of the three is kept, not the oldest");

   // A limit reached by opening more files than fit keeps the newest.
   recents.rememberFile(main.string());
   check(recents.recentFiles().size() == 3, "opening past the limit does not grow the list, got " +
                                                std::to_string(recents.recentFiles().size()));
   check(recents.recentFiles().front() == fs::canonical(main).string(),
         "and what is kept is the file opened last");

   // --- the limit is clamped, not obeyed ---
   //
   // The file can be edited by hand between runs. A stored zero read back as zero
   // would make the list silently empty forever, with nothing on screen to say why.
   recents.setMaxRecentFiles(0);
   check(recents.maxRecentFiles() == RecentFiles::kMinRecentLimit,
         "a limit of zero is clamped up to one, got " + std::to_string(recents.maxRecentFiles()));
   recents.setMaxRecentFiles(100000);
   check(recents.maxRecentFiles() == RecentFiles::kMaxRecentLimit,
         "and a limit past the maximum is clamped down to it, got " +
             std::to_string(recents.maxRecentFiles()));

   // --- one limit for both lists ---
   //
   // The key is shared, so this is one "remember N" setting rather than two numbers
   // for the same question. If it were ever split, a user told to remember ten
   // things would have to be told which of the two numbers applied to which list.
   recents.setMaxRecentFiles(7);
   check(ProjectManager::getInstance().maxRecentProjects() == 7,
         "the recent projects answer to the same limit, got " +
             std::to_string(ProjectManager::getInstance().maxRecentProjects()));

   // --- forgetting one ---
   recents.rememberFile(json.string());
   const std::string jsonPath = fs::canonical(json).string();
   check(occurrences(recents.recentFiles(), jsonPath) == 1, "the file to forget is in the list");
   recents.forgetFile(jsonPath);
   check(occurrences(recents.recentFiles(), jsonPath) == 0, "and is gone afterwards");
   check(occurrences(onDisk(), jsonPath) == 0, "and is gone from the file too, found " +
                                                 std::to_string(occurrences(onDisk(), jsonPath)));

   // --- and the whole list ---
   recents.clearRecentFiles();
   check(recents.recentFiles().empty(), "the list can be emptied");
   check(onDisk().empty(), "and the file is emptied with it, got " + std::to_string(onDisk().size()));
   // The project list is a different key, so emptying this one must not reach it.
   check(settings::getList(settings::kCoreFile, "recent", "project").size() == 1,
         "and the project list is still there, got " +
             std::to_string(settings::getList(settings::kCoreFile, "recent", "project").size()));

   fs::current_path(fs::temp_directory_path());
   fs::remove_all(work);

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}