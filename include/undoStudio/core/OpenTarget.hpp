/**
 * @file OpenTarget.hpp
 * @brief Decides what a path given to the IDE at launch, or dropped on it, is.
 * @author Salvatore Bamundo
 * @date 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * A path can reach undoStudio from the command line or from a drop onto the
 * window, and it can name a project or a single file. Which one it is decides
 * what the IDE opens, so it is settled here rather than at the call site: the two
 * entry points have to agree, and a rule that is written twice is a rule that will
 * disagree.
 *
 * This is deliberately free of ImGui and of the undoApps. The workspace that owns
 * the tree is a plugin, and the core cannot call it, so the core can only say
 * what it was handed and let the plugin act; keeping the decision here means it
 * can be tested without either.
 */

#ifndef UNDOSTUDIO_CORE_OPENTARGET_HPP
#define UNDOSTUDIO_CORE_OPENTARGET_HPP

#include <string>
#include <vector>

namespace undoStudio::core {

/**
 * @brief What a path handed to the IDE names.
 */
enum class OpenTargetKind {
   None,    ///< Nothing usable: empty, or a path that does not exist.
   Project, ///< A project: a directory, or its .undoProject config folder.
   File,    ///< A file to open in the editor.
};

/**
 * @brief The config folder a project directory holds.
 *
 * A path ending in this is the project itself rather than a folder inside it,
 * because naming either of the two is what a user does and both mean the same
 * thing here.
 */
extern const char* const kProjectConfigDirName;

/**
 * @brief What a single path names.
 * @param path The path as the user wrote it, relative to the working directory or absolute.
 * @return The kind of target, and None for a path that is not there.
 *
 * A directory is taken for a project. That is what a folder dropped on a window
 * means, and a workspace folder is a project as far as undoStudio is concerned;
 * an ordinary folder is still openable as one.
 */
OpenTargetKind classifyOpenTarget(const std::string& path);

/**
 * @brief The targets named by a command line, in the order given.
 * @param argc The argument count, as handed to main().
 * @param argv The argument vector, as handed to main(); may be null.
 * @return One entry per path that names something, skipping switches.
 *
 * Switches are anything beginning with a dash, and so is anything after a bare
 * "--", which is where the paths go once something else has taken the switches:
 *   undoStudio workspace.proj
 *   undoStudio -- some/file.st
 * Every path is returned rather than only the first, so that dropping two files
 * at once and passing two files behave the same way.
 */
std::vector<std::string> pathsFromCommandLine(int argc, char* const argv[]);

} // namespace undoStudio::core

#endif // UNDOSTUDIO_CORE_OPENTARGET_HPP
