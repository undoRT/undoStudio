/**
 * @file open_target.cpp
 * @brief What a path handed to the IDE is taken for, at launch and on a drop
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The two things that can be dragged or typed in are a project and a single file,
// and they open in different places: a project replaces the workspace tree, a file
// goes to the editor that handles its extension. Getting this wrong is not a
// cosmetic mistake, because the wrong one is not an error but the other target,
// opened without a word.
//
// These checks drive the classification on real files in a temporary directory
// rather than on a stand-in, because every rule here is about the filesystem: a
// path that is not there is the interesting case, and no amount of matching on
// strings would find it.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "undoStudio/core/OpenTarget.hpp"

using undoStudio::core::OpenTargetKind;
using undoStudio::core::classifyOpenTarget;
using undoStudio::core::pathsFromCommandLine;

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

static void writeFile(const fs::path& path, const std::string& content = "x") {
   std::ofstream out(path);
   out << content;
}

/// argv as main() receives it: writable strings, since argv is char**.
static std::vector<std::string> argvStorage(const std::vector<std::string>& args) {
   return args;
}

static std::vector<std::string> pathsFrom(const std::vector<std::string>& args) {
   std::vector<std::string> store = argvStorage(args);
   std::vector<char*> argv;
   for (std::string& s : store) {
      argv.push_back(const_cast<char*>(s.c_str()));
   }
   argv.push_back(nullptr);
   return pathsFromCommandLine(static_cast<int>(store.size()), argv.data());
}

int main(int argc, char** argv) {
   const fs::path root = fs::temp_directory_path() / "undoStudio-open-target";
   fs::remove_all(root);
   fs::create_directories(root);

   // A project: a directory holding a config folder.
   const fs::path project = root / "myProject";
   fs::create_directories(project / ".undoProject");
   writeFile(project / ".undoProject" / "project.json", "{\"project\": {\"name\": \"myProject\"}}\n");
   writeFile(project / "Program.st", "PROGRAM x END_PROGRAM\n");

   // A single file outside any project.
   const fs::path loose = root / "loose.st";
   writeFile(loose);

   // A folder that is not a project, which is still a folder.
   const fs::path plainDir = root / "justAFolder";
   fs::create_directories(plainDir);

   const std::string missing = (root / "gone.st").string();

   // --- directories are projects ---
   check(classifyOpenTarget(project.string()) == OpenTargetKind::Project,
         "a directory is taken for a project");
   check(classifyOpenTarget(plainDir.string()) == OpenTargetKind::Project,
         "a folder with no config in it is still a folder, and opens as one");

   // --- the config folder names the same project ---
   check(classifyOpenTarget((project / ".undoProject").string()) == OpenTargetKind::Project,
         "naming the .undoProject folder names the project, not a file inside it");

   // --- files are files ---
   check(classifyOpenTarget(loose.string()) == OpenTargetKind::File,
         "a file is taken for a file");
   check(classifyOpenTarget((project / "Program.st").string()) == OpenTargetKind::File,
         "a file inside a project is a file, not the project");

   // --- what is not there is neither ---
   //
   // A drop can name something that has just been deleted, so this is not a
   // corner: returning Project here would have opened a project that is not there.
   check(classifyOpenTarget(missing) == OpenTargetKind::None,
         "a path that is not there is neither a project nor a file");
   check(classifyOpenTarget("") == OpenTargetKind::None,
         "an empty path is neither");

   // --- the command line ---
   check(pathsFrom({"undoStudio"}).empty(),
         "the program name alone names nothing");
   check(pathsFrom({"undoStudio", "/tmp/x.st"}).size() == 1,
         "one path after the program name is one path");
   check(pathsFrom({"undoStudio", "--verbose", "/tmp/x.st"}).size() == 1 &&
            pathsFrom({"undoStudio", "--verbose", "/tmp/x.st"})[0] == "/tmp/x.st",
         "a switch is skipped and does not take the path with it");
   check(pathsFrom({"undoStudio", "--", "/tmp/x.st"}).size() == 1,
         "a path after -- is a path, even when it starts with a dash");
   {
      const std::vector<std::string> got = pathsFrom({"undoStudio", "--", "-weird-name.st"});
      check(got.size() == 1 && got[0] == "-weird-name.st",
            "a path after -- may start with a dash and is still one");
   }
   {
      const std::vector<std::string> got = pathsFrom({"undoStudio", "a.st", "b.st"});
      check(got.size() == 2 && got[0] == "a.st" && got[1] == "b.st",
            "several paths are all returned, in the order given");
   }
   check(pathsFrom({"undoStudio", ""}).empty(), "an empty argument is not a path");
   check(pathsFrom({"undoStudio", "-"}).empty(), "a bare - is not a path");

   // A null argv is what a caller that never got one has, and reading it is how a
   // launcher that only wants the IDE started would crash on its first frame.
   check(pathsFromCommandLine(0, nullptr).empty(), "no arguments at all is nothing to open");

   // --- the two agree, which is the point of having one rule ---
   //
   // The command line and a drop both hand over a path and both go through the
   // same classifier, so the same file cannot open one way when typed and another
   // way when dragged.
   check(classifyOpenTarget(pathsFrom({"undoStudio", loose.string()})[0]) == OpenTargetKind::File
            && classifyOpenTarget(pathsFrom({"undoStudio", project.string()})[0]) == OpenTargetKind::Project,
         "a path from the command line classifies as it does when dropped");

   fs::remove_all(root);

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}