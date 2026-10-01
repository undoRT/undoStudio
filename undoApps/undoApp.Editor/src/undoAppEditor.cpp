/**
 * @file undoAppEditor.cpp
 * @brief Main entry point for the unified undoApp.Editor plugin
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * This file implements the plugin entry point and routes the single
 * public openFile(path) call to the right backend (ST, JSON, Text)
 * based on the file extension.
 */

#include "undoAppEditor.hpp"
// For the three calls that draw the Structured Text document: this file's own
// header is the app that owns the Editor panel, and the ST editor is a backend
// of it like the others. Both headers are included here rather than in each
// other, so the plugin does not depend on which one a caller happens to reach
// first.
#include "undoAppST.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"
#include "undoStudio/core/Application.hpp"
#include "undoStudio/core/ProjectManager.hpp"
#include "undoStudio/core/RecentFiles.hpp"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>

#ifdef USE_TINYFILEDIALOGS
#include <tinyfiledialogs.h>
#endif

namespace fs = std::filesystem;

namespace undoApp {
namespace Editor {

// ============================================================================
// Static Helper Functions
// ============================================================================

namespace {

/**
 * @brief Move a stashed document from one path to another
 *
 * Both places a stash holds the path are updated, not just the key: the key is how
 * the document is found, and the path inside it is what the backend is handed when
 * the document comes back, so a rename that moved only the key would still write to
 * the old path the next time the file was opened.
 *
 * @tparam Stashes A map of path to a document type carrying a path of its own
 * @param stashes The stash map to re-key
 * @param oldPath The key the document is filed under
 * @param newPath Where the file now is
 */
template <typename Stashes>
void rekeyStash(Stashes& stashes, const std::string& oldPath, const std::string& newPath)
{
   const auto it = stashes.find(oldPath);
   if (it == stashes.end()) {
      return;
   }
   typename Stashes::mapped_type moved = std::move(it->second);
   stashes.erase(it);
   moved.path = newPath;
   stashes.emplace(newPath, std::move(moved));
}

} // namespace

/**
 * @brief Lowercase a string in-place
 */
std::string EditorApp::toLower(std::string s)
{
   std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return s;
}

void EditorApp::expandToPath(FileNode& node, const std::string& targetPath)
{
   if (targetPath.rfind(node.path, 0) == 0) {
      node.expanded = true;
      for (auto& child : node.children) {
         expandToPath(child, targetPath);
      }
   }
}

/**
 * @brief Trim whitespace from a string
 */
std::string trim(const std::string& str)
{
   size_t first = str.find_first_not_of(" \t\n\r");
   if (first == std::string::npos) {
      return "";
   }
   size_t last = str.find_last_not_of(" \t\n\r");
   return str.substr(first, last - first + 1);
}

/**
 * @brief Convert file type to string for logging
 */
const char* fileTypeToString(FileType type)
{
   switch (type) {
   case FileType::ST:
      return "ST";
   case FileType::JSON:
      return "JSON";
   case FileType::Text:
      return "Text";
   case FileType::Cpp:
      return "C/C++";
   case FileType::None:
      return "None";
   }
   return "?";
}

// ============================================================================
// Singleton Instance
// ============================================================================

EditorApp& EditorApp::getInstance()
{
   static EditorApp instance;
   return instance;
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

bool EditorApp::initialize()
{
   if (m_initialized) {
      return true;
   }
   std::cout << "[undoApp.Editor] Initializing..." << std::endl;

   // Initialize all backends
   ST::STApp::getInstance().initialize();
   JSON::JSONApp::getInstance().initialize();
   TextApp::getInstance().initialize();
   CppApp::getInstance().initialize();

   registerPanels();
   m_initialized = true;
   std::cout << "[undoApp.Editor] Initialization complete" << std::endl;
   return true;
}

void EditorApp::shutdown()
{
   if (!m_initialized) {
      return;
   }
   std::cout << "[undoApp.Editor] Shutting down..." << std::endl;

   closeFile();

   // Shut down backends
   ST::STApp::getInstance().shutdown();
   JSON::JSONApp::getInstance().shutdown();
   TextApp::getInstance().shutdown();
   CppApp::getInstance().shutdown();

   // Remove panels
   auto& mgr = undoStudio::ui::ImGuiManager::getInstance();
   mgr.removePanel("Editor");
   mgr.removePanel("Workspace");

   m_initialized = false;
   std::cout << "[undoApp.Editor] Shutdown complete" << std::endl;
}

void EditorApp::registerPanels()
{
   auto& mgr = undoStudio::ui::ImGuiManager::getInstance();
   mgr.addPanel("Editor", std::bind(&EditorApp::renderEditorPanel, this));
   mgr.addPanel("Workspace", std::bind(&EditorApp::renderWorkspacePanel, this));
   std::cout << "[undoApp.Editor] Panels registered" << std::endl;
}

// ============================================================================
// File routing
// ============================================================================

void EditorApp::openFile(const std::string& path, bool pin)
{
   if (path.empty()) {
      std::cerr << "[undoApp.Editor] openFile called with empty path" << std::endl;
      return;
   }

   // Every route into the editor goes through here, so this is the one place that
   // knows a file was looked at. Recorded whether or not it was already open: the
   // list is ordered by when a file was last looked at, and a user who switched
   // back to a file is looking at it however long it had been sitting there.
   undoStudio::core::RecentFiles::getInstance().rememberFile(path);

   // Decide which backend to dispatch to based on the file extension
   std::string ext = toLower(fs::path(path).extension().string());

   DocKind kind = DocKind::Text;
   if (ext == ".st") {
      kind = DocKind::ST;
   } else if (ext == ".json" && !undoStudio::core::ProjectManager::getInstance().isConfigFile(path)) {
      kind = DocKind::JSON;
   } else if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" || ext == ".hpp" || ext == ".hh"
              || ext == ".hxx") {
      kind = DocKind::Cpp;
   }
   // A project's own configuration is JSON, and so would go to the JSON viewer, which
   // draws a tree and has nothing to type into. These are the files the user is told
   // to edit by hand -- exports.json holds the PROGRAM order, a task file its cycle
   // time -- so they are opened as text, which is what they were before the format
   // changed. Every other .json still gets the viewer.

   openDocument(path, kind, pin);
}

void EditorApp::openDocument(const std::string& path, DocKind kind, bool pin)
{
   // Already open: bring it forward rather than loading it a second time. The tabs
   // are where someone looks to see what is open, and a file in two of them makes
   // that a worse answer than no tabs at all.
   if (m_open.isOpen(path)) {
      switchToTab(path);
      // Pinned after the switch rather than instead of it, because a deliberate
      // re-open of something already in the preview slot should keep it there.
      if (pin) {
         m_open.pin(path);
      }
      return;
   }

   // Whatever is on screen goes back into its own tab before the next file lands
   // in the editor that is currently holding it.
   //
   // The stash is taken first, but a preview that open() is about to replace is
   // not kept: the rule that decides whether it survives lives in the model, and
   // this only puts its text somewhere in case it does. A preview with unsaved
   // changes is pinned by the model, not here, so that the rule has one home.
   stashActiveDocument(/*pin=*/false);

   const OpenOutcome outcome = m_open.open(path, kind);
   if (!outcome.alreadyOpen) {
      m_open.activate(path);
      loadActiveDocument(path);
   }
   if (pin) {
      m_open.pin(path);
   }

   std::cout << "[undoApp.Editor] Opened " << path << " as " << fileTypeToString(m_currentFileType) << std::endl;
}

// ============================================================================
// Open documents
// ============================================================================

DocKind EditorApp::docKindFor(FileType type)
{
   switch (type) {
   case FileType::ST:
      return DocKind::ST;
   case FileType::JSON:
      return DocKind::JSON;
   case FileType::Text:
      return DocKind::Text;
   case FileType::Cpp:
      return DocKind::Cpp;
   default:
      return DocKind::None;
   }
}

FileType EditorApp::fileTypeFor(DocKind kind)
{
   switch (kind) {
   case DocKind::ST:
      return FileType::ST;
   case DocKind::JSON:
      return FileType::JSON;
   case DocKind::Text:
      return FileType::Text;
   case DocKind::Cpp:
      return FileType::Cpp;
   default:
      return FileType::None;
   }
}

/// Where a document goes while another one is in its editor.
void EditorApp::stashActiveDocument(bool keep)
{
   const std::string active = m_open.active();
   if (active.empty()) {
      return;
   }
   const size_t index = documentIndex(active);
   if (index == kNoIndex) {
      return;
   }
   switch (m_open.documents()[index].kind) {
   case DocKind::ST:
      m_stStashes[active] = ST::STApp::getInstance().takeDocument();
      break;
   case DocKind::Text: {
      StashedText out;
      TextApp::getInstance().document(out.path, out.text);
      m_textStashes[active] = std::move(out);
      break;
   }
   case DocKind::Cpp: {
      StashedText out;
      CppApp::getInstance().document(out.path, out.text);
      m_cppStashes[active] = std::move(out);
      break;
   }
   case DocKind::JSON:
      m_jsonStashes[active] = JSON::JSONApp::getInstance().takeDocument();
      break;
   default:
      break;
   }
   // Keeping the tab is not the same question as whether it has unsaved changes,
   // and conflating them is what put a mark on every file that was ever looked at
   // twice: switching tabs used to mean "there are changes here", so every tab got
   // an asterisk that nothing ever took off. The mark comes from the editors, every
   // frame, in refreshDirtyState().
   if (keep) {
      m_open.pin(active);
   }
}

void EditorApp::refreshDirtyState()
{
   // Only the document on screen is asked. The rest are stashes -- their text lives
   // in a map, not in an editor -- so whatever they were when they were put there is
   // the truth about them until they come back.
   const std::string active = m_open.active();
   if (active.empty()) {
      return;
   }
   const size_t index = documentIndex(active);
   if (index == kNoIndex) {
      return;
   }

   bool changed = false;
   switch (m_open.documents()[index].kind) {
   case DocKind::ST:
      changed = ST::STApp::getInstance().hasUnsavedChanges();
      break;
   case DocKind::Text:
      changed = TextApp::getInstance().hasUnsavedChanges();
      break;
   case DocKind::Cpp:
      changed = CppApp::getInstance().hasUnsavedChanges();
      break;
   case DocKind::JSON:
      // A JSON file is shown as a tree with no text of its own to edit, so there is
      // nothing here that could differ from the file.
      changed = false;
      break;
   default:
      break;
   }

   // setDirty() pins a document the moment it gains changes, which is what stops a
   // preview holding somebody's typing from being reused by the next browse.
   // Setting
   // it to false does not unpin: a tab that was pinned for having work keeps its
   // place after the work is saved, which is what a tab somebody opened on purpose
   // should do.
   m_open.setDirty(active, changed);
}

void EditorApp::saveActiveDocument()
{
   // One place that knows how to write each kind, because the key that reaches the
   // editor panel is not the key that reaches the ST editor's own panel, and a save
   // that only handled what the panel under the keyboard could dispatch left .st
   // files unsaveable from it.
   switch (m_currentFileType) {
   case FileType::ST:
      ST::STApp::getInstance().saveCurrentFile();
      break;
   case FileType::Cpp:
      CppApp::getInstance().saveFile();
      break;
   case FileType::Text:
      TextApp::getInstance().saveFile();
      break;
   default:
      // JSON has no editor text to write back, and nothing open is not an error.
      return;
   }
   // The backends drop their own flag once the write went through; this is the same
   // answer reached from here, so the tab stops claiming otherwise without waiting
   // for the next frame.
   refreshDirtyState();
}

void EditorApp::loadActiveDocument(const std::string& path)
{
   if (path.empty()) {
      m_currentFilePath.clear();
      m_currentFileType = FileType::None;
      return;
   }
   const size_t index = documentIndex(path);
   if (index == kNoIndex) {
      m_currentFilePath.clear();
      m_currentFileType = FileType::None;
      return;
   }

   m_currentFilePath = path;
   m_currentFileType = fileTypeFor(m_open.documents()[index].kind);

   switch (m_open.documents()[index].kind) {
   case DocKind::ST: {
      ST::STApp& st = ST::STApp::getInstance();
      const auto it = m_stStashes.find(path);
      if (it != m_stStashes.end()) {
         // Back from a stash: the editors already hold this file's text.
         st.setDocument(it->second);
      } else {
         st.openFile(path);
      }
      break;
   }
   case DocKind::Text: {
      const auto it = m_textStashes.find(path);
      if (it != m_textStashes.end()) {
         TextApp::getInstance().setDocument(it->second.path, it->second.text);
      } else {
         TextApp::getInstance().openFile(path);
      }
      break;
   }
   case DocKind::Cpp: {
      const auto it = m_cppStashes.find(path);
      if (it != m_cppStashes.end()) {
         CppApp::getInstance().setDocument(it->second.path, it->second.text);
      } else {
         CppApp::getInstance().openFile(path);
      }
      break;
   }
   case DocKind::JSON: {
      const auto it = m_jsonStashes.find(path);
      if (it != m_jsonStashes.end()) {
         JSON::JSONApp::getInstance().setDocument(it->second);
      } else {
         JSON::JSONApp::getInstance().loadJSONFile(path);
      }
      break;
   }
   default:
      break;
   }
}

void EditorApp::switchToTab(const std::string& path)
{
   if (path == m_open.active()) {
      return;
   }
   // Switching tabs is not browsing: the tab being left is not a preview about to
   // be reused, so it keeps whatever it holds and its unsaved mark stays.
   stashActiveDocument(/*pin=*/true);
   m_open.activate(path);
   loadActiveDocument(path);
}

void EditorApp::closeTab(const std::string& path)
{
   if (!m_open.isOpen(path)) {
      return;
   }
   const bool wasActive = (m_open.active() == path);
   const std::string next = wasActive ? neighbourAfterClose(path) : std::string();

   dropStashes(path);
   m_open.close(path);

   if (wasActive) {
      if (next.empty()) {
         m_open.activate("");
         m_currentFilePath.clear();
         m_currentFileType = FileType::None;
      } else {
         m_open.activate(next);
         loadActiveDocument(next);
      }
   }
}

size_t EditorApp::documentIndex(const std::string& path) const
{
   if (path.empty()) {
      return kNoIndex;
   }
   for (size_t i = 0; i < m_open.documents().size(); ++i) {
      if (m_open.documents()[i].path == path) {
         return i;
      }
   }
   return kNoIndex;
}

std::string EditorApp::neighbourAfterClose(const std::string& path) const
{
   for (size_t i = 0; i < m_open.documents().size(); ++i) {
      if (m_open.documents()[i].path == path) {
         if (m_open.documents().size() == 1) {
            return std::string();
         }
         return m_open.documents()[(i > 0) ? i - 1 : 0].path;
      }
   }
   return std::string();
}


void EditorApp::closeFile()
{
    ST::STApp::getInstance().closeFile();
    JSON::JSONApp::getInstance().closeFile();
    TextApp::getInstance().closeFile();
    CppApp::getInstance().closeFile();
    m_currentFileType = FileType::None;
    m_currentFilePath.clear();
}

/// @brief Forget everything stashed for a path
///
/// An empty path means every stash, which is the only way to say "nothing is open"
/// to a set of maps keyed by path.
void EditorApp::dropStashes(const std::string& path)
{
   if (path.empty()) {
      m_stStashes.clear();
      m_textStashes.clear();
      m_cppStashes.clear();
      m_jsonStashes.clear();
      return;
   }
   m_stStashes.erase(path);
   m_textStashes.erase(path);
   m_cppStashes.erase(path);
   m_jsonStashes.erase(path);
}

/// @brief Close every tab under a path, including the files inside a folder
///
/// Deleting a folder takes the files in it, and a tab for a file that is gone is a
/// row that offers to open something that cannot be opened.
void EditorApp::closeDocumentsUnder(const std::string& path)
{
   if (path.empty()) {
      return;
   }
   const std::string prefix = path + "/";
   std::vector<std::string> gone;
   for (const OpenDocument& doc : m_open.documents()) {
      if (doc.path == path || doc.path.rfind(prefix, 0) == 0) {
         gone.push_back(doc.path);
      }
   }
   // closeTab rather than a close per path, because it is the one that answers what to
   // leave on screen: the model moves its active document to a neighbour, and
   // something has to load it or the bar claims a file is open that the editors are
   // not holding.
   for (const std::string& doc : gone) {
      closeTab(doc);
   }
}

/// @brief Follow a file that was renamed or moved on disk
///
/// Everything that holds a path has to be told, and it is this one function that
/// knows the list: the tab and its stash are keyed by path, the editor on screen
/// saves to the path it was given, and the recent files list points at a file that
/// is no longer there. Each of them left behind is not a stale label but a second
/// file: a save after a rename writes a copy where the file used to be.
void EditorApp::followPathChange(const std::string& oldPath, const std::string& newPath)
{
   if (oldPath.empty() || newPath.empty() || oldPath == newPath) {
      return;
   }

   // A folder carries the files inside it, so renaming one moves their tabs too: a
   // tab whose path no longer resolves is a row offering to open a file that is not
   // there, and a save after the move writes a copy at the old path.
   const std::string prefix = oldPath + "/";
   std::vector<std::pair<std::string, std::string>> moved;
   for (const OpenDocument& doc : m_open.documents()) {
      if (doc.path == oldPath) {
         moved.emplace_back(doc.path, newPath);
      } else if (doc.path.rfind(prefix, 0) == 0) {
         moved.emplace_back(doc.path, newPath + doc.path.substr(oldPath.size()));
      }
   }

   for (const auto& fromTo : moved) {
      const std::string& from = fromTo.first;
      const std::string& to = fromTo.second;

      undoStudio::core::RecentFiles::getInstance().renameFile(from, to);

      const size_t index = documentIndex(from);
      if (index == kNoIndex) {
         continue;
      }
      const DocKind kind = m_open.documents()[index].kind;
      const bool wasActive = (m_open.active() == from);

      // The editor on screen saves to the path it was given, so it is told before
      // anything else: this is the one that would write a second file.
      switch (kind) {
      case DocKind::ST:
         if (wasActive) {
            ST::STApp::getInstance().renameFileTo(to);
         }
         break;
      case DocKind::Text:
         if (wasActive) {
            TextApp::getInstance().renameFileTo(to);
         }
         break;
      case DocKind::Cpp:
         if (wasActive) {
            CppApp::getInstance().renameFileTo(to);
         }
         break;
      case DocKind::JSON:
         // No text to write back, so nothing saves to the old path.
         break;
      default:
         break;
      }

      // The stashes are keyed by path as well, and each document carries the path
      // inside it: both are the file, so both move.
      rekeyStash(m_stStashes, from, to);
      rekeyStash(m_textStashes, from, to);
      rekeyStash(m_cppStashes, from, to);
      rekeyStash(m_jsonStashes, from, to);

      if (m_open.rename(from, to) && wasActive) {
         m_currentFilePath = to;
      }
   }
}

// ============================================================================
// Workspace Management
// ============================================================================

void EditorApp::loadWorkspace(const std::string& path)
{
   m_workspacePath = path;
   m_isProjectMode = false;
   m_rootNode.name = fs::path(path).filename().string();
   m_rootNode.path = path;
   m_rootNode.isDirectory = true;
   m_rootNode.expanded = true;
   buildFileTree(m_rootNode, path);
   std::cout << "[undoApp.Editor] Workspace loaded: " << path << std::endl;
}

bool EditorApp::isProjectOpen() const
{
   return undoStudio::core::ProjectManager::getInstance().isProjectOpen();
}

void EditorApp::createProject(const std::string& parentDir, const std::string& name)
{
   auto& pm = undoStudio::core::ProjectManager::getInstance();
   if (!pm.createProject(parentDir, name)) {
      std::cerr << "[undoApp.Editor] Failed to create project: " << name << std::endl;
      return;
   }
   m_isProjectMode = true;
   m_workspacePath = pm.getProjectPath();
   m_rootNode.name = pm.getProjectName();
   m_rootNode.path = m_workspacePath;
   m_rootNode.isDirectory = true;
   m_rootNode.expanded = true;
   buildFileTree(m_rootNode, m_workspacePath);
}

void EditorApp::openProject(const std::string& projectDir)
{
   auto& pm = undoStudio::core::ProjectManager::getInstance();
   if (!pm.openProject(projectDir)) {
      std::cerr << "[undoApp.Editor] Failed to open project: " << projectDir << std::endl;
      return;
   }
   m_isProjectMode = true;
   m_workspacePath = pm.getProjectPath();
   m_rootNode.name = pm.getProjectName();
   m_rootNode.path = m_workspacePath;
   m_rootNode.isDirectory = true;
   m_rootNode.expanded = true;
   buildFileTree(m_rootNode, m_workspacePath);
}

void EditorApp::closeProject()
{
   undoStudio::core::ProjectManager::getInstance().closeProject();
   m_isProjectMode = false;
   m_workspacePath.clear();
   m_rootNode = FileNode{};
   closeFile();

   // The tabs go with the project. They used to be left behind: the next project
   // opened showed the previous one's files in the bar, and clicking one of them asked
   // a backend for a file that was not there any more. closeFile() alone cannot do
   // this, because it also means "the file on screen is gone", where the other tabs
   // have to stay.
   dropStashes(std::string());
   m_open.closeAll();
}

void EditorApp::refreshFileTree()
{
   if (!m_workspacePath.empty()) {
      buildFileTree(m_rootNode, m_workspacePath);
   }
}

void EditorApp::openFileAsText(const std::string& path)
{
   if (path.empty()) {
      std::cerr << "[undoApp.Editor] openFileAsText called with empty path" << std::endl;
      return;
   }

   std::cout << "[undoApp.Editor] Opening JSON as text: " << path << std::endl;

   // A file that is already open with another backend is one file, and it gets one
   // tab: the tab bar answers "what is open", and a second row for the same path is
   // a worse answer than the same row now holding the text. Its stash goes with it,
   // because it was stashed under the backend that is being dropped and its text is
   // the file, which is on disk either way.
   const size_t index = documentIndex(path);
   if (index != kNoIndex && m_open.documents()[index].kind != DocKind::Text) {
      const bool wasActive = (m_open.active() == path);
      dropStashes(path);
      m_open.close(path);
      if (wasActive) {
         // Nothing is on screen now, and openDocument below is what puts the text
         // there. Leaving the JSON backend loaded would leave two of them holding one
         // file, and only one of them is ever drawn.
         JSON::JSONApp::getInstance().closeFile();
         m_currentFilePath.clear();
         m_currentFileType = FileType::None;
      }
   }

   openDocument(path, DocKind::Text, /*pin=*/true);
}

void EditorApp::openFileAsTree(const std::string& path)
{
   if (path.empty()) {
      std::cerr << "[undoApp.Editor] openFileAsTree called with empty path" << std::endl;
      return;
   }

   std::cout << "[undoApp.Editor] Opening JSON in the tree viewer: " << path << std::endl;

   // The mirror of openFileAsText, and the same reason: a file is one file and gets
   // one tab, so a document already open with another backend is closed and reopened
   // with this one. Its stash goes with it, for the same reason as there: it was
   // stashed under the backend being dropped, and the text it holds is the file,
   // which is on disk either way.
   const size_t index = documentIndex(path);
   if (index != kNoIndex && m_open.documents()[index].kind != DocKind::JSON) {
      // The kind is read before the tab is closed, because afterwards there is no
      // tab left to read it from, and it is what says which backend to let go of.
      const DocKind dropped = m_open.documents()[index].kind;
      const bool wasActive = (m_open.active() == path);
      dropStashes(path);
      m_open.close(path);
      if (wasActive) {
         // Nothing is on screen now, and openDocument below is what puts the tree
         // there. Leaving the other backend loaded would leave two of them holding
         // one file, and only one of them is ever drawn.
         switch (dropped) {
         case DocKind::Text:
            TextApp::getInstance().closeFile();
            break;
         case DocKind::Cpp:
            CppApp::getInstance().closeFile();
            break;
         default:
            break;
         }
         m_currentFilePath.clear();
         m_currentFileType = FileType::None;
      }
   }

   openDocument(path, DocKind::JSON, /*pin=*/true);
}

void EditorApp::buildFileTree(FileNode& node, const std::string& rootPath)
{
   node.children.clear();
   auto& pm = undoStudio::core::ProjectManager::getInstance();
   try {
      for (const auto& entry : fs::directory_iterator(rootPath)) {
         FileNode child;
         child.name = entry.path().filename().string();
         child.path = entry.path().string();
         child.isDirectory = entry.is_directory();
         child.role = pm.isProjectOpen() ? pm.classifyPath(child.path) : NodeRole::Generic;

         if (entry.is_directory()) {
            buildFileTree(child, entry.path().string());
         }
         node.children.push_back(std::move(child));
      }
      // Sort: directories first, then alphabetical
      std::sort(node.children.begin(), node.children.end(), [](const FileNode& a, const FileNode& b) {
         if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
         }
         return a.name < b.name;
      });
   } catch (const std::exception& e) {
      std::cerr << "[undoApp.Editor] buildFileTree: " << e.what() << std::endl;
   }
}

void EditorApp::createNewFile(const std::string& parentPath, const std::string& name)
{
   std::string baseName = name;
   size_t dotPos = baseName.find_last_of('.');
   if (dotPos != std::string::npos) {
      baseName = baseName.substr(0, dotPos);
   }

   fs::path newPath = fs::path(parentPath) / name;
   if (fs::exists(newPath)) {
      std::cerr << "[undoApp.Editor] File already exists: " << newPath.string() << std::endl;
      return;
   }

   std::stringstream content;
   switch (m_newPOUType) {
   case ST::POUType::Program:
      content << "PROGRAM " << baseName << "\n";
      content << "VAR\n";
      content << "END_VAR\n";
      break;
   case ST::POUType::FunctionBlock:
      content << "FUNCTION_BLOCK " << baseName << "\n";
      content << "VAR_INPUT\n";
      content << "END_VAR\n";
      content << "VAR_OUTPUT\n";
      content << "END_VAR\n";
      content << "VAR_IN_OUT\n";
      content << "END_VAR\n";
      content << "VAR\n";
      content << "END_VAR\n";
      break;
   case ST::POUType::Function:
      content << "FUNCTION " << baseName << " : " << m_newPOUReturnType << "\n";
      content << "VAR_INPUT\n";
      content << "END_VAR\n";
      content << "VAR_OUTPUT\n";
      content << "END_VAR\n";
      content << "VAR_IN_OUT\n";
      content << "END_VAR\n";
      content << "VAR\n";
      content << "END_VAR\n";
      break;
   }
   content << "\n\n";

   switch (m_newPOUType) {
   case ST::POUType::Program:
      content << "END_PROGRAM\n";
      break;
   case ST::POUType::FunctionBlock:
      content << "END_FUNCTION_BLOCK\n";
      break;
   case ST::POUType::Function:
      content << "END_FUNCTION\n";
      break;
   }

   std::ofstream file(newPath.string());
   if (file.is_open()) {
      file << content.str();
      file.close();
      std::cout << "[undoApp.Editor] Created file: " << newPath.string() << std::endl;
      openFile(newPath.string(), /*pin=*/true);
      refreshFileTree();
      expandToPath(m_rootNode, parentPath);
   } else {
      std::cerr << "[undoApp.Editor] Failed to create file: " << newPath.string() << std::endl;
   }
}

void EditorApp::createNewFolder(const std::string& parentPath, const std::string& name)
{
   fs::path newPath = fs::path(parentPath) / name;
   if (!fs::exists(newPath)) {
      fs::create_directory(newPath);
      std::cout << "[undoApp.Editor] Created folder: " << newPath.string() << std::endl;
      refreshFileTree();
   }
}

void EditorApp::deleteFile(const std::string& path)
{
   // Only the flag is set here; the panel that owns the modal is what opens it.
   // OpenPopup hashes its id against the current ID stack and this is reached from
   // renderFileTree, where that stack is deeper than where the modal is drawn, so
   // the two ids would not match. An entry with no matching Begin also stays on
   // ImGui's open stack for good and swallows every Escape pressed afterwards.
   m_deletePendingPath = path;
   m_showDeleteConfirmation = true;
}

void EditorApp::renameFile(const std::string& oldPath, const std::string& newName)
{
   try {
      fs::path oldP = oldPath;
      fs::path newP = oldP.parent_path() / newName;

      if (!fs::exists(newP)) {
         fs::rename(oldP, newP);
         std::cout << "[undoApp.Editor] Renamed: " << oldPath << " -> " << newP.string() << std::endl;

         followPathChange(oldPath, newP.string());
         refreshFileTree();
      }
   } catch (const std::exception& e) {
      std::cerr << "[undoApp.Editor] Failed to rename: " << e.what() << std::endl;
   }
}

void EditorApp::moveFile(const std::string& sourcePath, const std::string& destDir)
{
   try {
      fs::path src = sourcePath;
      fs::path dst = fs::path(destDir) / src.filename();

      if (!fs::exists(dst)) {
         fs::rename(src, dst);
         std::cout << "[undoApp.Editor] Moved: " << sourcePath << " -> " << dst.string() << std::endl;

         followPathChange(sourcePath, dst.string());
         refreshFileTree();
      }
   } catch (const std::exception& e) {
      std::cerr << "[undoApp.Editor] Failed to move: " << e.what() << std::endl;
   }
}

void EditorApp::openWorkspaceDialog()
{
#ifdef USE_TINYFILEDIALOGS
   const char* selected = tinyfd_selectFolderDialog("Select Workspace", "");
   if (selected) {
      loadWorkspace(selected);
   }
#else
   std::string cmd = "zenity --file-selection --directory --title='Select Workspace' 2>/dev/null";
   FILE* pipe = popen(cmd.c_str(), "r");
   if (pipe) {
      char buffer[1024];
      std::string result;
      while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
         result += buffer;
      }
      pclose(pipe);

      if (!result.empty() && result.back() == '\n') {
         result.pop_back();
      }

      if (!result.empty()) {
         loadWorkspace(result);
         return;
      }
   }

   // zenity is not installed, so there is no dialog to show. Ask for the path
   // instead, in a modal of its own.
   //
   // This used to call OpenPopup("Select Workspace") and nothing else, with no
   // Begin anywhere in the app to match it: the popup was never drawn, and the
   // entry it left on ImGui's open stack stayed there for the rest of the session,
   // swallowing every Escape pressed afterwards.
   m_showWorkspaceDialog = true;
   strncpy(m_workspaceDialogPath, m_workspacePath.c_str(), sizeof(m_workspaceDialogPath) - 1);
   m_workspaceDialogPath[sizeof(m_workspaceDialogPath) - 1] = '\0';
#endif
}

void EditorApp::createNewGenericFile(const std::string& parentPath, const std::string& name)
{
   fs::path newPath = fs::path(parentPath) / name;
   if (fs::exists(newPath)) {
      std::cerr << "[undoApp.Editor] File already exists: " << newPath.string() << std::endl;
      return;
   }

   // Create an empty file
   std::ofstream file(newPath.string());
   if (!file.is_open()) {
      std::cerr << "[undoApp.Editor] Failed to create file: " << newPath.string() << std::endl;
      return;
   }
   file.close();

   std::cout << "[undoApp.Editor] Created generic file: " << newPath.string() << std::endl;

   // Open the current file with the appropriate backend (based on the extension)
   openFile(newPath.string(), /*pin=*/true);
   refreshFileTree();
}

// ============================================================================
// Workspace Panel Rendering
// ============================================================================

void EditorApp::renderWorkspacePanel()
{
   auto& pm = undoStudio::core::ProjectManager::getInstance();
   auto& imguiManager = undoStudio::ui::ImGuiManager::getInstance();

   // A project picked from the menu bar's recent list, or with Ctrl+R. The menu
   // is the core and this workspace is the undoApp that has to rebuild its
   // tree, so the pick arrives as a request rather than as a call.
   {
      std::string requested;
      if (const std::string* path = imguiManager.consumeOpenProjectRequest(requested)) {
         openProject(*path);
      }
   }

   // A file named on the command line or dropped onto the window. It is
   // drained one at a time rather than taken in a batch, so a drop of several
   // opens them in the order they were given and each one's errors are reported
   // on its own: a drop of a dozen files where the ninth is not readable says
   // so, instead of the whole drop failing together.
   while (true) {
      std::string requestedFile;
      const std::string* path = imguiManager.consumeOpenFileRequest(requestedFile);
      if (path == nullptr) {
         break;
      }
      // Asked for by name, so it keeps the tab. A file picked out of the recent list, or
      // dropped on the window, is not browsing past it.
      openFile(*path, /*pin=*/true);
   }

   // -----------------------------------------------------------------------
   // Top toolbar
   // -----------------------------------------------------------------------
   if (pm.isProjectOpen()) {
      ImGui::TextColored(ImVec4(0.0f, 0.8f, 0.4f, 1.0f), "[Project]");
      ImGui::SameLine();
      ImGui::Text("%s", pm.getProjectName().c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Save")) {
         pm.saveProject();
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Close")) {
         closeProject();
      }
   } else {
      if (ImGui::Button("New Project")) {
         m_showNewProjectPopup = true;
         m_newProjectParent[0] = '\0';
         m_newProjectName[0] = '\0';
         m_newProjectAuthor[0] = '\0';
      }
      ImGui::SameLine();
      if (ImGui::Button("Open Project")) {
#ifdef USE_TINYFILEDIALOGS
         const char* sel = tinyfd_selectFolderDialog("Open undoProject", "");
         if (sel) {
            openProject(sel);
         }
#else
         std::string cmd = "zenity --file-selection --directory --title='Open undoProject' 2>/dev/null";
         FILE* pipe = popen(cmd.c_str(), "r");
         if (pipe) {
            char buf[1024];
            std::string result;
            while (fgets(buf, sizeof(buf), pipe)) {
               result += buf;
            }
            pclose(pipe);
            if (!result.empty() && result.back() == '\n') {
               result.pop_back();
            }
            if (!result.empty()) {
               openProject(result);
            }
         }
#endif
      }
      ImGui::SameLine();
      if (ImGui::Button("Select Workspace")) {
         openWorkspaceDialog();
      }
      ImGui::SameLine();
      if (ImGui::Button("Refresh")) {
         refreshFileTree();
      }
   }

   // -----------------------------------------------------------------------
   // Secondary toolbar (project-specific or workspace-specific)
   // -----------------------------------------------------------------------
   ImGui::Separator();
   if (pm.isProjectOpen()) {
      if (ImGui::Button("+ PLC")) {
         m_showAddPLCPopup = true;
         m_addPLCName[0] = '\0';
         m_addPLCDesc[0] = '\0';
      }
      ImGui::SameLine();
      if (ImGui::Button("+ Task")) {
         m_showAddTaskPopup = true;
         m_addTaskName[0] = '\0';
         m_addTaskPLC[0] = '\0';
         m_addTaskCycleMs = 1;
         m_addTaskPriority = 80;
         m_addTaskCPU = -1;
      }
   } else if (!m_workspacePath.empty()) {
      if (ImGui::Button("+ POU")) {
         m_showNewPOUPopup = true;
         m_newItemParent = m_workspacePath;
         m_newPOUName.clear();
         m_newPOUType = ST::POUType::Program;
      }
      ImGui::SameLine();
      if (ImGui::Button("+ Folder")) {
         m_showNewFolderPopup = true;
         m_newItemParent = m_workspacePath;
         m_newItemName.clear();
      }
      ImGui::SameLine();
      if (ImGui::Button("+ File")) {
         m_showNewFilePopup = true;
         m_newFileParent = m_workspacePath;
         m_newFileName.clear();
      }
   }

   // -----------------------------------------------------------------------
   // File tree
   // -----------------------------------------------------------------------
   if (!m_workspacePath.empty()) {
      renderFileTree(m_rootNode);
   } else if (!pm.isProjectOpen()) {
      ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No workspace or project open.");
   }

   // =========================================================================
   // POPUP MODALS — fuori da Begin/End, attivi in ENTRAMBE le modalità.
   //
   // REGOLA IMGUI: OpenPopup deve essere chiamato una sola volta per frame,
   // nello stesso livello di stack della finestra che li ospita — MAI dentro
   // una funzione ricorsiva come renderFileTree.
   // =========================================================================

   // --- New Project ----------------------------------------------------------
   if (m_showNewProjectPopup) {
      ImGui::OpenPopup("New undoProject");
   }
   if (ImGui::BeginPopupModal("New undoProject", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Parent directory:");
      ImGui::InputText("##parent", m_newProjectParent, sizeof(m_newProjectParent));
      ImGui::SameLine();
      if (ImGui::Button("Browse##proj")) {
#ifdef USE_TINYFILEDIALOGS
         const char* sel = tinyfd_selectFolderDialog("Select parent directory", "");
         if (sel) {
            strncpy(m_newProjectParent, sel, sizeof(m_newProjectParent) - 1);
         }
#else
         std::string cmd = "zenity --file-selection --directory 2>/dev/null";
         FILE* pipe = popen(cmd.c_str(), "r");
         if (pipe) {
            char buf[1024];
            std::string res;
            while (fgets(buf, sizeof(buf), pipe)) {
               res += buf;
            }
            pclose(pipe);
            if (!res.empty() && res.back() == '\n') {
               res.pop_back();
            }
            if (!res.empty()) {
               strncpy(m_newProjectParent, res.c_str(), sizeof(m_newProjectParent) - 1);
            }
         }
#endif
      }
      ImGui::InputText("Project Name", m_newProjectName, sizeof(m_newProjectName));
      ImGui::InputText("Author", m_newProjectAuthor, sizeof(m_newProjectAuthor));
      ImGui::InputText("Architecture", m_newProjectArch, sizeof(m_newProjectArch));
      ImGui::Separator();
      bool canCreate = m_newProjectParent[0] != '\0' && m_newProjectName[0] != '\0';
      ImGui::BeginDisabled(!canCreate);
      if (ImGui::Button("Create")) {
         createProject(m_newProjectParent, m_newProjectName);
         if (pm.isProjectOpen()) {
            auto& cfg = const_cast<undoStudio::core::ProjectConfig&>(pm.getConfig());
            cfg.author = m_newProjectAuthor;
            cfg.arch = m_newProjectArch;
            pm.saveProject();
         }
         m_showNewProjectPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showNewProjectPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- Add PLC --------------------------------------------------------------
   if (m_showAddPLCPopup) {
      ImGui::OpenPopup("Add PLC");
   }
   if (ImGui::BeginPopupModal("Add PLC", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      auto& pm2 = undoStudio::core::ProjectManager::getInstance();
      ImGui::Text("PLC name (e.g. undoPLC_Axis):");
      ImGui::InputText("##plcname", m_addPLCName, sizeof(m_addPLCName));
      ImGui::InputText("Description", m_addPLCDesc, sizeof(m_addPLCDesc));
      bool nameTaken = pm2.hasPLC(m_addPLCName);
      if (nameTaken) {
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "A PLC with this name already exists.");
      }
      ImGui::BeginDisabled(m_addPLCName[0] == '\0' || nameTaken);
      if (ImGui::Button("Add")) {
         pm2.addPLC(m_addPLCName, m_addPLCDesc);
         refreshFileTree();
         m_showAddPLCPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showAddPLCPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- Add Task -------------------------------------------------------------
   if (m_showAddTaskPopup) {
      ImGui::OpenPopup("Add Task");
   }
   if (ImGui::BeginPopupModal("Add Task", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      auto& pm2 = undoStudio::core::ProjectManager::getInstance();
      ImGui::InputText("Task Name", m_addTaskName, sizeof(m_addTaskName));
      ImGui::InputText("PLC Name", m_addTaskPLC, sizeof(m_addTaskPLC));
      ImGui::InputInt("Cycle (ms)", &m_addTaskCycleMs);
      ImGui::InputInt("Priority (1-99)", &m_addTaskPriority);
      ImGui::InputInt("CPU affinity (-1=any)", &m_addTaskCPU);
      if (m_addTaskCycleMs < 1) {
         m_addTaskCycleMs = 1;
      }
      m_addTaskPriority = std::max(1, std::min(99, m_addTaskPriority));
      if (m_addTaskPLC[0] != '\0' && !pm2.hasPLC(m_addTaskPLC)) {
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "PLC '%s' not found in project.", m_addTaskPLC);
      }
      ImGui::TextDisabled("Programs can be added from exports.json after creation.");
      ImGui::BeginDisabled(m_addTaskName[0] == '\0' || m_addTaskPLC[0] == '\0');
      if (ImGui::Button("Add")) {
         undoStudio::core::TaskConfig t;
         t.name = m_addTaskName;
         t.plc = m_addTaskPLC;
         t.cycle_ms = m_addTaskCycleMs;
         t.priority = m_addTaskPriority;
         t.cpu_affinity = m_addTaskCPU;
         pm2.addTask(t);
         refreshFileTree();
         m_showAddTaskPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showAddTaskPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- Rename PLC -----------------------------------------------------------
   if (m_showRenamePLCPopup) {
      ImGui::OpenPopup("Rename PLC");
   }
   if (ImGui::BeginPopupModal("Rename PLC", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      auto& pm2 = undoStudio::core::ProjectManager::getInstance();
      ImGui::Text("Rename '%s' to:", m_renamePLCOldName.c_str());
      ImGui::InputText("##newplcname", m_renamePLCNewName, sizeof(m_renamePLCNewName));
      ImGui::BeginDisabled(m_renamePLCNewName[0] == '\0');
      if (ImGui::Button("Rename")) {
         pm2.renamePLC(m_renamePLCOldName, m_renamePLCNewName);
         refreshFileTree();
         m_showRenamePLCPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showRenamePLCPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- Delete PLC -----------------------------------------------------------
   if (m_showDeletePLCConfirm) {
      ImGui::OpenPopup("Delete PLC?");
   }
   if (ImGui::BeginPopupModal("Delete PLC?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      auto& pm2 = undoStudio::core::ProjectManager::getInstance();
      ImGui::Text("Delete PLC '%s' and ALL its files?", m_deletePLCPending.c_str());
      ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "This cannot be undone!");
      if (ImGui::Button("Yes, Delete")) {
         std::error_code ec;
         fs::remove_all(pm2.plcPath(m_deletePLCPending), ec);
         pm2.removePLC(m_deletePLCPending);
         refreshFileTree();
         m_showDeletePLCConfirm = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showDeletePLCConfirm = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- New POU (workspace mode and project mode) ------------------------------
   if (m_showNewPOUPopup) {
      ImGui::OpenPopup("New POU");
   }
   if (ImGui::BeginPopupModal("New POU", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Create new POU in:");
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", m_newItemParent.c_str());
      char nameBuf[256] = "";
      strncpy(nameBuf, m_newPOUName.c_str(), sizeof(nameBuf) - 1);
      if (ImGui::InputText("POU Name", nameBuf, sizeof(nameBuf))) {
         m_newPOUName = nameBuf;
      }
      const char* typeItems[] = {"Program", "Function Block", "Function"};
      int currentType = static_cast<int>(m_newPOUType);
      if (ImGui::Combo("Type", &currentType, typeItems, 3)) {
         m_newPOUType = static_cast<ST::POUType>(currentType);
         if (m_newPOUType != ST::POUType::Function) {
            m_newPOUReturnType.clear();
         } else if (m_newPOUReturnType.empty()) {
            m_newPOUReturnType = "INT";
         }
      }
      if (m_newPOUType == ST::POUType::Function) {
         char retBuf[64] = "";
         strncpy(retBuf, m_newPOUReturnType.c_str(), sizeof(retBuf) - 1);
         if (ImGui::InputText("Return Type", retBuf, sizeof(retBuf))) {
            m_newPOUReturnType = retBuf;
         }
      }
      ImGui::TextDisabled(".st extension added automatically");
      bool canCreate = !m_newPOUName.empty() && (m_newPOUType != ST::POUType::Function || !m_newPOUReturnType.empty());
      ImGui::BeginDisabled(!canCreate);
      if (ImGui::Button("Create")) {
         std::string fn = m_newPOUName;
         if (fn.size() < 3 || fn.substr(fn.size() - 3) != ".st") {
            fn += ".st";
         }
         createNewFile(m_newItemParent, fn);
         m_showNewPOUPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showNewPOUPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- New File (generic) ---------------------------------------------------
   if (m_showNewFilePopup) {
      ImGui::OpenPopup("New File");
   }
   if (ImGui::BeginPopupModal("New File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Create new file in:");
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", m_newFileParent.c_str());
      char nameBuf[256] = "";
      strncpy(nameBuf, m_newFileName.c_str(), sizeof(nameBuf) - 1);
      if (ImGui::InputText("File Name (with extension)", nameBuf, sizeof(nameBuf))) {
         m_newFileName = nameBuf;
      }
      ImGui::TextDisabled("e.g.  GVL.st  MyStruct.st  config.json");
      ImGui::BeginDisabled(m_newFileName.empty());
      if (ImGui::Button("Create")) {
         createNewGenericFile(m_newFileParent, m_newFileName);
         m_showNewFilePopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showNewFilePopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- New Folder -----------------------------------------------------------
   if (m_showNewFolderPopup) {
      ImGui::OpenPopup("New Folder");
   }
   if (ImGui::BeginPopupModal("New Folder", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Create new folder in:");
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", m_newItemParent.c_str());
      char nameBuf[256] = "";
      strncpy(nameBuf, m_newItemName.c_str(), sizeof(nameBuf) - 1);
      if (ImGui::InputText("Folder Name", nameBuf, sizeof(nameBuf))) {
         m_newItemName = nameBuf;
      }
      ImGui::BeginDisabled(m_newItemName.empty());
      if (ImGui::Button("Create")) {
         createNewFolder(m_newItemParent, m_newItemName);
         m_showNewFolderPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showNewFolderPopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- Rename (file/folder generico) ----------------------------------------
   if (m_showRenamePopup) {
      ImGui::OpenPopup("Rename");
   }
   if (ImGui::BeginPopupModal("Rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Rename: %s", fs::path(m_renamePath).filename().string().c_str());
      char nameBuf[256] = "";
      strncpy(nameBuf, m_newItemName.c_str(), sizeof(nameBuf) - 1);
      if (ImGui::InputText("New Name", nameBuf, sizeof(nameBuf))) {
         m_newItemName = nameBuf;
      }
      ImGui::BeginDisabled(m_newItemName.empty());
      if (ImGui::Button("Rename")) {
         renameFile(m_renamePath, m_newItemName);
         m_showRenamePopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showRenamePopup = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- Delete confirmation (file/folder generico) ---------------------------
   if (m_showDeleteConfirmation) {
      ImGui::OpenPopup("Delete?");
   }
   if (ImGui::BeginPopupModal("Delete?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Delete: %s", fs::path(m_deletePendingPath).filename().string().c_str());
      ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "This cannot be undone!");
      if (ImGui::Button("Yes, Delete")) {
         try {
            if (fs::is_directory(m_deletePendingPath)) {
               fs::remove_all(m_deletePendingPath);
            } else {
               fs::remove(m_deletePendingPath);
            }
            closeDocumentsUnder(m_deletePendingPath);
            refreshFileTree();
         } catch (const std::exception& e) {
            std::cerr << "[undoApp.Editor] Delete failed: " << e.what() << std::endl;
         }
         m_showDeleteConfirmation = false;
         m_deletePendingPath.clear();
         ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showDeleteConfirmation = false;
         m_deletePendingPath.clear();
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   // --- Workspace picker (only when there is no native dialog to fall back on) --
   if (m_showWorkspaceDialog) {
      ImGui::OpenPopup("Select Workspace");
   }
   if (ImGui::BeginPopupModal("Select Workspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Neither tinyfiledialogs nor zenity is available.");
      ImGui::Text("Enter the path of the workspace folder:");
      ImGui::InputText("##workspacePath", m_workspaceDialogPath, sizeof(m_workspaceDialogPath));
      if (m_workspaceDialogPath[0] == '\0') {
         ImGui::TextDisabled("No folder chosen.");
      } else if (!fs::is_directory(m_workspaceDialogPath)) {
         ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Not a folder.");
      }
      ImGui::BeginDisabled(m_workspaceDialogPath[0] == '\0' || !fs::is_directory(m_workspaceDialogPath));
      if (ImGui::Button("Open")) {
         loadWorkspace(m_workspaceDialogPath);
         m_showWorkspaceDialog = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showWorkspaceDialog = false;
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }
}

// ============================================================================
// File Tree Renderer
// ============================================================================

void EditorApp::renderFileTree(FileNode& node)
{
   auto& pm = undoStudio::core::ProjectManager::getInstance();
   const bool inProject = pm.isProjectOpen();

   // Colore label in base al ruolo nel progetto
   auto pushRoleColor = [&]() {
      switch (node.role) {
      case NodeRole::UndoCore:
      case NodeRole::TasksFolder:
      case NodeRole::TaskFile:
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
         break;
      case NodeRole::UndoLogic:
      case NodeRole::SharedLibs:
      case NodeRole::SharedGVLs:
      case NodeRole::SharedDUTs:
      case NodeRole::SharedPOUs:
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.85f, 1.0f, 1.0f));
         break;
      case NodeRole::PLCRoot:
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.6f, 1.0f));
         break;
      case NodeRole::PLCLibs:
      case NodeRole::PLCGVLs:
      case NodeRole::PLCDUTs:
      case NodeRole::PLCPOUs:
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.75f, 1.0f, 0.75f, 1.0f));
         break;
      case NodeRole::ExportsFile:
      case NodeRole::ConfigFile:
      case NodeRole::JSONFile:
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.7f, 0.5f, 1.0f));
         break;
      case NodeRole::STFile:
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.7f, 1.0f, 1.0f));
         break;
      case NodeRole::ConfigFolder:
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
         break;
      default:
         ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
         break;
      }
   };

   if (node.isDirectory) {
      ImGuiTreeNodeFlags flags = node.expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

      pushRoleColor();
      bool isOpen = ImGui::TreeNodeEx(("##dir_" + node.path).c_str(), flags, "%s", node.name.c_str());
      ImGui::PopStyleColor();

      // Drop target per drag-and-drop
      if (m_isDragging && ImGui::IsItemHovered() && ImGui::IsMouseReleased(0) && m_draggedItemPath != node.path) {
         moveFile(m_draggedItemPath, node.path);
         m_isDragging = false;
         m_draggedItemPath.clear();
      }

      if (isOpen) {
         node.expanded = true;
         for (auto& child : node.children) {
            renderFileTree(child);
         }
         ImGui::TreePop();
      } else {
         node.expanded = false;
      }

      // Context menu — imposta SOLO flag, non apre popup
      if (ImGui::IsItemClicked(1)) {
         ImGui::OpenPopup(("##ctx_" + node.path).c_str());
      }
      if (ImGui::BeginPopup(("##ctx_" + node.path).c_str())) {
         if (node.role == NodeRole::SharedPOUs || node.role == NodeRole::PLCPOUs) {
            if (ImGui::MenuItem("New PROGRAM")) {
               m_showNewPOUPopup = true;
               m_newItemParent = node.path;
               m_newPOUType = ST::POUType::Program;
               m_newPOUName.clear();
            }
            if (ImGui::MenuItem("New FUNCTION_BLOCK")) {
               m_showNewPOUPopup = true;
               m_newItemParent = node.path;
               m_newPOUType = ST::POUType::FunctionBlock;
               m_newPOUName.clear();
            }
            if (ImGui::MenuItem("New FUNCTION")) {
               m_showNewPOUPopup = true;
               m_newItemParent = node.path;
               m_newPOUType = ST::POUType::Function;
               m_newPOUName.clear();
               m_newPOUReturnType = "INT";
            }
         } else if (node.role == NodeRole::SharedGVLs || node.role == NodeRole::PLCGVLs) {
            if (ImGui::MenuItem("New GVL")) {
               m_showNewFilePopup = true;
               m_newFileParent = node.path;
               m_newFileName = "GVL.st";
            }
         } else if (node.role == NodeRole::SharedDUTs || node.role == NodeRole::PLCDUTs) {
            if (ImGui::MenuItem("New STRUCT")) {
               m_showNewFilePopup = true;
               m_newFileParent = node.path;
               m_newFileName = "MyStruct.st";
            }
            if (ImGui::MenuItem("New ENUM")) {
               m_showNewFilePopup = true;
               m_newFileParent = node.path;
               m_newFileName = "MyEnum.st";
            }
         } else if (node.role == NodeRole::SharedLibs || node.role == NodeRole::PLCLibs) {
            if (ImGui::MenuItem("Add Library File")) {
               m_showNewFilePopup = true;
               m_newFileParent = node.path;
               m_newFileName.clear();
            }
         } else if (node.role == NodeRole::UndoLogic) {
            if (ImGui::MenuItem("Add PLC...")) {
               m_showAddPLCPopup = true;
               m_addPLCName[0] = '\0';
               m_addPLCDesc[0] = '\0';
            }
         } else if (node.role == NodeRole::PLCRoot) {
            if (ImGui::MenuItem("Add Task...")) {
               m_showAddTaskPopup = true;
               strncpy(m_addTaskPLC, node.name.c_str(), sizeof(m_addTaskPLC) - 1);
               m_addTaskName[0] = '\0';
               m_addTaskCycleMs = 1;
               m_addTaskPriority = 80;
               m_addTaskCPU = -1;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Rename PLC...")) {
               m_showRenamePLCPopup = true;
               m_renamePLCOldName = node.name;
               m_renamePLCNewName[0] = '\0';
            }
            if (ImGui::MenuItem("Delete PLC")) {
               m_showDeletePLCConfirm = true;
               m_deletePLCPending = node.name;
            }
         } else if (node.role == NodeRole::TasksFolder) {
            if (ImGui::MenuItem("Add Task...")) {
               m_showAddTaskPopup = true;
               m_addTaskName[0] = '\0';
               m_addTaskPLC[0] = '\0';
               m_addTaskCycleMs = 1;
               m_addTaskPriority = 80;
               m_addTaskCPU = -1;
            }
         } else {
            // Generic directory (flat workspace or unknown role)
            if (ImGui::MenuItem("New POU")) {
               m_showNewPOUPopup = true;
               m_newItemParent = node.path;
               m_newPOUType = ST::POUType::Program;
               m_newPOUName.clear();
            }
            if (ImGui::MenuItem("New File")) {
               m_showNewFilePopup = true;
               m_newFileParent = node.path;
               m_newFileName.clear();
            }
            if (ImGui::MenuItem("New Folder")) {
               m_showNewFolderPopup = true;
               m_newItemParent = node.path;
               m_newItemName.clear();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Rename")) {
               m_showRenamePopup = true;
               m_renamePath = node.path;
               m_newItemName = node.name;
            }
            if (ImGui::MenuItem("Delete Folder")) {
               m_showDeleteConfirmation = true;
               m_deletePendingPath = node.path;
            }
         }
         ImGui::EndPopup();
      }

   } else {
      // -----------------------------------------------------------------------
      // File node — nessun popup aperto qui, solo flag
      // -----------------------------------------------------------------------
      bool isST = node.role == NodeRole::STFile
                  || (!node.name.empty() && node.name.size() > 3 && node.name.substr(node.name.size() - 3) == ".st");
      bool isJSON = node.name.size() > 5 && node.name.substr(node.name.size() - 5) == ".json";
      bool isSelected = (node.path == m_currentFilePath);

      pushRoleColor();
      ImGui::Selectable(node.name.c_str(), isSelected);
      ImGui::PopStyleColor();

      if (ImGui::IsItemClicked(0)) {
         // A click keeps its tab. It did not used to: the click opened a preview and
         // the next one took it away, which reads as the editor losing files — the
         // file being read disappeared from the bar the moment another was opened,
         // and the way to keep it was to know to double click.
         openFile(node.path, /*pin=*/true);
      }
      if (ImGui::IsItemClicked(0) && ImGui::IsMouseDragging(0)) {
         m_isDragging = true;
         m_draggedItemPath = node.path;
      }
      if (m_isDragging && ImGui::IsItemHovered() && m_draggedItemPath != node.path && ImGui::IsMouseReleased(0)) {
         moveFile(m_draggedItemPath, fs::path(node.path).parent_path().string());
         m_isDragging = false;
         m_draggedItemPath.clear();
      }
      // No double-click branch: a click already keeps the tab, so a second click on
      // the same row has nothing left to decide. It used to be the only way to keep a
      // file, and ImGui only reports the double click when both clicks land within a
      // few pixels, so "double click to keep it" was a rule nobody could see.
      //
      if (ImGui::IsItemClicked(1)) {
         ImGui::OpenPopup(("##ctx_" + node.path).c_str());
      }
      if (ImGui::BeginPopup(("##ctx_" + node.path).c_str())) {
         if (ImGui::MenuItem("Open")) {
            openFile(node.path, /*pin=*/true);
         }
         if (isJSON && ImGui::MenuItem("Open as Text")) {
            openFileAsText(node.path);
         }
         if (isJSON && ImGui::MenuItem("Open as Tree")) {
            openFileAsTree(node.path);
         }
         if (isST && ImGui::MenuItem("Add Method")) {
            // Ask the ST document to open its dialog rather than adding a
            // method straight away with a fixed name, so this behaves exactly
            // like the "+" on the tab bar. It has to travel through the ST app
            // because the popup belongs to the Editor panel and not to this
            // Workspace one; OpenPopup does not cross windows.
            ST::STApp& st = ST::STApp::getInstance();
            st.openFile(node.path);
            st.requestAddMethodDialog();
         }
         if (node.role == NodeRole::TaskFile) {
            std::string taskName = fs::path(node.path).stem().string();
            if (auto* t = undoStudio::core::ProjectManager::getInstance().findTask(taskName)) {
               ImGui::Separator();
               ImGui::TextDisabled("cycle=%dms  priority=%d  cpu=%d", t->cycle_ms, t->priority, t->cpu_affinity);
               if (ImGui::MenuItem("Delete Task")) {
                  undoStudio::core::ProjectManager::getInstance().removeTask(taskName);
                  refreshFileTree();
               }
            }
         }
         // Rename/Delete solo per file non strutturali del progetto
         if (!inProject || node.role == NodeRole::Generic || node.role == NodeRole::STFile) {
            ImGui::Separator();
            if (ImGui::MenuItem("Rename")) {
               m_showRenamePopup = true;
               m_renamePath = node.path;
               m_newItemName = node.name;
            }
            if (ImGui::MenuItem("Delete")) {
               m_showDeleteConfirmation = true;
               m_deletePendingPath = node.path;
            }
         }
         ImGui::EndPopup();
      }
   }
}

// ============================================================================
// Editor Panel Rendering
// ============================================================================

/**
 * @brief The bar of open files, above whichever editor is showing them
 *
 * Drawn here rather than inside a backend, because the backends are not the ones
 * that know what is open: there is one ST editor, one JSON viewer and so on, and
 * the set of files is wider than all of them together.
 *
 * A preview is drawn in italics, the way VS Code draws one, because the difference
 * between a tab you are passing through and one you are working in is the whole
 * reason the second click overwrites the first. An unsaved file carries a dot, and
 * the dot is the only thing here that says anything about changes being at risk.
 *
 * The bar scrolls sideways rather than squeezing: thirty tabs in the width of a
 * window is unreadable at any size that shows them all, and a name truncated to
 * four letters is worse than one that runs off the edge and scrolls.
 *
 * Both its dimensions are given rather than left to ImGui, and the height is the
 * reason the panel used to show nothing but this bar. A zero in a child window's
 * size is not "none of it", it is *all* of it: this one was `ImVec2(0, 0)`, grew to
 * the height of the whole panel, and the editor it was meant to sit above was laid
 * out underneath and never seen. The width stays at zero on purpose, because that
 * is what makes the bar span the panel; the height is a row, plus the scrollbar's
 * height on the one frame where the tabs turn out to be too many for it.
 */
void EditorApp::renderFileTabs()
{
   // Before the bar is measured or drawn, because the marks are part of what it
   // measures: a tab that has just been saved is a tab that is narrower.
   refreshDirtyState();

   const std::vector<OpenDocument>& docs = m_open.documents();
   if (docs.empty()) {
      return;
   }

   // How wide the bar wants to be, summed before anything is drawn: ImGui only
   // learns the content size once the buttons exist, and a child window cannot be
   // told to shrink after the fact. The widths are the ones ImGui itself uses --
   // CalcTextSize plus the frame padding, and one ItemSpacing for every SameLine
   // below -- so the sum matches what the child will measure, off by nothing.
   const ImGuiStyle& style = ImGui::GetStyle();
   float needed = 0.0f;
   for (const OpenDocument& doc : docs) {
      const std::string name = fs::path(doc.path).filename().string();
      const std::string label = doc.pinned ? name : ("~ " + name);

      needed += ImGui::CalcTextSize(label.c_str()).x + style.FramePadding.x * 2.0f;
      needed += style.ItemSpacing.x;   // the SameLine after the tab
      if (doc.dirty) {
         needed += ImGui::CalcTextSize("*").x + style.ItemSpacing.x;
      }
      needed += ImGui::CalcTextSize("x").x + style.FramePadding.x * 2.0f;
      needed += style.ItemSpacing.x;   // the SameLine after the close button
   }
   // The loop below ends every tab with a SameLine, including the last one, and
   // ImGui counts no spacing after a line nothing follows. Discount it once, or
   // the bar reserves room for a scrollbar it does not need.
   needed -= style.ItemSpacing.x;

   // The scrollbar is inside the bar, so it is inside its height: a bar that grew
   // by the scrollbar's height would push the editor down by that much every time
   // a tab was opened, and a bar given only a row would clip its own buttons. The
   // row is always reserved; the scrollbar only when the tabs are wider than the
   // panel.
   const bool scrolls = needed > ImGui::GetContentRegionAvail().x;
   const float barHeight = ImGui::GetFrameHeightWithSpacing() + (scrolls ? style.ScrollbarSize : 0.0f);
   const ImGuiWindowFlags barFlags = scrolls
                                         ? ImGuiWindowFlags_HorizontalScrollbar
                                         : (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

   ImGui::BeginChild("##fileTabs", ImVec2(0.0f, barHeight), false, barFlags);

   for (const OpenDocument& doc : docs) {
      const std::string name = fs::path(doc.path).filename().string();

      // A preview is the tab the next click replaces, and a reader should be able
      // to tell without opening anything: the name is prefixed with a tilde. The
      // label is held in a named string rather than built inside the call, so the
      // button is never handed the address of a temporary.
      const std::string label = doc.pinned ? name : ("~ " + name);
      const bool active = (doc.path == m_open.active());

      // The path is the id, because two open files can share a name in different
      // folders and they are two tabs.
      ImGui::PushID(doc.path.c_str());

      ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.18f, 0.22f, 0.30f, 1.0f)
                                                     : ImVec4(0.13f, 0.15f, 0.20f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.30f, 0.38f, 1.0f));
      if (ImGui::Button(label.c_str())) {
         switchToTab(doc.path);
      }
      ImGui::PopStyleColor(2);

      // The unsaved mark. Beside the name rather than as a colour on the tab,
      // because the tab colour is already saying which one is active.
      if (doc.dirty) {
         ImGui::SameLine();
         ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "*");
      }

      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.40f, 0.18f, 0.18f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.28f, 0.28f, 1.0f));
      if (ImGui::SmallButton("x")) {
         closeTab(doc.path);
      }
      ImGui::PopStyleColor(2);

      ImGui::PopID();
      ImGui::SameLine();
   }

   ImGui::EndChild();
}

void EditorApp::renderEditorPanel()
{
   ImGuiIO& io = ImGui::GetIO();
   if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && (io.KeyCtrl || io.KeySuper)
       && // Ctrl on Windows/Linux, Cmd on macOS
       ImGui::IsKeyPressed(ImGuiKey_S)) {
      saveActiveDocument();
   }

   // The bar of open files, above whatever the file on screen is drawn by. It
   // used to be drawn inside each backend instead, which meant the ST panel drew
   // a second one: same set of files, two bars, and the one in the panel nobody
   // was looking at. One panel, one bar, and the bar belongs to the panel.
   renderFileTabs();

   // Dispatch to whichever backend the file on screen needs. None of them opens a
   // window: ImGuiManager::render() has already begun this one, and a Begin here
   // would be a second window with the panel's name laid out inside it, which is
   // how a file came to appear in a window of its own titled with its name.
   switch (m_currentFileType) {
   case FileType::ST: {
      auto& st = ST::STApp::getInstance();
      // Three calls rather than one because they are three different windows'
      // worth of work. The keyboard and the unsaved-changes prompt belong to the
      // panel, so they happen before the document; the signature help and the
      // completion lists are separate windows drawn after it so that the editor
      // cannot clip them.
      st.beginEditorFrame();
      st.renderDocument();
      st.endEditorFrame();
      break;
   }
   case FileType::JSON:
      JSON::JSONApp::getInstance().renderJSONPanel();
      break;
   case FileType::Cpp:
      CppApp::getInstance().renderEditorPanel();
      break;
   case FileType::Text:
      TextApp::getInstance().renderEditorPanel();
      break;
   case FileType::None:
   default:
      ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No file open");
      ImGui::TextWrapped("Open a file from the Workspace panel or use the File menu.");
      break;
   }
}

// ============================================================================
// Plugin Entry Points
// ============================================================================

extern "C" {
void* createUndoApp()
{
   auto& app = EditorApp::getInstance();
   app.initialize();
   return &app;
}

void destroyUndoApp(void* app)
{
   auto* editorApp = static_cast<EditorApp*>(app);
   editorApp->shutdown();
}
}

} // namespace Editor
} // namespace undoApp