/**
 * @file ProjectManager.hpp
 * @brief undoProject management service for undoStudio
 * @ingroup core
 *
 * Manages the lifecycle of undoProject projects: creation, opening, closing,
 * and PLC/Task configuration. The project structure on disk is:
 *
 *   myProject/
 *     .undoProject/        <- config folder (hidden)
 *       project.toml       <- name, version, author, target, semantics
 *       plcs.toml          <- list of undoPLC entries
 *       tasks.toml         <- list of undoTask entries with cycle/priority
 *     undoCore/
 *       tasks/             <- one .toml per task (cycle, priority, cpu_affinity)
 *     undoLogic/
 *       undoSharedLibs/    <- shared libraries across all PLCs
 *       undoSharedGVLs/    <- shared global variable lists
 *       undoSharedDUTs/    <- shared data unit types (STRUCT, ENUM)
 *       undoSharedPOUs/    <- shared FUNCTION / FUNCTION_BLOCK / PROGRAM
 *       undoPLC_X/         <- per-PLC sub-tree (name chosen by user)
 *         undoLibs/
 *         undoGVLs/
 *         undoDUTs/
 *         undoPOUs/
 *         exports.toml     <- ordered PROGRAM list exported to undoCore
 *       undoPLC_Y/
 *         ...
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <functional>

namespace undoStudio {
namespace core {

// ============================================================================
// Data structures
// ============================================================================

/**
 * @brief Configuration for a single undoTask
 *
 * Each task maps to one undoPLC and carries a list of PROGRAM POUs
 * that are executed in the given order at each cycle.
 */
struct TaskConfig
{
   std::string name;                  ///< Task name (e.g., "FastTask")
   std::string plc;                   ///< Associated PLC name
   int cycle_ms = 1;                  ///< Cycle time in milliseconds
   int priority = 80;                 ///< RT thread priority (1-99)
   int cpu_affinity = -1;             ///< CPU pin (-1 = any core)
   std::vector<std::string> programs; ///< Ordered PROGRAM execution list
};

/**
 * @brief Configuration entry for a single undoPLC
 */
struct PLCConfig
{
   std::string name;        ///< PLC folder name (e.g., "undoPLC_Axis")
   std::string description; ///< Optional description
};

/**
 * @brief Semantic strictness of a project, as declared in project.toml
 *
 * "on"  -> Strictness::Strict: the st2cpp analyzer enforces the IEC 61131-3
 *          implicit-conversion rules, so a value-losing or cross-family
 *          implicit assignment (e.g. INT := REAL) is reported as an error.
 * "off" -> Strictness::Permissive: the analyzer stays permissive and lets the
 *          generated C++ static_cast perform the conversion silently.
 */
enum class Strictness {
   Off, ///< Permissive: lossy implicit conversions are not reported
   On   ///< Strict IEC 61131-3: lossy implicit conversions are errors
};

/**
 * @brief Top-level project metadata
 */
struct ProjectConfig
{
   std::string name;
   std::string version = "1.0.0";
   std::string author;
   std::string created;
   std::string arch = "x86_64";
   std::string os = "linux";
   std::string rt_kernel = "PREEMPT-RT";
   std::string strictness = "on"; ///< Raw [semantics] strictness value: "on" or "off"

   /// @brief Strictness as declared, defaulting to On for an absent/empty value
   Strictness getStrictness() const { return strictness == "off" ? Strictness::Off : Strictness::On; }
};

// ============================================================================
// NodeRole — semantic classification of paths inside a project
//
// Used by undoAppEditor to offer the right context menu for each tree node
// (e.g., "New FUNCTION_BLOCK" on a undoPOUs folder, not on undoCore).
// ============================================================================
enum class NodeRole {
   Generic,      ///< Regular file or folder outside a project
   ProjectRoot,  ///< The project root directory
   ConfigFolder, ///< .undoProject/
   UndoCore,     ///< undoCore/
   TasksFolder,  ///< undoCore/tasks/
   TaskFile,     ///< undoCore/tasks/*.toml
   UndoLogic,    ///< undoLogic/
   SharedLibs,   ///< undoLogic/undoSharedLibs/
   SharedGVLs,   ///< undoLogic/undoSharedGVLs/
   SharedDUTs,   ///< undoLogic/undoSharedDUTs/
   SharedPOUs,   ///< undoLogic/undoSharedPOUs/
   PLCRoot,      ///< undoLogic/undoPLC_X/
   PLCLibs,      ///< undoLogic/undoPLC_X/undoLibs/
   PLCGVLs,      ///< undoLogic/undoPLC_X/undoGVLs/
   PLCDUTs,      ///< undoLogic/undoPLC_X/undoDUTs/
   PLCPOUs,      ///< undoLogic/undoPLC_X/undoPOUs/
   ExportsFile,  ///< undoLogic/undoPLC_X/exports.toml
   STFile,       ///< Any .st file
   TOMLFile,     ///< Generic .toml config file
};

// ============================================================================
// ProjectManager
// ============================================================================

/**
 * @brief Singleton service that manages the lifecycle of an undoProject
 */
class ProjectManager
{
public:
   static ProjectManager& getInstance();

   // --------------------------------------------------------------------------
   // Project lifecycle
   // --------------------------------------------------------------------------

   /**
    * @brief Create a new project in parentDir/name/ and scaffold all folders
    * @param parentDir Parent directory (must exist)
    * @param name      Project name (becomes the folder name and project.toml name)
    * @return true on success
    */
   bool createProject(const std::string& parentDir, const std::string& name);

   /**
    * @brief Open an existing project from projectDir
    * @param projectDir Directory that contains .undoProject/
    * @return true on success
    */
   bool openProject(const std::string& projectDir);

   /**
    * @brief Close the current project and reset all state
    */
   void closeProject();

   /**
    * @brief Write all TOML config files to disk
    */
   bool saveProject();

   // --------------------------------------------------------------------------
   // State accessors
   // --------------------------------------------------------------------------

   bool isProjectOpen() const { return m_isOpen; }
   const ProjectConfig& getConfig() const { return m_config; }
   const std::vector<PLCConfig>& getPLCs() const { return m_plcs; }
   const std::vector<TaskConfig>& getTasks() const { return m_tasks; }
   std::string getProjectPath() const { return m_projectPath; }
   std::string getProjectName() const { return m_config.name; }

   /// @brief Semantic strictness declared by the open project (Strict when none is open)
   Strictness getStrictness() const { return m_config.getStrictness(); }

   // --------------------------------------------------------------------------
   // PLC management
   // --------------------------------------------------------------------------

   bool addPLC(const std::string& name, const std::string& description = "");
   bool removePLC(const std::string& name);
   bool renamePLC(const std::string& oldName, const std::string& newName);
   bool hasPLC(const std::string& name) const;

   // --------------------------------------------------------------------------
   // Task management
   // --------------------------------------------------------------------------

   bool addTask(const TaskConfig& task);
   bool removeTask(const std::string& name);
   bool updateTask(const TaskConfig& task);
   const TaskConfig* findTask(const std::string& name) const;
   TaskConfig* findTask(const std::string& name);

   // --------------------------------------------------------------------------
   // Per-PLC exports (ordered PROGRAM list assigned to the PLC's task)
   // --------------------------------------------------------------------------

   bool setExports(const std::string& plcName, const std::vector<std::string>& programs);
   std::vector<std::string> getExports(const std::string& plcName) const;

   // --------------------------------------------------------------------------
   // Path helpers — all return absolute paths (empty when no project is open)
   // --------------------------------------------------------------------------

   std::string configDir() const;     ///< .undoProject/
   std::string undoCorePath() const;  ///< undoCore/
   std::string tasksPath() const;     ///< undoCore/tasks/
   std::string undoLogicPath() const; ///< undoLogic/
   std::string sharedLibsPath() const;
   std::string sharedGVLsPath() const;
   std::string sharedDUTsPath() const;
   std::string sharedPOUsPath() const;
   std::string plcPath(const std::string& plcName) const; ///< undoLogic/undoPLC_X/
   std::string plcLibsPath(const std::string& plcName) const;
   std::string plcGVLsPath(const std::string& plcName) const;
   std::string plcDUTsPath(const std::string& plcName) const;
   std::string plcPOUsPath(const std::string& plcName) const;
   std::string plcExportsPath(const std::string& plcName) const;
   std::string taskFilePath(const std::string& taskName) const; ///< undoCore/tasks/taskName.toml

   // --------------------------------------------------------------------------
   // Path classification utilities
   // --------------------------------------------------------------------------

   /**
    * @brief Classify a filesystem path relative to the open project
    * @return NodeRole enum value; NodeRole::Generic if not in a project
    */
   NodeRole classifyPath(const std::string& path) const;

   /**
    * @brief Find which PLC a path belongs to
    * @return PLC name, or "" if the path is not inside any PLC sub-tree
    */
   std::string ownerPLC(const std::string& path) const;

   // --------------------------------------------------------------------------
   // Change notification callback
   // --------------------------------------------------------------------------

   using OnProjectChanged = std::function<void()>;
   void setOnProjectChanged(OnProjectChanged cb) { m_onChanged = cb; }

private:
   ProjectManager() = default;
   ~ProjectManager() = default;
   ProjectManager(const ProjectManager&) = delete;
   ProjectManager& operator=(const ProjectManager&) = delete;

   // TOML I/O
   bool readProjectTOML();
   bool readPLCsTOML();
   bool readTasksTOML();
   bool readExportsTOML(const std::string& plcName);
   bool writeProjectTOML() const;
   bool writePLCsTOML() const;
   bool writeTasksTOML() const;
   bool writeExportsTOML(const std::string& plcName) const;
   bool writeTaskDetailTOML(const TaskConfig& t) const;

   // Filesystem scaffolding
   bool scaffoldProject(const std::string& path, const std::string& name);
   bool scaffoldPLC(const std::string& plcPath);
   static bool mkdirs(const std::string& path);
   static std::string today();
   void notifyChanged()
   {
      if (m_onChanged) {
         m_onChanged();
      }
   }

   // State
   bool m_isOpen = false;
   std::string m_projectPath;
   ProjectConfig m_config;
   std::vector<PLCConfig> m_plcs;
   std::vector<TaskConfig> m_tasks;
   std::map<std::string, std::vector<std::string>> m_exports;
   OnProjectChanged m_onChanged;
};

} // namespace core
} // namespace undoStudio