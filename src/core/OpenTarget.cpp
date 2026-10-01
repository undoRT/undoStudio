/**
 * @file OpenTarget.cpp
 * @brief Decides what a path given to the IDE at launch, or dropped on it, is.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoStudio/core/OpenTarget.hpp"

#include <filesystem>

namespace undoStudio::core {

const char* const kProjectConfigDirName = ".undoProject";

OpenTargetKind classifyOpenTarget(const std::string& path)
{
   if (path.empty()) {
      return OpenTargetKind::None;
   }

   namespace fs = std::filesystem;

   std::error_code ec;
   const fs::path asPath(path);

   // is_directory and is_regular_file report through the error_code overloads, so
   // a path that cannot be read says so here instead of throwing. A drop from a
   // file manager can name something that has just been deleted, and this is on
   // the frame the drop happened.
   const bool isDir = fs::is_directory(asPath, ec);
   if (ec) {
      return OpenTargetKind::None;
   }
   if (isDir) {
      return OpenTargetKind::Project;
   }

   // Naming the config folder and naming the folder holding it mean the same
   // project, and both are what a user does: the second from a file manager that
   // was showing hidden files, the first from a script.
   if (asPath.filename() == kProjectConfigDirName) {
      return OpenTargetKind::Project;
   }

   if (fs::is_regular_file(asPath, ec) && !ec) {
      return OpenTargetKind::File;
   }

   return OpenTargetKind::None;
}

std::vector<std::string> pathsFromCommandLine(int argc, char* const argv[])
{
   std::vector<std::string> paths;
   if (argv == nullptr) {
      return paths;
   }

   bool afterSeparator = false;
   for (int i = 1; i < argc; ++i) {
      if (argv[i] == nullptr) {
         continue;
      }
      const std::string arg(argv[i]);
      if (arg.empty()) {
         continue;
      }
      if (afterSeparator) {
         paths.push_back(arg);
         continue;
      }
      if (arg == "--") {
         afterSeparator = true;
         continue;
      }
      // A switch, not a path. A bare "-" is the conventional "read from stdin" and
      // is left out with the rest: undoStudio has no stdin to read.
      if (arg[0] == '-') {
         continue;
      }
      paths.push_back(arg);
   }
   return paths;
}

} // namespace undoStudio::core
