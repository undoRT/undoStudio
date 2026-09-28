// The state files: what the IDE writes for itself between one run and the next.
//
// The format is small enough to be a mistake rather than a design problem, and
// the mistakes it invites are all about position: a key written above the section
// it belongs to, a second occurrence that looks like the value that was saved, a
// list that grows instead of being replaced. None of those show up until a second
// run, which is exactly when a user is not looking.

#include "undoStudio/core/Settings.hpp"
#include "undoStudio/core/ProjectManager.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using namespace undoStudio::core;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

static std::string readAll(const std::string& path) {
  std::ifstream in(path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void writeAll(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::trunc);
  out << text;
}

/// A working directory of its own, so the test never touches the real one.
static std::string makeWorkspace() {
  const fs::path dir = fs::temp_directory_path() / "undoStudio-settings-test";
  fs::remove_all(dir);
  fs::create_directories(dir);
  fs::current_path(dir);
  return dir.string();
}

int main() {
  makeWorkspace();
  const std::string file = "test.ini";

  // --- a value that was never written ---
  check(settings::getString(file, "window", "width") == "", "a missing value reads as nothing");
  check(settings::getInt(file, "window", "width", 1280) == 1280, "a missing integer falls back");

  // --- the first write creates the section, and the key goes inside it ---
  settings::setInt(file, "window", "width", 1600);
  const std::string afterWidth = readAll(file);
  check(afterWidth.find("[window]\nwidth=1600") != std::string::npos,
        "a key lands inside its own section, got '" + afterWidth + "'");
  check(settings::getInt(file, "window", "width", 0) == 1600, "and reads back");

  // --- a second key joins the section it is already there ---
  settings::setInt(file, "window", "height", 900);
  check(settings::getInt(file, "window", "height", 0) == 900, "a second key is written");
  check(readAll(file).find("[window]\nwidth=1600\nheight=900") != std::string::npos,
        "and lands in the same section, got '" + readAll(file) + "'");

  // --- a second section does not disturb the first ---
  settings::setString(file, "recent", "project", "/home/salvatore/prj2");
  const std::string withTwo = readAll(file);
  check(withTwo.find("[window]") != std::string::npos && withTwo.find("[recent]") != std::string::npos,
        "both sections are present, got '" + withTwo + "'");
  check(settings::getInt(file, "window", "width", 0) == 1600,
        "writing one section left the other's value alone");

  // --- writing the same key again replaces it rather than adding a second ---
  settings::setInt(file, "window", "width", 1920);
  check(settings::getInt(file, "window", "width", 0) == 1920, "a rewritten key takes the new value");
  size_t occurrences = 0;
  for (size_t at = readAll(file).find("width="); at != std::string::npos;
       at = readAll(file).find("width=", at + 1)) {
    ++occurrences;
  }
  check(occurrences == 1, "and appears once, not " + std::to_string(occurrences) + " times");

  // --- a value that is not a number is not read as one ---
  writeAll(file, "[window]\nwidth=16px\n");
  check(settings::getInt(file, "window", "width", 1280) == 1280, "\"16px\" is not read as 16");
  writeAll(file, "[window]\nwidth=\n");
  check(settings::getInt(file, "window", "width", 1280) == 1280, "an empty value falls back");

  // --- comments and blank lines survive a write ---
  writeAll(file, "# a comment the user wrote\n\n[window]\n# about the width\nwidth=1000\n");
  settings::setInt(file, "window", "height", 800);
  const std::string withComments = readAll(file);
  check(withComments.find("# a comment the user wrote") != std::string::npos,
        "a comment above the section survives, got '" + withComments + "'");
  check(withComments.find("# about the width") != std::string::npos, "so does one inside it");

  // --- a list is replaced whole, and keeps its order ---
  settings::setList(file, "recent", "project", {"/a", "/b", "/c"});
  check(settings::getList(file, "recent", "project").size() == 3, "a list of three is stored");
  check(settings::getList(file, "recent", "project")[0] == "/a", "in order");
  settings::setList(file, "recent", "project", {"/c", "/a"});
  const std::vector<std::string> replaced = settings::getList(file, "recent", "project");
  check(replaced.size() == 2, "a shorter list replaces the longer one, got " + std::to_string(replaced.size()));
  check(!replaced.empty() && replaced[0] == "/c" && replaced[1] == "/a", "with the order asked for");
  settings::setList(file, "recent", "project", {});
  check(settings::getList(file, "recent", "project").empty(), "an empty list clears it");

  // --- a value with a blank in it survives the round trip ---
  settings::setString(file, "project", "path", "/home/a b/prj");
  check(settings::getString(file, "project", "path") == "/home/a b/prj", "a value with a blank survives");

  // --- the recent list is capped, and the newest is first ---
  const fs::path projects = fs::path("projects");
  fs::create_directories(projects);
  ProjectManager& pm = ProjectManager::getInstance();
  for (int i = 0; i < static_cast<int>(ProjectManager::kMaxRecent) + 4; ++i) {
      const fs::path dir = projects / ("prj" + std::to_string(i));
      fs::create_directories(dir / ".undoProject");
      std::ofstream toml(dir / ".undoProject" / "project.toml");
      toml << "[project]\nname = \"prj" << i << "\"\n";
      toml.close();
      check(pm.openProject(dir.string()), "opened prj" + std::to_string(i));
      pm.closeProject();
  }
  const std::vector<std::string>& recent = pm.recentProjects();
  check(recent.size() == ProjectManager::kMaxRecent,
        "the list is capped at " + std::to_string(ProjectManager::kMaxRecent) + ", got " + std::to_string(recent.size()));
  check(!recent.empty() && recent.front().find("prj13") != std::string::npos,
        "the most recent is first, got '" + (recent.empty() ? std::string("none") : recent.front()) + "'");
  check(std::find(recent.begin(), recent.end(), std::string()) == recent.end(), "no empty entries");

  // --- reopening a project moves it rather than repeating it ---
  check(pm.openProject((projects / "prj0").string()), "reopened an older project");
  const std::vector<std::string>& afterReopen = pm.recentProjects();
  check(afterReopen.size() == ProjectManager::kMaxRecent, "the list did not grow");
  check(!afterReopen.empty() && afterReopen.front().find("prj0") != std::string::npos, "and it is at the head now");
  size_t times = 0;
  for (const std::string& entry : afterReopen) {
      if (entry.find("prj0") != std::string::npos) ++times;
  }
  check(times == 1, "the project is listed once, not " + std::to_string(times) + " times");

  // --- a project can be taken out of the list ---
  pm.forgetProject((projects / "prj0").string());
  check(pm.recentProjects().empty() ||
            std::find(pm.recentProjects().begin(), pm.recentProjects().end(), (projects / "prj0").string()) ==
                pm.recentProjects().end(),
        "a forgotten project is gone");

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}
