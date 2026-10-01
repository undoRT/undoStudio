/**
 * @file undoAppEditor.hpp
 * @brief Main header for the unified undoApp.Editor plugin
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * One editor panel, and the four backends it dispatches to, chosen from the
 * extension and from the file's role:
 * - `.st`                                        -> Structured Text
 * - `.json`                                      -> the JSON tree, or Text if it
 *                                                 is a file this project owns
 * - `.c` `.cpp` `.cc` `.cxx` `.h` `.hpp` ...     -> C++
 * - anything else (txt, md, ...)                -> plain text
 *
 * openFile(path) is the one route in: the tab bar is what answers "what is open",
 * so a backend that opened a file on its own would put a document on screen that
 * the bar had no row for. openFileAsText and openFileAsTree are the two ways to
 * be explicit about a `.json`, and both replace its row rather than adding one.
 */

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <map>

// ProjectManager: project-level service
#include "undoStudio/core/ProjectManager.hpp"

// Include all sub-app headers
#include "undoAppOpenDocuments.hpp"
#include "undoAppST.hpp"
#include "undoAppJSON.hpp"
#include "undoAppText.hpp"
#include "undoAppCpp.hpp"

namespace undoApp {
namespace Editor {

using NodeRole = undoStudio::core::NodeRole;

/**
 * @brief The kind of file currently open in the editor
 */
enum class FileType {
   None,
   ST,
   JSON,
   Text,
   Cpp
};

/**
 * @brief Node structure for the file tree representation
 */
struct FileNode
{
   std::string name;
   std::string path;
   bool isDirectory;
   std::vector<FileNode> children;
   bool expanded = false;
   bool isDragging = false;
   NodeRole role = NodeRole::Generic; ///< Semantic role within an undoProject
};

/**
 * @brief One plain-text or C++ document, held while another file is in the editor
 */
struct StashedText
{
   std::string path;
   std::string text;
};

/**
 * @brief Main application class for the Workspace and file routing
 */
class EditorApp
{
public:
   /// @brief Get the singleton instance of EditorApp
   static EditorApp& getInstance();

   /// @brief Draw the bar of open files above whichever editor is showing
   void renderFileTabs();

   /// @brief Initialize the editor plugin (called by PluginManager)
   bool initialize();

   /// @brief Shutdown the editor plugin and release resources
   void shutdown();

   /// @brief Register the unified "Editor" panel and "Workspace" panel with ImGuiManager
   void registerPanels();

   /// @brief Get the type of the currently open file
   FileType getCurrentFileType() const { return m_currentFileType; }

   /// @brief Get the path of the currently open file (empty when none)
   std::string getCurrentFilePath() const { return m_currentFilePath; }

   /**
    * @brief Open a file using the appropriate backend for its extension
    *
    * Every route into the editor ends up here, which is what makes this the one
    * place that knows a file was looked at, and the one place that decides whether
    * the open was browsing or deliberate.
    *
    * Also records the file in the recent files list, so that a file opened here and
    * then closed is still reachable afterwards.
    *
    * @param path File to open
    * @param pin  Whether the file keeps its tab. True by default, because a file
    *             somebody asked to see should still be there when they ask for
    *             another one: with a single click into the tree opening a preview,
    *             the file being read vanished the moment the next click landed, and
    *             what to do about that — double click it, remember to click twice —
    *             is not something to leave to a user who is trying to read code.
    *             False is for a caller that is deliberately browsing and wants the
    *             file to give way to the next one; the tab bar marks such a tab with
    *             a tilde.
    */
   void openFile(const std::string& path, bool pin = true);

   /// @brief Close the currently open file
   void closeFile();

   /// @brief Render the unified "Editor" panel by dispatching to the active backend
   void renderEditorPanel();

   /// @brief Render the Workspace panel (file tree and workspace management)
   void renderWorkspacePanel();

   /// @brief Load a workspace from the given path
   void loadWorkspace(const std::string& path);

   // ============================================================================
   // Project management (undoProject mode)
   // ============================================================================

   /// @brief Create a new undoProject
   void createProject(const std::string& parentDir, const std::string& name);

   /// @brief Open an existing undoProject from projectDir
   void openProject(const std::string& projectDir);

   /// @brief Close the current undoProject
   void closeProject();

   /// @brief True if an undoProject is currently open
   bool isProjectOpen() const;

private:
   // ============================================================================
   // File tree management
   // ============================================================================

   /// @brief Build a hierarchical file tree from a directory path
   void buildFileTree(FileNode& node, const std::string& root);

   /// @brief Recursively render the file tree
   void renderFileTree(FileNode& node);

   /// @brief Create a new file with ST template (POU)
   void createNewFile(const std::string& parentPath, const std::string& name);

   /// @brief Create a new folder in the workspace
   void createNewFolder(const std::string& parentPath, const std::string& name);

   /// @brief Delete a file or folder (shows confirmation popup)
   void deleteFile(const std::string& path);

   /// @brief Rename a file or folder
   ///
   /// A rename of a file that is open moves its tab, its stash and the editor on
   /// screen with it. The tab is keyed by path, and a save writes to the path the
   /// editor was told about, so a rename that left those behind would show a name
   /// that is not there and save a second copy where the file used to be.
   /// @param oldPath Path of the file or folder to rename
   /// @param newName The new name, in the same folder
   void renameFile(const std::string& oldPath, const std::string& newName);

   /// @brief Move a file to a destination directory
   ///
   /// Tracks the open documents under the new path for the same reason a rename
   /// does: the tab, the stash and the editor all hold the path, not the file.
   /// @param sourcePath Path of the file to move
   /// @param destDir Directory to move it into
   void moveFile(const std::string& sourcePath, const std::string& destDir);

   /// @brief Open a native folder selection dialog
   void openWorkspaceDialog();

   /// @brief Create generic file
   void createNewGenericFile(const std::string& parentPath, const std::string& name);

   /// @brief Refresh the file tree
   void refreshFileTree();

   /// @brief Open a JSON file as plain text (bypass JSON viewer)
   void openFileAsText(const std::string& path);

   /// @brief Open a JSON file in the tree viewer, whatever it opens as by default
   ///
   /// The other half of openFileAsText, and it exists for the same reason. A
   /// project's own configuration opens as text because it is the file the user is
   /// told to edit by hand, and a tree has nothing to type into; but exports.json
   /// is nested and reads badly flat, and the tree is how you look at it.
   /// One file, one tab, whichever of the two the file was opened with.
   void openFileAsTree(const std::string& path);

   // ============================================================================
   // Helper functions
   // ============================================================================

   /// @brief Get the file extension in lowercase
   std::string toLower(std::string s);

   void expandToPath(FileNode& node, const std::string& targetPath);

   // ============================================================================
   // Member variables
   // ============================================================================

   bool m_initialized = false;
   FileType m_currentFileType = FileType::None;
   std::string m_currentFilePath;

   /// The files that are open, and which is only a preview. See the header of
   /// undoAppOpenDocuments.hpp for why there is one preview slot rather than a tab
   /// per file opened.
   OpenDocuments m_open;

   /// What each open file's editor holds while another file is in its place.
   ///
   /// Keyed by path, not by position in m_open. A preview being dropped shortens
   /// that list, so anything indexed beside it slides against the wrong file; a
   /// path cannot. A tab that is still open has to come back showing its own text
   /// — the unsaved half of every change exists nowhere else — and this is where
   /// that text waits.
   std::map<std::string, ST::STDocument> m_stStashes;
   std::map<std::string, StashedText> m_textStashes;
   std::map<std::string, StashedText> m_cppStashes;
   std::map<std::string, JSON::JSONDocument> m_jsonStashes;

   /// Says "not found", for a lookup that may genuinely have no answer
   static constexpr size_t kNoIndex = static_cast<size_t>(-1);

   /// @brief Which backend a document kind belongs to
   static DocKind docKindFor(FileType type);
   static FileType fileTypeFor(DocKind kind);

   /// @brief Take the file on screen out of its backend, into its own stash
   /// @param keep Whether it is a tab rather than a preview about to be reused, in
   ///              which case it is pinned. Not a statement about unsaved changes:
   ///              that is what the editors are asked about, in refreshDirtyState().
   void stashActiveDocument(bool keep);

   /**
    * @brief Ask the backend on screen whether the file has unsaved changes
    *
    * The tab bar's mark is drawn from this, once a frame. Answering it here rather
    * than where the text changes is what makes the mark mean one thing: it appears
    * when there is something to lose and goes away when the file has been written.
    */
   void refreshDirtyState();

   /**
    * @brief Write the file on screen back to disk, whichever kind it is
    *
    * The route the save key takes, and the only one that covers every kind: the ST
    * editor draws its own panel and its own save button, so a shortcut handled where
    * the text backends are dispatched from never reached an .st file.
    */
   void saveActiveDocument();

   /// @brief Put a file into the editor its backend owns
   /// @param path The file to show
   void loadActiveDocument(const std::string& path);

   /// @brief Bring a tab to the front, restoring what it held
   void switchToTab(const std::string& path);

   /// @brief Close a tab, moving to the one beside it if it was the active one
   void closeTab(const std::string& path);



   /// @brief Where a file sits in the open list
   size_t documentIndex(const std::string& path) const;

   /// @brief Open a file with a chosen backend, through the tab bar and the model
   /// @param path File to open
   /// @param kind Which backend holds it
   /// @param pin Whether the file keeps its tab
   void openDocument(const std::string& path, DocKind kind, bool pin);

   /// @brief Follow a file that was renamed or moved on disk
   ///
   /// The one place that knows what a path change means: the tab and its stash are
   /// keyed by path, the editor on screen saves to the path it was given, and the
   /// recent files list points at a file that is no longer there. Anything holding a
   /// path and left behind is now pointing at two different files.
   /// @param oldPath Path the file was known by
   /// @param newPath Path it now has
   void followPathChange(const std::string& oldPath, const std::string& newPath);

   /// @brief Close every tab under a path, including the files inside a folder
   /// @param path File or folder that is gone
   void closeDocumentsUnder(const std::string& path);

   /// @brief Forget everything stashed for a path
   /// @param path Path to drop
   void dropStashes(const std::string& path);

   /// @brief The tab to show after closing one
   std::string neighbourAfterClose(const std::string& path) const;

   // Workspace management
   std::string m_workspacePath;
   FileNode m_rootNode;

   // POU creation popup state
   bool m_showNewPOUPopup = false;
   ST::POUType m_newPOUType = ST::POUType::Program;
   std::string m_newPOUName;
   std::string m_newItemParent;
   std::string m_newPOUReturnType;
   bool m_showPOUReturnType = false;

   // Folder creation popup state
   bool m_showNewFolderPopup = false;
   std::string m_newItemName;
   bool m_showNewFilePopup = false;
   std::string m_newFileName;
   std::string m_newFileParent;

   // Rename popup state
   bool m_showRenamePopup = false;
   std::string m_renamePath;

// Delete confirmation state
    bool m_showDeleteConfirmation = false;
    std::string m_deletePendingPath;

    // Fallback workspace picker, for when neither tinyfiledialogs nor zenity is
    // available. A modal of the app's own, so it has to be drawn by the panel.
    bool m_showWorkspaceDialog = false;
    char m_workspaceDialogPath[1024] = {};

   // Drag and drop state
   std::string m_draggedItemPath;
   bool m_isDragging = false;

   // ============================================================================
   // Project mode state
   // ============================================================================

   bool m_isProjectMode = false;

   // "New Project" popup
   bool m_showNewProjectPopup = false;
   char m_newProjectParent[1024] = {};
   char m_newProjectName[256] = {};
   // Fields for project.json
   char m_newProjectAuthor[256] = {};
   char m_newProjectArch[64] = {"x86_64"};

   // "Add PLC" popup
   bool m_showAddPLCPopup = false;
   char m_addPLCName[256] = {};
   char m_addPLCDesc[512] = {};

   // "Add Task" popup
   bool m_showAddTaskPopup = false;
   char m_addTaskName[256] = {};
   char m_addTaskPLC[256] = {};
   int m_addTaskCycleMs = 1;
   int m_addTaskPriority = 80;
   int m_addTaskCPU = -1;

   // "Rename PLC" popup
   bool m_showRenamePLCPopup = false;
   std::string m_renamePLCOldName;
   char m_renamePLCNewName[256] = {};

   // "Delete PLC" confirmation
   bool m_showDeletePLCConfirm = false;
   std::string m_deletePLCPending;

   // "Edit Task" popup (re-uses Add fields; set to existing task on open)
   bool m_showEditTaskPopup = false;
   std::string m_editTaskOriginalName;
};

} // namespace Editor
} // namespace undoApp