/**
 * @file undoAppEditor.cpp
 * @brief Main entry point for the unified undoApp.Editor plugin
 * @ingroup undoapps
 *
 * This file implements the plugin entry point and routes the single
 * public openFile(path) call to the right backend (ST, JSON, Text)
 * based on the file extension.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoAppEditor.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"
#include "undoStudio/core/Application.hpp"
#include "undoStudio/core/ProjectManager.hpp"

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

void EditorApp::openFile(const std::string& path)
{
   if (path.empty()) {
      std::cerr << "[undoApp.Editor] openFile called with empty path" << std::endl;
      return;
   }
   // Close any previously open file in ALL backends
   // This ensures clean state when switching between file types
   ST::STApp::getInstance().closeFile();
   JSON::JSONApp::getInstance().closeFile();
   TextApp::getInstance().closeFile();
   CppApp::getInstance().closeFile();

   // Decide which backend to dispatch to based on the file extension
   std::string ext = toLower(fs::path(path).extension().string());

   if (ext == ".st") {
      TextApp::getInstance().closeFile();
      ST::STApp::getInstance().openFile(path);
      m_currentFileType = FileType::ST;
   } else if (ext == ".json") {
      TextApp::getInstance().closeFile();
      JSON::JSONApp::getInstance().loadJSONFile(path);
      m_currentFileType = FileType::JSON;
   } else if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".h" || ext == ".hpp" || ext == ".hh"
              || ext == ".hxx") {
      CppApp::getInstance().openFile(path);
      m_currentFileType = FileType::Cpp;
   } else {
      // Plain text: clear any state in the TextApp and load the file.
      TextApp::getInstance().openFile(path);
      m_currentFileType = FileType::Text;
   }

   m_currentFilePath = path;
   std::cout << "[undoApp.Editor] Opened " << path << " as " << fileTypeToString(m_currentFileType) << std::endl;
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

   // Close all backends first
   ST::STApp::getInstance().closeFile();
   JSON::JSONApp::getInstance().closeFile();
   TextApp::getInstance().closeFile();

   // Force open as text using TextApp
   TextApp::getInstance().openFile(path);
   m_currentFileType = FileType::Text;
   m_currentFilePath = path;

   std::cout << "[undoApp.Editor] Opened JSON as text: " << path << std::endl;
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
      openFile(newPath.string());
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
   m_deletePendingPath = path;
   m_showDeleteConfirmation = true;
   ImGui::OpenPopup("Delete Confirmation");
}

void EditorApp::renameFile(const std::string& oldPath, const std::string& newName)
{
   try {
      fs::path oldP = oldPath;
      fs::path newP = oldP.parent_path() / newName;

      if (!fs::exists(newP)) {
         fs::rename(oldP, newP);
         std::cout << "[undoApp.Editor] Renamed: " << oldPath << " -> " << newP.string() << std::endl;

         if (m_currentFilePath == oldPath) {
            m_currentFilePath = newP.string();
         }
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

         if (m_currentFilePath == sourcePath) {
            m_currentFilePath = dst.string();
         }
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

   static bool showWorkspaceDialog = true;
   static char workspacePath[1024] = "";

   if (!m_workspacePath.empty()) {
      strncpy(workspacePath, m_workspacePath.c_str(), sizeof(workspacePath) - 1);
   }

   ImGui::OpenPopup("Select Workspace");
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
   openFile(newPath.string());
   refreshFileTree();
}

// ============================================================================
// Workspace Panel Rendering
// ============================================================================

void EditorApp::renderWorkspacePanel()
{
   auto& pm = undoStudio::core::ProjectManager::getInstance();

   if (ImGui::Begin("Workspace")) {
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
   }
   ImGui::End();

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
      ImGui::TextDisabled("Programs can be added from exports.toml after creation.");
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
         ST::STApp::getInstance().setFunctionReturnType(m_newPOUReturnType);
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
      ImGui::TextDisabled("e.g.  GVL.st  MyStruct.st  config.toml");
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
            if (m_currentFilePath == m_deletePendingPath) {
               closeFile();
            }
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
      case NodeRole::TOMLFile:
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
         openFile(node.path);
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
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
         openFile(node.path);
      }

      if (ImGui::IsItemClicked(1)) {
         ImGui::OpenPopup(("##ctx_" + node.path).c_str());
      }
      if (ImGui::BeginPopup(("##ctx_" + node.path).c_str())) {
         if (ImGui::MenuItem("Open")) {
            openFile(node.path);
         }
         if (isJSON && ImGui::MenuItem("Open as Text")) {
            openFileAsText(node.path);
         }
         if (isST && ImGui::MenuItem("Add Method")) {
            // Ask the ST Editor panel to open its dialog rather than adding a
            // method straight away with a fixed name, so this behaves exactly
            // like the "+" on the tab bar. It has to travel through the ST app
            // because the popup belongs to the ST Editor window, not to this
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

void EditorApp::renderEditorPanel()
{
   ImGuiIO& io = ImGui::GetIO();
   if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && (io.KeyCtrl || io.KeySuper)
       && // Ctrl on Windows/Linux, Cmd on macOS
       ImGui::IsKeyPressed(ImGuiKey_S)) {
      // Sace active file
      switch (m_currentFileType) {
      case FileType::Cpp:
         CppApp::getInstance().saveFile();
         break;
      case FileType::Text:
         TextApp::getInstance().saveFile();
         break;
      default:
         break;
      }
   }
   // Each backend's renderEditorPanel() opens its own ImGui window
   // with the title "Editor", so we don't call ImGui::Begin here.
   // We just dispatch to whichever backend is active.
   switch (m_currentFileType) {
   case FileType::ST:
      // The ST editor is not drawn here. STApp registers panels of its own, "ST
      // Editor" among them, and ImGuiManager::render() calls every registered
      // panel once per frame. Drawing it here as well meant the same panel ran
      // twice in the same frame: the variables and body sections appeared a
      // second time, squeezed into this window, and since both passes handed the
      // same editors to TextEditor::Render, every key press was handled twice and
      // each character came out doubled.
      //
      // The ST editor lives in its own panel; this one is for the backends that
      // have no panel of their own.
      ImGui::TextDisabled("The Structured Text editor is in the 'ST Editor' panel.");
      break;
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
      // No file open - render a small placeholder
      if (ImGui::Begin("Editor")) {
         ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No file open");
         ImGui::TextWrapped("Open a file from the Workspace panel or use the File menu.");
         ImGui::End();
      }
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