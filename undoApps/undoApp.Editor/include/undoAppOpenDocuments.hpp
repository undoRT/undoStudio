/**
 * @file undoAppOpenDocuments.hpp
 * @brief Which files are open, and which of them is only a preview.
 * @ingroup undoAppEditor
 *
 * @author undoStudio
 * @date 2026
 * @copyright SPDX-License-Identifier: GPL-3.0-or-later
 *
 * undoStudio opens one file at a time: opening a second one closed the first. The
 * backends hold a single document each, so what is missing is not a tab bar but
 * somewhere to record that several files are open at once and, for each, the state
 * to put back when it is shown again.
 *
 * This is that record, and it is deliberately free of ImGui and of the backends:
 * the decisions worth checking are which tab a new file lands in and what happens
 * to the one that was in it, and those are the ones that can quietly lose somebody's
 * work.
 *
 * The rule it implements is the one VS Code uses, and the reason for it is that a
 * file manager makes opening files cheap. Clicking through twenty files to find the
 * one you wanted would be intolerable if each click left a tab behind, so a file
 * opened that way takes the single preview slot and the next one takes its place.
 * A tab becomes permanent — pinned — the moment it earns it: you edit it, you
 * open it deliberately rather than by browsing, or you move it.
 *
 * A preview that has unsaved changes is pinned rather than dropped. Replacing it
 * would throw those changes away, and doing that silently, on a gesture as
 * ordinary as clicking the next file in a tree, is not something a file editor
 * gets to do.
 */

#ifndef UNDOSTUDIO_UNDOAPP_OPENDOCUMENTS_HPP
#define UNDOSTUDIO_UNDOAPP_OPENDOCUMENTS_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace undoApp {

/**
 * @brief Which backend a document belongs to
 *
 * Mirrors the EditorApp::FileType it is mapped to. Duplicated rather than included
 * because that enum lives in a header that pulls in the whole plugin, and this one
 * has to be checkable on its own.
 */
enum class DocKind
{
   None,
   ST,
   JSON,
   Text,
   Cpp
};

/**
 * @brief One file that is open
 */
struct OpenDocument
{
   std::string path;      ///< Absolute path
   DocKind kind = DocKind::None;
   bool pinned = false;   ///< Permanent: a preview is not replaced
   bool dirty = false;    ///< Has changes not written to disk
};

/**
 * @brief What opening a file did, so the caller knows what to put back
 *
 * The caller owns the editors, so it has to know whether it is switching away from
 * something and whether anything just disappeared. Deciding that here and only
 * reporting it keeps the decision in one place: a second place that guessed would
 * eventually disagree with this one about which tab vanished.
 */
struct OpenOutcome
{
   bool alreadyOpen = false;    ///< The file was already open and was focused
   bool droppedPreview = false; ///< The preview slot's previous occupant went away
   std::string droppedPath;     ///< The file that went away, empty if none did
};

/**
 * @brief The set of open files, at most one of them a preview
 */
class OpenDocuments
{
public:
   /// @brief Open a file, taking the preview slot
   ///
   /// A file that is already open is focused rather than opened twice: the tabs
   /// are where the user looks to see what is open, and a file appearing in two of
   /// them would make that a worse answer than no tabs at all.
   OpenOutcome open(const std::string& path, DocKind kind)
   {
      OpenOutcome outcome;
      if (path.empty()) {
         return outcome;
      }
      for (OpenDocument& doc : m_documents) {
         if (doc.path == path) {
            m_active = path;
            outcome.alreadyOpen = true;
            return outcome;
         }
      }

      // The outgoing preview. There is only ever one, which is what makes browsing
      // a tree cheap: it is a slot that the next file takes rather than a tab per
      // file passed over.
      //
      // A dirty preview cannot be reached here. setDirty() pins a document the
      // moment it gains changes, so anything still in the slot has none to lose.
      for (size_t i = 0; i < m_documents.size(); ++i) {
         if (m_documents[i].pinned) {
            continue;
         }
         outcome.droppedPath = m_documents[i].path;
         m_documents.erase(m_documents.begin() + static_cast<long>(i));
         outcome.droppedPreview = true;
         break;
      }

      OpenDocument doc;
      doc.path = path;
      doc.kind = kind;
      m_documents.push_back(doc);
      m_active = path;
      return outcome;
   }

   /// @brief Make a document permanent
   /// @return Whether it was open
   bool pin(const std::string& path)
   {
      for (OpenDocument& doc : m_documents) {
         if (doc.path == path) {
            doc.pinned = true;
            return true;
         }
      }
      return false;
   }

   /// @brief Record whether a document has changes not written to disk
   ///
   /// A preview that has just been edited is pinned: it now holds work, and the
   /// next click in the tree must not be able to discard it.
   void setDirty(const std::string& path, bool dirty)
   {
      for (OpenDocument& doc : m_documents) {
         if (doc.path != path) {
            continue;
         }
         doc.dirty = dirty;
         if (dirty) {
            doc.pinned = true;
         }
         return;
      }
   }

   /// @brief Record that a document's changes have been written
   void setClean(const std::string& path) { setDirty(path, false); }

   /// @brief Close a document
   /// @return Whether it was open, and what to show afterwards
   struct CloseOutcome
   {
      bool closed = false;
      std::string nextActive;
      bool activeWasClosed = false;
   };

   CloseOutcome close(const std::string& path)
   {
      CloseOutcome outcome;
      for (size_t i = 0; i < m_documents.size(); ++i) {
         if (m_documents[i].path != path) {
            continue;
         }
         outcome.activeWasClosed = (m_active == path);
         outcome.closed = true;
         m_documents.erase(m_documents.begin() + static_cast<long>(i));
         if (outcome.activeWasClosed) {
            // The tab to the left, or the first one left, so closing does not jump
            // to whatever happens to be at the end of the list.
            m_active = m_documents.empty() ? std::string()
                                           : m_documents[(i > 0) ? i - 1 : 0].path;
            outcome.nextActive = m_active;
         }
         return outcome;
      }
      return outcome;
   }

   /// @brief Follow a file that was renamed or moved on disk
   ///
   /// The path is the identity of a document here: it is the tab's ImGui id and the
   /// key every stash is held under, so it has to move with the file, or the tab bar
   /// shows a name that is not there and a save writes a second copy where the file
   /// used to be. The document keeps its place in the list, its mark and whether it is
   /// pinned: it is the same file, and this is not a save.
   ///
   /// @param oldPath The path the document was opened under
   /// @param newPath The path it now has
   /// @return Whether a document was open under oldPath
   bool rename(const std::string& oldPath, const std::string& newPath)
   {
      if (newPath.empty() || oldPath == newPath || isOpen(newPath)) {
         return false;
      }
      for (OpenDocument& doc : m_documents) {
         if (doc.path != oldPath) {
            continue;
         }
         doc.path = newPath;
         if (m_active == oldPath) {
            m_active = newPath;
         }
         return true;
      }
      return false;
   }

   /// @brief Bring a document to the front without reopening it
   bool activate(const std::string& path)
   {
      for (const OpenDocument& doc : m_documents) {
         if (doc.path == path) {
            m_active = path;
            return true;
         }
      }
      return false;
   }

   /// @brief Close every document that has changes
   /// @return How many were closed, and how many were dirty
   struct DirtySummary
   {
      size_t closed = 0;
      size_t dirty = 0;
   };

   DirtySummary closeAll()
   {
      DirtySummary summary;
      for (const OpenDocument& doc : m_documents) {
         if (doc.dirty) {
            ++summary.dirty;
         }
      }
      summary.closed = m_documents.size();
      m_documents.clear();
      m_active.clear();
      return summary;
   }

   const std::vector<OpenDocument>& documents() const { return m_documents; }
   const std::string& active() const { return m_active; }
   size_t size() const { return m_documents.size(); }

   /// @brief Whether a document is open
   bool isOpen(const std::string& path) const
   {
      for (const OpenDocument& doc : m_documents) {
         if (doc.path == path) {
            return true;
         }
      }
      return false;
   }

   /// @brief How many documents are pinned
   size_t pinnedCount() const
   {
      size_t count = 0;
      for (const OpenDocument& doc : m_documents) {
         if (doc.pinned) {
            ++count;
         }
      }
      return count;
   }

   /**
    * @brief Move to the next or previous document, wrapping round
    *
    * Moves the active tab rather than only reporting where it would go: this is
    * what Ctrl+Tab does, and a version that answered the question without acting
    * on it would leave the shortcut doing nothing.
    *
    * @param direction 1 for the next, -1 for the previous
    * @return The document now active, or nullptr when there are none
    */
   const std::string* cycle(int direction)
   {
      if (m_documents.empty()) {
         return nullptr;
      }
      int index = -1;
      for (size_t i = 0; i < m_documents.size(); ++i) {
         if (m_documents[i].path == m_active) {
            index = static_cast<int>(i);
            break;
         }
      }
      const int count = static_cast<int>(m_documents.size());
      // With nothing active, "next" starts at the beginning and "previous" at the
      // end, so either key goes somewhere rather than jumping to the first twice.
      int next = (index < 0) ? (direction >= 0 ? 0 : count - 1) : (index + direction + count) % count;
      m_active = m_documents[static_cast<size_t>(next)].path;
      return &m_active;
   }

private:
   std::vector<OpenDocument> m_documents;
   std::string m_active;
};

} // namespace undoApp

#endif // UNDOSTUDIO_UNDOAPP_OPENDOCUMENTS_HPP
