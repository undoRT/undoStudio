/**
 * @file ProjectManager.cpp
 * @brief Implementation of the undoProject management service
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoStudio/core/ProjectManager.hpp"
#include "undoStudio/core/Settings.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <algorithm>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace undoStudio {
namespace core {

// ============================================================================
// JSON I/O for the project configuration
//
// The five files a project is described by are JSON, and nlohmann/json is already
// vendored for the Editor's JSON viewer, so there is no hand-written parser to keep
// in step with the writer. A value read back is the value written, which is the
// whole reason the "on"/"off" spelling of strictness could become a real boolean.
// ============================================================================

namespace {

// Typed getters. Each one answers with the default when the key is absent or is
// not of the type asked for, so a file edited by hand that is nearly right still
// opens: a project is not unreadable because a number arrived as a string.
json section(const json& doc, const char* key)
{
   if (doc.is_object()) {
      const auto it = doc.find(key);
      if (it != doc.end() && it->is_object()) {
         return *it;
      }
   }
   return json::object();
}

json array(const json& doc, const char* key)
{
   if (doc.is_object()) {
      const auto it = doc.find(key);
      if (it != doc.end() && it->is_array()) {
         return *it;
      }
   }
   return json::array();
}

std::string str(const json& obj, const char* key, const std::string& def = "")
{
   if (obj.is_object()) {
      const auto it = obj.find(key);
      if (it != obj.end() && it->is_string()) {
         return it->get<std::string>();
      }
   }
   return def;
}

int num(const json& obj, const char* key, int def = 0)
{
   if (obj.is_object()) {
      const auto it = obj.find(key);
      if (it != obj.end() && it->is_number()) {
         return it->get<int>();
      }
   }
   return def;
}

bool flag(const json& obj, const char* key, bool def)
{
   if (obj.is_object()) {
      const auto it = obj.find(key);
      if (it != obj.end() && it->is_boolean()) {
         return it->get<bool>();
      }
   }
   return def;
}

std::vector<std::string> strList(const json& obj, const char* key)
{
   std::vector<std::string> out;
   for (const auto& e : array(obj, key)) {
      if (e.is_string()) {
         out.push_back(e.get<std::string>());
      }
   }
   return out;
}

json taskToJSON(const TaskConfig& t)
{
   json j;
   j["name"] = t.name;
   j["plc"] = t.plc;
   j["cycle_ms"] = t.cycle_ms;
   j["priority"] = t.priority;
   j["cpu_affinity"] = t.cpu_affinity;
   j["programs"] = t.programs;
   return j;
}

TaskConfig taskFromJSON(const json& j)
{
   TaskConfig t;
   t.name = str(j, "name");
   t.plc = str(j, "plc");
   t.cycle_ms = num(j, "cycle_ms", 1);
   t.priority = num(j, "priority", 80);
   t.cpu_affinity = num(j, "cpu_affinity", -1);
   t.programs = strList(j, "programs");
   return t;
}

// A file that cannot be read is reported and read as empty, rather than closing
// the project: the name of a PLC and the cycle time of a task are worth keeping
// even if the key that was wrong has to be typed again.
json readJSONFile(const std::string& path)
{
   std::ifstream f(path);
   if (!f.is_open()) {
      return json::object();
   }
   json doc = json::parse(f, nullptr, /*allow_exceptions=*/false);
   if (doc.is_discarded()) {
      std::cerr << "[ProjectManager] Cannot parse: " << path << std::endl;
      return json::object();
   }
   return doc;
}

bool writeJSONFile(const std::string& path, const json& doc)
{
   std::ofstream f(path, std::ios::out | std::ios::trunc);
   if (!f.is_open()) {
      std::cerr << "[ProjectManager] Cannot write: " << path << std::endl;
      return false;
   }
   f << doc.dump(2) << "\n";
   return true;
}

} // anonymous namespace

// ============================================================================
// Singleton
// ============================================================================

ProjectManager& ProjectManager::getInstance()
{
   static ProjectManager instance;
   return instance;
}

// ============================================================================
// Static helpers
// ============================================================================

bool ProjectManager::mkdirs(const std::string& path)
{
   std::error_code ec;
   fs::create_directories(path, ec);
   if (ec) {
      std::cerr << "[ProjectManager] mkdirs failed: " << path << " — " << ec.message() << std::endl;
      return false;
   }
   return true;
}

std::string ProjectManager::today()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::tm* tm_info = std::localtime(&t);
    if (!tm_info) {
       return "";
    }
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
    return buf;
}

// ============================================================================
// Path helpers
// ============================================================================

std::string ProjectManager::configDir() const
{
   return m_isOpen ? m_projectPath + "/.undoProject" : "";
}
std::string ProjectManager::undoCorePath() const
{
   return m_isOpen ? m_projectPath + "/undoCore" : "";
}
std::string ProjectManager::tasksPath() const
{
   return m_isOpen ? m_projectPath + "/undoCore/tasks" : "";
}
std::string ProjectManager::undoLogicPath() const
{
   return m_isOpen ? m_projectPath + "/undoLogic" : "";
}
std::string ProjectManager::sharedLibsPath() const
{
   return m_isOpen ? m_projectPath + "/undoLogic/undoSharedLibs" : "";
}
std::string ProjectManager::sharedGVLsPath() const
{
   return m_isOpen ? m_projectPath + "/undoLogic/undoSharedGVLs" : "";
}
std::string ProjectManager::sharedDUTsPath() const
{
   return m_isOpen ? m_projectPath + "/undoLogic/undoSharedDUTs" : "";
}
std::string ProjectManager::sharedPOUsPath() const
{
   return m_isOpen ? m_projectPath + "/undoLogic/undoSharedPOUs" : "";
}
std::string ProjectManager::plcPath(const std::string& n) const
{
   return m_isOpen ? m_projectPath + "/undoLogic/" + n : "";
}
std::string ProjectManager::plcLibsPath(const std::string& n) const
{
   return plcPath(n) + "/undoLibs";
}
std::string ProjectManager::plcGVLsPath(const std::string& n) const
{
   return plcPath(n) + "/undoGVLs";
}
std::string ProjectManager::plcDUTsPath(const std::string& n) const
{
   return plcPath(n) + "/undoDUTs";
}
std::string ProjectManager::plcPOUsPath(const std::string& n) const
{
   return plcPath(n) + "/undoPOUs";
}
std::string ProjectManager::plcExportsPath(const std::string& n) const
{
   return plcPath(n) + "/exports.json";
}
std::string ProjectManager::taskFilePath(const std::string& n) const
{
   return tasksPath() + "/" + n + ".json";
}

// ============================================================================
// Path classification
// ============================================================================

NodeRole ProjectManager::classifyPath(const std::string& path) const
{
   if (!m_isOpen) {
      return NodeRole::Generic;
   }

   // Normalize to canonical string for prefix tests
   auto norm = [](const std::string& p) -> std::string {
      std::error_code ec;
      auto cp = fs::weakly_canonical(fs::path(p), ec);
      return ec ? p : cp.string();
   };

   std::string np = norm(path);
   std::string root = norm(m_projectPath);

   if (np == root) {
      return NodeRole::ProjectRoot;
   }

   auto startsWith = [&](const std::string& prefix) {
      std::string np2 = norm(prefix);
      return np == np2 || np.rfind(np2, 0) == 0;
   };
   auto eq = [&](const std::string& other) {
      return np == norm(other);
   };

   if (eq(configDir())) {
      return NodeRole::ConfigFolder;
   }
   if (eq(undoCorePath())) {
      return NodeRole::UndoCore;
   }
   if (eq(tasksPath())) {
      return NodeRole::TasksFolder;
   }
   if (eq(undoLogicPath())) {
      return NodeRole::UndoLogic;
   }
   if (eq(sharedLibsPath())) {
      return NodeRole::SharedLibs;
   }
   if (eq(sharedGVLsPath())) {
      return NodeRole::SharedGVLs;
   }
   if (eq(sharedDUTsPath())) {
      return NodeRole::SharedDUTs;
   }
   if (eq(sharedPOUsPath())) {
      return NodeRole::SharedPOUs;
   }

   // Task files: undoCore/tasks/*.json
   if (startsWith(tasksPath()) && fs::path(path).extension() == ".json") {
      return NodeRole::TaskFile;
   }

   // The project description itself: .undoProject/*.json
   if (fs::path(np).parent_path().string() == norm(configDir())) {
      return NodeRole::ConfigFile;
   }

   // PLC sub-trees
   for (const auto& plc : m_plcs) {
      std::string pp = norm(plcPath(plc.name));
      if (np == pp) {
         return NodeRole::PLCRoot;
      }
      if (np == norm(plcLibsPath(plc.name))) {
         return NodeRole::PLCLibs;
      }
      if (np == norm(plcGVLsPath(plc.name))) {
         return NodeRole::PLCGVLs;
      }
      if (np == norm(plcDUTsPath(plc.name))) {
         return NodeRole::PLCDUTs;
      }
      if (np == norm(plcPOUsPath(plc.name))) {
         return NodeRole::PLCPOUs;
      }
      if (np == norm(plcExportsPath(plc.name))) {
         return NodeRole::ExportsFile;
      }
   }

   // File extensions
   std::string ext = fs::path(path).extension().string();
   if (ext == ".st") {
      return NodeRole::STFile;
   }
   if (ext == ".json") {
      return NodeRole::JSONFile;
   }

   return NodeRole::Generic;
}

bool ProjectManager::isConfigFile(const std::string& path) const
{
   switch (classifyPath(path)) {
   case NodeRole::ConfigFile:
   case NodeRole::TaskFile:
   case NodeRole::ExportsFile:
      return true;
   default:
      return false;
   }
}

std::string ProjectManager::ownerPLC(const std::string& path) const
{
   if (!m_isOpen) {
      return "";
   }
   std::error_code ec;
   auto np = fs::weakly_canonical(fs::path(path), ec).string();
   for (const auto& plc : m_plcs) {
      auto pp = fs::weakly_canonical(fs::path(plcPath(plc.name)), ec).string();
      if (np.rfind(pp, 0) == 0) {
         return plc.name;
      }
   }
   return "";
}

// ============================================================================
// JSON I/O
// ============================================================================

bool ProjectManager::readProjectJSON()
{
   const json doc = readJSONFile(configDir() + "/project.json");

   const json project = section(doc, "project");
   m_config.name = str(project, "name");
   m_config.version = str(project, "version", "1.0.0");
   m_config.author = str(project, "author");
   m_config.created = str(project, "created");

   const json target = section(doc, "target");
   m_config.arch = str(target, "arch", "x86_64");
   m_config.os = str(target, "os", "linux");
   m_config.rt_kernel = str(target, "rt_kernel", "PREEMPT-RT");

   // A project written before this key existed carries no [semantics] section, so it
   // reads as strict, which is what a new project is written with.
   m_config.strictness = flag(section(doc, "semantics"), "strictness", true);

   return !m_config.name.empty();
}

bool ProjectManager::readPLCsJSON()
{
   m_plcs.clear();
   for (const auto& entry : array(readJSONFile(configDir() + "/plcs.json"), "plcs")) {
      if (!entry.is_object()) {
         continue;
      }
      PLCConfig plc;
      plc.name = str(entry, "name");
      plc.description = str(entry, "description");
      if (!plc.name.empty()) {
         m_plcs.push_back(std::move(plc));
      }
   }
   return true;
}

bool ProjectManager::readTasksJSON()
{
   m_tasks.clear();
   for (const auto& entry : array(readJSONFile(configDir() + "/tasks.json"), "tasks")) {
      if (!entry.is_object()) {
         continue;
      }
      TaskConfig t = taskFromJSON(entry);
      if (!t.name.empty()) {
         m_tasks.push_back(std::move(t));
      }
   }
   return true;
}

bool ProjectManager::readExportsJSON(const std::string& plcName)
{
   m_exports[plcName] = strList(readJSONFile(plcExportsPath(plcName)), "programs");
   return true;
}

bool ProjectManager::writeProjectJSON() const
{
   json doc;
   doc["project"] = {{"name", m_config.name},
                     {"version", m_config.version},
                     {"author", m_config.author},
                     {"created", m_config.created}};
   doc["target"] = {{"arch", m_config.arch},
                    {"os", m_config.os},
                    {"rt_kernel", m_config.rt_kernel}};
   doc["semantics"] = {{"strictness", m_config.strictness}};
   return writeJSONFile(configDir() + "/project.json", doc);
}

bool ProjectManager::writePLCsJSON() const
{
   json plcs = json::array();
   for (const auto& plc : m_plcs) {
      plcs.push_back({{"name", plc.name}, {"description", plc.description}});
   }
   json doc;
   doc["plcs"] = std::move(plcs);
   return writeJSONFile(configDir() + "/plcs.json", doc);
}

bool ProjectManager::writeTasksJSON() const
{
   json tasks = json::array();
   for (const auto& t : m_tasks) {
      tasks.push_back(taskToJSON(t));
   }
   json doc;
   doc["tasks"] = std::move(tasks);
   return writeJSONFile(configDir() + "/tasks.json", doc);
}

bool ProjectManager::writeExportsJSON(const std::string& plcName) const
{
   // The ordered PROGRAM list is the whole file. It is written empty rather than not
   // at all, so that the order can be typed in by hand: the file is the one piece of
   // the project's configuration the user is expected to edit.
   const auto it = m_exports.find(plcName);
   json doc;
   doc["programs"] = (it != m_exports.end()) ? json(it->second) : json::array();
   return writeJSONFile(plcExportsPath(plcName), doc);
}

bool ProjectManager::writeTaskDetailJSON(const TaskConfig& t) const
{
   json doc;
   doc["task"] = taskToJSON(t);
   return writeJSONFile(taskFilePath(t.name), doc);
}

// ============================================================================
// Filesystem scaffolding
// ============================================================================

bool ProjectManager::scaffoldPLC(const std::string& pp)
{
   return mkdirs(pp + "/undoLibs") && mkdirs(pp + "/undoGVLs") && mkdirs(pp + "/undoDUTs") && mkdirs(pp + "/undoPOUs");
}

bool ProjectManager::scaffoldProject(const std::string& path, const std::string& name)
{
   (void) name;
   bool ok = true;
   ok &= mkdirs(path + "/.undoProject");
   ok &= mkdirs(path + "/undoCore/tasks");
   ok &= mkdirs(path + "/undoLogic/undoSharedLibs");
   ok &= mkdirs(path + "/undoLogic/undoSharedGVLs");
   ok &= mkdirs(path + "/undoLogic/undoSharedPOUs");
   ok &= mkdirs(path + "/undoLogic/undoSharedDUTs");
   return ok;
}

// ============================================================================
// Project lifecycle
// ============================================================================

bool ProjectManager::createProject(const std::string& parentDir, const std::string& name)
{
   if (name.empty()) {
      std::cerr << "[ProjectManager] Empty project name\n";
      return false;
   }
   std::string path = (fs::path(parentDir) / name).string();
   if (fs::exists(path)) {
      std::cerr << "[ProjectManager] Path already exists: " << path << "\n";
      return false;
   }

   if (!scaffoldProject(path, name)) {
      return false;
   }

   m_projectPath = path;
   m_isOpen = true;
   m_plcs.clear();
   m_tasks.clear();
   m_exports.clear();
   m_config = ProjectConfig{};
   m_config.name = name;
   m_config.created = today();

   bool ok = writeProjectJSON() && writePLCsJSON() && writeTasksJSON();
   if (!ok) {
      closeProject();
      return false;
   }

   std::cout << "[ProjectManager] Created project: " << path << std::endl;
   notifyChanged();
   return true;
}

bool ProjectManager::openProject(const std::string& projectDir)
{
   std::string cfgDir = (fs::path(projectDir) / ".undoProject").string();
   if (!fs::exists(cfgDir)) {
      std::cerr << "[ProjectManager] Not an undoProject (no .undoProject/): " << projectDir << "\n";
      return false;
   }

   closeProject();
   m_projectPath = fs::canonical(fs::path(projectDir)).string();
   m_isOpen = true;

   bool ok = readProjectJSON() && readPLCsJSON() && readTasksJSON();
   for (const auto& plc : m_plcs) {
      readExportsJSON(plc.name);
   }

   if (!ok) {
      closeProject();
      return false;
   }

   std::cout << "[ProjectManager] Opened project: " << m_config.name << " @ " << m_projectPath << std::endl;
   rememberProject(m_projectPath);
   notifyChanged();
   return true;
}

// ============================================================================
// Recent projects
// ============================================================================

size_t ProjectManager::maxRecentProjects() const
{
   // Clamped on the way out as well as on the way in: the file can be edited by
   // hand between runs, and a limit of zero read back would silently make the list
   // empty forever with nothing to say why.
   const int stored = settings::getInt(settings::kCoreFile, "recent", "max", static_cast<int>(kDefaultMaxRecent));
   if (stored < static_cast<int>(kMinRecentLimit)) {
      return kMinRecentLimit;
   }
   if (stored > static_cast<int>(kMaxRecentLimit)) {
      return kMaxRecentLimit;
   }
   return static_cast<size_t>(stored);
}

void ProjectManager::setMaxRecentProjects(size_t count)
{
   size_t clamped = count;
   if (clamped < kMinRecentLimit) {
      clamped = kMinRecentLimit;
   }
   if (clamped > kMaxRecentLimit) {
      clamped = kMaxRecentLimit;
   }
   settings::setInt(settings::kCoreFile, "recent", "max", static_cast<int>(clamped));
   // Trim now rather than at the next rememberProject: a limit lowered from fifty
   // to five should leave five entries, not fifty of which forty-five appear in no
   // list and go on being written to the file.
   loadRecents();
   if (m_recentProjects.size() > clamped) {
      m_recentProjects.resize(clamped);
      settings::setList(settings::kCoreFile, "recent", "project", m_recentProjects);
   }
}

void ProjectManager::rememberProject(const std::string& projectPath)
{
   if (projectPath.empty()) {
      return;
   }
   loadRecents();

   // Newest first, and a project that was already in the list moves rather than
   // joining: the list is ordered by when a project was last looked at, and an
   // entry appearing twice is the thing a user notices first.
   m_recentProjects.erase(std::remove(m_recentProjects.begin(), m_recentProjects.end(), projectPath),
                          m_recentProjects.end());
   m_recentProjects.insert(m_recentProjects.begin(), projectPath);
   const size_t limit = maxRecentProjects();
   if (m_recentProjects.size() > limit) {
      m_recentProjects.resize(limit);
   }
   settings::setList(settings::kCoreFile, "recent", "project", m_recentProjects);
}

void ProjectManager::forgetProject(const std::string& projectPath)
{
   loadRecents();
   const size_t before = m_recentProjects.size();
   m_recentProjects.erase(std::remove(m_recentProjects.begin(), m_recentProjects.end(), projectPath),
                          m_recentProjects.end());
   if (m_recentProjects.size() != before) {
      settings::setList(settings::kCoreFile, "recent", "project", m_recentProjects);
   }
}

void ProjectManager::clearRecentProjects()
{
   loadRecents();
   m_recentProjects.clear();
   settings::setList(settings::kCoreFile, "recent", "project", m_recentProjects);
}

void ProjectManager::loadRecents() const
{
   if (m_recentsLoaded) {
      return;
   }
   m_recentsLoaded = true;
   m_recentProjects = settings::getList(settings::kCoreFile, "recent", "project");
}

void ProjectManager::closeProject()
{
   m_isOpen = false;
   m_projectPath.clear();
   m_config = ProjectConfig{};
   m_plcs.clear();
   m_tasks.clear();
   m_exports.clear();
   notifyChanged();
}

bool ProjectManager::saveProject()
{
   if (!m_isOpen) {
      return false;
   }
   bool ok = writeProjectJSON() && writePLCsJSON() && writeTasksJSON();
   for (const auto& plc : m_plcs) {
      ok &= writeExportsJSON(plc.name);
   }
   for (const auto& t : m_tasks) {
      ok &= writeTaskDetailJSON(t);
   }
   if (ok) {
      std::cout << "[ProjectManager] Project saved." << std::endl;
   }
   return ok;
}

// ============================================================================
// PLC management
// ============================================================================

bool ProjectManager::hasPLC(const std::string& name) const
{
   return std::any_of(m_plcs.begin(), m_plcs.end(), [&](const PLCConfig& p) { return p.name == name; });
}

bool ProjectManager::addPLC(const std::string& name, const std::string& description)
{
   if (name.empty() || hasPLC(name)) {
      return false;
   }

   std::string pp = plcPath(name);
   if (!scaffoldPLC(pp)) {
      return false;
   }

   m_plcs.push_back({name, description});
   m_exports[name] = {};
   writeExportsJSON(name);
   writePLCsJSON();

   std::cout << "[ProjectManager] Added PLC: " << name << std::endl;
   notifyChanged();
   return true;
}

bool ProjectManager::removePLC(const std::string& name)
{
   auto it = std::find_if(m_plcs.begin(), m_plcs.end(), [&](const PLCConfig& p) { return p.name == name; });
   if (it == m_plcs.end()) {
      return false;
   }
   m_plcs.erase(it);
   m_exports.erase(name);
   // Remove the task whose PLC was this, if any
   m_tasks.erase(std::remove_if(m_tasks.begin(), m_tasks.end(), [&](const TaskConfig& t) { return t.plc == name; }), m_tasks.end());
   writePLCsJSON();
   writeTasksJSON();
   notifyChanged();
   return true;
}

bool ProjectManager::renamePLC(const std::string& oldName, const std::string& newName)
{
   if (newName.empty() || hasPLC(newName)) {
      return false;
   }
   for (auto& plc : m_plcs) {
      if (plc.name == oldName) {
         // Move directory
         std::error_code ec;
         fs::rename(plcPath(oldName), plcPath(newName), ec);
         if (ec) {
            return false;
         }
         plc.name = newName;
         // Update exports map
         auto exIt = m_exports.find(oldName);
         if (exIt != m_exports.end()) {
            m_exports[newName] = std::move(exIt->second);
            m_exports.erase(oldName);
         }
         // Update any task that referenced the old PLC name
         for (auto& t : m_tasks) {
            if (t.plc == oldName) {
               t.plc = newName;
            }
         }
         writePLCsJSON();
         writeTasksJSON();
         writeExportsJSON(newName);
         notifyChanged();
         return true;
      }
   }
   return false;
}

// ============================================================================
// Task management
// ============================================================================

const TaskConfig* ProjectManager::findTask(const std::string& name) const
{
   auto it = std::find_if(m_tasks.begin(), m_tasks.end(), [&](const TaskConfig& t) { return t.name == name; });
   return (it != m_tasks.end()) ? &(*it) : nullptr;
}

TaskConfig* ProjectManager::findTask(const std::string& name)
{
   auto it = std::find_if(m_tasks.begin(), m_tasks.end(), [&](const TaskConfig& t) { return t.name == name; });
   return (it != m_tasks.end()) ? &(*it) : nullptr;
}

bool ProjectManager::addTask(const TaskConfig& task)
{
   if (task.name.empty() || findTask(task.name)) {
      return false;
   }
   m_tasks.push_back(task);
   writeTasksJSON();
   writeTaskDetailJSON(task);
   notifyChanged();
   return true;
}

bool ProjectManager::removeTask(const std::string& name)
{
   auto it = std::find_if(m_tasks.begin(), m_tasks.end(), [&](const TaskConfig& t) { return t.name == name; });
   if (it == m_tasks.end()) {
      return false;
   }
   // Remove the task detail .json file
   std::error_code ec;
   fs::remove(taskFilePath(name), ec);
   m_tasks.erase(it);
   writeTasksJSON();
   notifyChanged();
   return true;
}

bool ProjectManager::updateTask(const TaskConfig& task)
{
   TaskConfig* existing = findTask(task.name);
   if (!existing) {
      return false;
   }
   *existing = task;
   writeTasksJSON();
   writeTaskDetailJSON(task);
   notifyChanged();
   return true;
}

// ============================================================================
// Exports management
// ============================================================================

bool ProjectManager::setExports(const std::string& plcName, const std::vector<std::string>& programs)
{
   if (!hasPLC(plcName)) {
      return false;
   }
   m_exports[plcName] = programs;
   return writeExportsJSON(plcName);
}

std::vector<std::string> ProjectManager::getExports(const std::string& plcName) const
{
   auto it = m_exports.find(plcName);
   return (it != m_exports.end()) ? it->second : std::vector<std::string>{};
}

} // namespace core
} // namespace undoStudio