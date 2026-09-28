/**
 * @file undoAppEditor.hpp
 * @brief Main header for the unified undoApp.Editor plugin
 * @ingroup undoapps
 *
 * This plugin is a single editor that auto-detects how to display a file
 * based on its extension:
 * - `.st` (case-insensitive)        -> Structured Text editor
 * - `.json`                         -> JSON viewer
 * - everything else (txt, md, ...)  -> plain-text editor (read / edit / save)
 *
 * The plugin owns three sub-apps internally (ST, JSON, Text) and dispatches
 * the single public openFile(path) call to the right one.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <string>
#include <vector>
#include <filesystem>

// ProjectManager: project-level service
#include "undoStudio/core/ProjectManager.hpp"

// Include all sub-app headers
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
 * @brief Main application class for the unified undoApp.Editor plugin
 */
class EditorApp
{
public:
   /// @brief Get the singleton instance of EditorApp
   static EditorApp& getInstance();

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
    */
   void openFile(const std::string& path);

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
   void renameFile(const std::string& oldPath, const std::string& newName);

   /// @brief Move a file to a destination directory
   void moveFile(const std::string& sourcePath, const std::string& destDir);

   /// @brief Open a native folder selection dialog
   void openWorkspaceDialog();

   /// @brief Create generic file
   void createNewGenericFile(const std::string& parentPath, const std::string& name);

   /// @brief Refresh the file tree
   void refreshFileTree();

   /// @brief Open a JSON file as plain text (bypass JSON viewer)
   void openFileAsText(const std::string& path);

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
   // Fields for project.toml
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