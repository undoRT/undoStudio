/**
 * @file RecentFiles.cpp
 * @brief The files opened recently, newest first
 * @ingroup core
 */

#include "undoStudio/core/RecentFiles.hpp"

#include "undoStudio/core/Settings.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace undoStudio {
namespace core {

namespace fs = std::filesystem;

/// Default maximum number of remembered files. Ten matches the project list, and is
/// small enough that the menu does not need a scrollbar on most screens.
static constexpr size_t kDefaultMaxRecentFiles = 10;

RecentFiles& RecentFiles::getInstance()
{
   static RecentFiles instance;
   return instance;
}

void RecentFiles::loadRecents() const
{
   if (m_recentsLoaded) {
      return;
   }
   m_recentsLoaded = true;
   m_recentFiles = settings::getList(settings::kCoreFile, "recent", "file");
}

const std::vector<std::string>& RecentFiles::recentFiles() const
{
   loadRecents();
   return m_recentFiles;
}

size_t RecentFiles::maxRecentFiles() const
{
   // Clamped on the way out as well as on the way in: the file can be edited by
   // hand between runs, and a limit of zero read back would silently make the list
   // empty forever with nothing to say why.
   const int stored = settings::getInt(settings::kCoreFile, "recent", "max", static_cast<int>(kDefaultMaxRecentFiles));
   if (stored < static_cast<int>(kMinRecentLimit)) {
      return kMinRecentLimit;
   }
   if (stored > static_cast<int>(kMaxRecentLimit)) {
      return kMaxRecentLimit;
   }
   return static_cast<size_t>(stored);
}

void RecentFiles::setMaxRecentFiles(size_t count)
{
   size_t clamped = count;
   if (clamped < kMinRecentLimit) {
      clamped = kMinRecentLimit;
   }
   if (clamped > kMaxRecentLimit) {
      clamped = kMaxRecentLimit;
   }
   settings::setInt(settings::kCoreFile, "recent", "max", static_cast<int>(clamped));
   loadRecents();
   if (m_recentFiles.size() > clamped) {
      m_recentFiles.resize(clamped);
      settings::setList(settings::kCoreFile, "recent", "file", m_recentFiles);
   }
}

std::string RecentFiles::canonicalOrAsGiven(const std::string& filePath)
{
   std::error_code ec;
   if (!fs::exists(filePath, ec)) {
      // Does not resolve, so keep the path as it was given: the list is a history
      // and a stale entry is the expected answer to "what did I open?"
      return filePath;
   }
   // Canonical, so that the same file reached by two paths appears once.
   const fs::path canon = fs::canonical(filePath, ec);
   return ec ? filePath : canon.string();
}

void RecentFiles::rememberFile(const std::string& filePath)
{
   if (filePath.empty()) {
      return;
   }

   const std::string toRecord = canonicalOrAsGiven(filePath);

   loadRecents();

   // Newest first, and a file that was already in the list moves rather than
   // joining: the list is ordered by when a file was last looked at, and an entry
   // appearing twice is the thing a user notices first.
   m_recentFiles.erase(std::remove(m_recentFiles.begin(), m_recentFiles.end(), toRecord),
                       m_recentFiles.end());
   m_recentFiles.insert(m_recentFiles.begin(), toRecord);

   const size_t limit = maxRecentFiles();
   if (m_recentFiles.size() > limit) {
      m_recentFiles.resize(limit);
   }
   settings::setList(settings::kCoreFile, "recent", "file", m_recentFiles);
}

void RecentFiles::forgetFile(const std::string& filePath)
{
   if (filePath.empty()) {
      return;
   }
   loadRecents();
   const std::string toDrop = canonicalOrAsGiven(filePath);
   const size_t before = m_recentFiles.size();
   m_recentFiles.erase(std::remove(m_recentFiles.begin(), m_recentFiles.end(), toDrop),
                       m_recentFiles.end());
   if (m_recentFiles.size() != before) {
      settings::setList(settings::kCoreFile, "recent", "file", m_recentFiles);
   }
}

void RecentFiles::renameFile(const std::string& oldPath, const std::string& newPath)
{
   if (oldPath.empty() || newPath.empty()) {
      return;
   }
   loadRecents();
   const std::string from = canonicalOrAsGiven(oldPath);
   const std::string to = canonicalOrAsGiven(newPath);

   // In place, and not by forgetting and remembering: the list is ordered by when a
   // file was last looked at, and renaming a file from an hour ago must not make it
   // the newest thing in it. An entry that is not there is not added, because a
   // rename of a file nobody opened is not something this list has an opinion about.
   const auto entry = std::find(m_recentFiles.begin(), m_recentFiles.end(), from);
   if (entry == m_recentFiles.end()) {
      return;
   }
   size_t index = static_cast<size_t>(std::distance(m_recentFiles.begin(), entry));

   // The file may already be in the list under the name it was moved to, from a
   // previous move. One entry per file is the whole point of the list, and the older
   // of the two is the one that goes: this one has just been looked at by renaming it.
   const auto existing = std::find(m_recentFiles.begin(), m_recentFiles.end(), to);
   if (existing != m_recentFiles.end()) {
      const size_t other = static_cast<size_t>(std::distance(m_recentFiles.begin(), existing));
      m_recentFiles.erase(m_recentFiles.begin() + static_cast<long>(other));
      if (other < index) {
         --index;
      }
   }
   m_recentFiles[index] = to;
   settings::setList(settings::kCoreFile, "recent", "file", m_recentFiles);
}

void RecentFiles::clearRecentFiles()
{
   loadRecents();
   m_recentFiles.clear();
   settings::setList(settings::kCoreFile, "recent", "file", m_recentFiles);
}

} // namespace core
} // namespace undoStudio