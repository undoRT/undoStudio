/**
 * @file ProjectManager.hpp
 * @brief undoProject management service for undoStudio
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * Manages the lifecycle of undoProject projects: creation, opening, closing,
 * and PLC/Task configuration. The project structure on disk is:
 *
 *   myProject/
 *     .undoProject/        <- config folder (hidden)
 *       project.json       <- name, version, author, target, semantics
 *       plcs.json          <- list of undoPLC entries
 *       tasks.json         <- list of undoTask entries with cycle/priority
 *     undoCore/
 *       tasks/             <- one .json per task (cycle, priority, cpu_affinity)
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
 *         exports.json     <- ordered PROGRAM list exported to undoCore
 *       undoPLC_Y/
 *         ...
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
 * @brief Semantic strictness of a project, as declared in project.json
 *
 * On   -> Strictness::Strict: the st2cpp analyzer enforces the IEC 61131-3
 *         implicit-conversion rules, so a value-losing or cross-family
 *         implicit assignment (e.g. INT := REAL) is reported as an error.
 * Off  -> Strictness::Permissive: the analyzer stays permissive and lets the
 *         generated C++ static_cast perform the conversion silently.
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
   bool strictness = true; ///< [semantics] strictness as declared in project.json

   /// @brief Strictness as declared, defaulting to On for an absent/empty value
   Strictness getStrictness() const { return strictness ? Strictness::On : Strictness::Off; }
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
   ConfigFile,   ///< .undoProject/*.json
   UndoCore,     ///< undoCore/
   TasksFolder,  ///< undoCore/tasks/
   TaskFile,     ///< undoCore/tasks/*.json
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
   ExportsFile,  ///< undoLogic/undoPLC_X/exports.json
   STFile,       ///< Any .st file
   JSONFile,     ///< Any other .json file
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
     * @param name      Project name (becomes the folder name and project.json name)
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
    * @brief Write all project configuration files to disk
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

   /**
    * @brief The projects opened most recently, newest first
    * @return Absolute paths, at most maxRecentProjects() of them
    *
    * Remembered so they can be offered as a list. A project is not reopened on its
    * own: the IDE starting with somebody's project already on disk is a surprise,
    * and a list is one click away from the same place.
    *
    * Reading the list is what pulls it off disk, on the first call of the run. It
    * has to be here rather than in the code that opens or forgets a project: those
    * are the only calls that used to load it, so a run in which nobody opened
    * anything showed an empty list while the entries sat in the file. That reads
    * as a lost history, and the history was in the state file the whole time.
    */
   const std::vector<std::string>& recentProjects() const {
      loadRecents();
      return m_recentProjects;
   }

    /// @brief How many projects are kept when nothing has been configured
    static constexpr size_t kDefaultMaxRecent = 10;

    /// @brief The least and the most a configured limit may be
    static constexpr size_t kMinRecentLimit = 1;
    static constexpr size_t kMaxRecentLimit = 100;

    /**
     * @brief How many projects the list keeps
     *
     * Read from the state file and clamped to kMinRecentLimit..kMaxRecentLimit.
     * Clamped rather than refused because the file is the IDE's own and is edited
     * by hand when something has gone wrong with it: a limit of zero would leave a
     * list that can never be filled, and one of ten thousand would make every frame
     * after a project is opened walk a list nobody asked for.
     */
    size_t maxRecentProjects() const;

    /// @brief Set how many projects the list keeps
    /// @param count The new limit, clamped the same way maxRecentProjects() is
    void setMaxRecentProjects(size_t count);


   /// @brief Put a project at the head of the list, or move it there if it was there
   /// @param projectPath Absolute path of the project
   void rememberProject(const std::string& projectPath);

   /// @brief Take a project out of the list
   /// @param projectPath Absolute path of the project
   void forgetProject(const std::string& projectPath);

   /// @brief Empty the list
   void clearRecentProjects();

   /// @brief Read the list from the state file, once
   ///
   /// Const because it is a cache: reading the list does not change what the list
   /// is, and recentProjects() is const and has to be able to fill it.
   void loadRecents() const;

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
   std::string taskFilePath(const std::string& taskName) const; ///< undoCore/tasks/taskName.json

   // --------------------------------------------------------------------------
   // Path classification utilities
   // --------------------------------------------------------------------------

   /**
    * @brief Classify a filesystem path relative to the open project
    * @return NodeRole enum value; NodeRole::Generic if not in a project
    */
   NodeRole classifyPath(const std::string& path) const;

   /**
    * @brief Whether a path is a configuration file this service owns
    * @return true for .undoProject files, undoCore/tasks files, and the exports.json of a PLC
    *
    * The editor asks, because these are JSON and so open in the JSON viewer, which
    * shows a tree and cannot be typed into. exports.json in particular is a file
    * the user is told to edit by hand, so it is opened as text instead of as a
    * picture of itself.
    */
   bool isConfigFile(const std::string& path) const;

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

   // Project configuration I/O. Every file this service writes is JSON: there is
   // no reader for any other format, and no migration from one.
   bool readProjectJSON();
   bool readPLCsJSON();
   bool readTasksJSON();
   bool readExportsJSON(const std::string& plcName);
   bool writeProjectJSON() const;
   bool writePLCsJSON() const;
   bool writeTasksJSON() const;
   bool writeExportsJSON(const std::string& plcName) const;
   bool writeTaskDetailJSON(const TaskConfig& t) const;

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
   /// Opened projects, newest first. Mutable because reading the list fills it,
   /// and reading is const.
   mutable std::vector<std::string> m_recentProjects;
   mutable bool m_recentsLoaded = false;
   std::map<std::string, std::vector<std::string>> m_exports;
   OnProjectChanged m_onChanged;
};

} // namespace core
} // namespace undoStudio