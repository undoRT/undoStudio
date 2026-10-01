/**
 * @file RecentFiles.hpp
 * @brief The files opened recently, newest first
 * @ingroup core
 *
 * The tabs answer "what is open now". This answers "what was open before", which is
 * a different question and the one a file list cannot answer: the editor's tab bar
 * holds one preview slot, so browsing a tree in it replaces what was there, and a
 * file that was opened ten minutes ago and has since been replaced is gone from
 * every piece of UI the IDE has.
 *
 * So the list is kept here, in the core, rather than in whichever undoApp happens to
 * open the file. It outlives the plugin that wrote it, and it is the shape a user
 * expects: newest first, an entry that was already there moves rather than
 * repeating, and a limit.
 *
 * It lives in the same state file as the recent projects, under a different key, and
 * shares their limit. Sharing one limit rather than keeping two is deliberate: the
 * key is `max`, it has one owner per read, and both lists answer to the same
 * "remember N" setting. A second key in the same section would be a second number
 * for the same question, and a user asked to remember ten things would have to be
 * told which of the two numbers applied.
 *
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace undoStudio {
namespace core {

/**
 * @brief Which files were opened recently
 *
 * A singleton, because the list belongs to the IDE and not to any one undoApp: the
 * menu that shows it is the core's, and a plugin that kept its own copy would write
 * a list that the menu could not see.
 */
class RecentFiles
{
public:
   /// @brief The one instance
   static RecentFiles& getInstance();

   /**
    * @brief Record that a file was opened
    *
    * Moves rather than repeats: the list is ordered by when a file was last looked
    * at, and an entry appearing twice is the first thing a user notices about it.
    *
    * The path is canonicalised so that one file reached by two routes is one entry.
    * A path that does not resolve — a file deleted between being listed and being
    * opened — is recorded as it was given rather than dropped: the list is a
    * history, and refusing to remember something is a worse answer than remembering
    * it in a form that no longer opens.
    *
    * @param filePath Absolute path of the file that was opened
    */
   void rememberFile(const std::string& filePath);

   /**
    * @brief Forget one file
    * @param filePath Path to drop from the list
    */
   void forgetFile(const std::string& filePath);

   /**
    * @brief Point a remembered file at where it moved to
    *
    * In place, keeping its position, so a rename does not make an old file the
    * newest thing in the list. A deleted file is left alone instead: the list is a
    * history, and the missing mark is the honest answer for one that is gone.
    *
    * @param oldPath The path the entry was recorded under
    * @param newPath Where the file now is
    */
   void renameFile(const std::string& oldPath, const std::string& newPath);

   /// @brief Empty the list
   void clearRecentFiles();

   /**
    * @brief The files opened recently, newest first
    *
    * Loads the list from the state file the first time it is asked for, so a fresh
    * process sees the entries a previous one left. Reading is const and mutates the
    * cache, which is why the cache is mutable and why the flag saying whether it is
    * loaded is too.
    *
    * @return The remembered paths, possibly empty
    */
   const std::vector<std::string>& recentFiles() const;

   /**
    * @brief How many files to remember
    * @return The limit, never below one and never above the maximum
    */
   size_t maxRecentFiles() const;

   /**
    * @brief Set how many files to remember, trimming the list at once
    *
    * Trimmed here rather than at the next rememberFile(), because a limit lowered
    * from fifty to five should leave five entries rather than fifty of which
    * forty-five appear in no list and go on being written to the file.
    *
    * @param count The limit to store, clamped to the allowed range
    */
   void setMaxRecentFiles(size_t count);

   /// @brief The smallest limit that may be stored
   static constexpr size_t kMinRecentLimit = 1;

   /// @brief The largest limit that may be stored
   static constexpr size_t kMaxRecentLimit = 100;

private:
   RecentFiles() = default;

   /// @brief Read the list from disk unless it has been read already
   /// @brief Canonical path, or the one as given when it does not resolve
   static std::string canonicalOrAsGiven(const std::string& filePath);

   void loadRecents() const;

   /// Remembered files, newest first. Mutable because reading the list fills it.
   mutable std::vector<std::string> m_recentFiles;

   /// Whether the list has been read from disk in this process
   mutable bool m_recentsLoaded = false;
};

} // namespace core
} // namespace undoStudio