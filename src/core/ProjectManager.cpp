/**
 * @file ProjectManager.cpp
 * @brief Implementation of the undoProject management service
 * @ingroup core
 */

#include "undoStudio/core/ProjectManager.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

namespace undoStudio {
namespace core {

// ============================================================================
// Minimal TOML reader/writer
//
// Supports: [section], [[array_section]], key = "string", key = 42,
//           key = true/false, key = ["a", "b"]  (inline string array)
//           # comments, values on the same line.
// This subset is all we need for the three project config files.
// ============================================================================

namespace {

struct TomlValue
{
   enum class Type {
      String,
      Int,
      Bool,
      StringArray
   } type = Type::String;
   std::string str;
   int64_t num = 0;
   bool b = false;
   std::vector<std::string> arr;
};

using TomlTable = std::map<std::string, TomlValue>;

struct TomlSection
{
   std::string name;
   bool isArray = false;
   TomlTable values;
};

using TomlDoc = std::vector<TomlSection>;

static std::string trimS(const std::string& s)
{
   size_t a = s.find_first_not_of(" \t\r\n");
   if (a == std::string::npos) {
      return "";
   }
   size_t b = s.find_last_not_of(" \t\r\n");
   return s.substr(a, b - a + 1);
}

static TomlValue parseValue(const std::string& raw)
{
   std::string s = trimS(raw);
   TomlValue v;
   if (s.empty()) {
      return v;
   }

   // String "..."
   if (s.front() == '"') {
      v.type = TomlValue::Type::String;
      size_t end = s.find('"', 1);
      if (end != std::string::npos) {
         v.str = s.substr(1, end - 1);
      }
      return v;
   }

   // Inline string array [...]
   if (s.front() == '[') {
      v.type = TomlValue::Type::StringArray;
      size_t end = s.rfind(']');
      std::string inner = (end != std::string::npos) ? s.substr(1, end - 1) : s.substr(1);
      std::istringstream ss(inner);
      std::string item;
      while (std::getline(ss, item, ',')) {
         item = trimS(item);
         if (item.size() >= 2 && item.front() == '"' && item.back() == '"') {
            item = item.substr(1, item.size() - 2);
         }
         if (!item.empty()) {
            v.arr.push_back(item);
         }
      }
      return v;
   }

   if (s == "true") {
      v.type = TomlValue::Type::Bool;
      v.b = true;
      return v;
   }
   if (s == "false") {
      v.type = TomlValue::Type::Bool;
      v.b = false;
      return v;
   }

   // Integer
   try {
      size_t idx;
      v.num = std::stoll(s, &idx);
      if (idx == s.size()) {
         v.type = TomlValue::Type::Int;
         return v;
      }
   } catch (...) {
   }

   return v; // fallback: empty String
}

static TomlDoc parseToml(const std::string& content)
{
   TomlDoc doc;
   TomlSection* cur = nullptr;
   std::istringstream stream(content);
   std::string line;

   while (std::getline(stream, line)) {
      size_t hash = line.find('#');
      if (hash != std::string::npos) {
         line = line.substr(0, hash);
      }
      line = trimS(line);
      if (line.empty()) {
         continue;
      }

      if (line.size() >= 4 && line.substr(0, 2) == "[[") {
         size_t end = line.find("]]", 2);
         std::string name = end != std::string::npos ? trimS(line.substr(2, end - 2)) : line.substr(2);
         doc.push_back({name, true, {}});
         cur = &doc.back();
         continue;
      }

      if (line.front() == '[') {
         size_t end = line.find(']', 1);
         std::string name = end != std::string::npos ? trimS(line.substr(1, end - 1)) : line.substr(1);
         doc.push_back({name, false, {}});
         cur = &doc.back();
         continue;
      }

      size_t eq = line.find('=');
      if (eq == std::string::npos) {
         continue;
      }
      std::string key = trimS(line.substr(0, eq));
      std::string rawVal = trimS(line.substr(eq + 1));
      if (key.empty()) {
         continue;
      }
      if (!cur) {
         doc.push_back({"", false, {}});
         cur = &doc.back();
      }
      cur->values[key] = parseValue(rawVal);
   }

   return doc;
}

// Typed getters
static std::string tomlStr(const TomlSection& sec, const std::string& key, const std::string& def = "")
{
   auto it = sec.values.find(key);
   if (it != sec.values.end() && it->second.type == TomlValue::Type::String) {
      return it->second.str;
   }
   return def;
}

static int64_t tomlInt(const TomlSection& sec, const std::string& key, int64_t def = 0)
{
   auto it = sec.values.find(key);
   if (it != sec.values.end() && it->second.type == TomlValue::Type::Int) {
      return it->second.num;
   }
   return def;
}

static std::vector<std::string> tomlArr(const TomlSection& sec, const std::string& key)
{
   auto it = sec.values.find(key);
   if (it != sec.values.end() && it->second.type == TomlValue::Type::StringArray) {
      return it->second.arr;
   }
   return {};
}

// "on"/"off" (case-insensitive) -> "on"/"off". Anything else is reported as
// the default so a typo lands on the strict reading rather than silently
// disabling the checks.
static std::string tomlOnOff(const TomlSection& sec, const std::string& key, const std::string& def = "on")
{
   std::string raw = tomlStr(sec, key, def);
   std::string lower = raw;
   std::transform(lower.begin(), lower.end(), lower.begin(),
                  [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   return (lower == "on" || lower == "off") ? lower : def;
}

// Serialization helpers
static std::string q(const std::string& s)
{
   return "\"" + s + "\"";
}

static std::string arrStr(const std::vector<std::string>& v)
{
   std::string r = "[";
   for (size_t i = 0; i < v.size(); ++i) {
      if (i) {
         r += ", ";
      }
      r += q(v[i]);
   }
   return r + "]";
}

static bool writeFile(const std::string& path, const std::string& content)
{
   std::ofstream f(path, std::ios::out | std::ios::trunc);
   if (!f.is_open()) {
      std::cerr << "[ProjectManager] Cannot write: " << path << std::endl;
      return false;
   }
   f << content;
   return true;
}

static std::string readFile(const std::string& path)
{
   std::ifstream f(path);
   if (!f.is_open()) {
      return "";
   }
   return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
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
   return plcPath(n) + "/exports.toml";
}
std::string ProjectManager::taskFilePath(const std::string& n) const
{
   return tasksPath() + "/" + n + ".toml";
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

   // Task files: undoCore/tasks/*.toml
   if (startsWith(tasksPath()) && fs::path(path).extension() == ".toml") {
      return NodeRole::TaskFile;
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
   if (ext == ".toml") {
      return NodeRole::TOMLFile;
   }

   return NodeRole::Generic;
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
// TOML I/O
// ============================================================================

bool ProjectManager::readProjectTOML()
{
   auto doc = parseToml(readFile(configDir() + "/project.toml"));
   for (const auto& sec : doc) {
      if (sec.name == "project") {
         m_config.name = tomlStr(sec, "name");
         m_config.version = tomlStr(sec, "version", "1.0.0");
         m_config.author = tomlStr(sec, "author");
         m_config.created = tomlStr(sec, "created");
      }
      if (sec.name == "target") {
         m_config.arch = tomlStr(sec, "arch", "x86_64");
         m_config.os = tomlStr(sec, "os", "linux");
         m_config.rt_kernel = tomlStr(sec, "rt_kernel", "PREEMPT-RT");
      }
      if (sec.name == "semantics") {
         // A project written before this key existed carries no [semantics]
         // section, so it reads as strict, which is what a new project is written with.
         m_config.strictness = tomlOnOff(sec, "strictness", "on");
      }
   }
   return !m_config.name.empty();
}

bool ProjectManager::readPLCsTOML()
{
   m_plcs.clear();
   auto doc = parseToml(readFile(configDir() + "/plcs.toml"));
   for (const auto& sec : doc) {
      if (sec.name == "plc" && sec.isArray) {
         PLCConfig plc;
         plc.name = tomlStr(sec, "name");
         plc.description = tomlStr(sec, "description");
         if (!plc.name.empty()) {
            m_plcs.push_back(std::move(plc));
         }
      }
   }
   return true;
}

bool ProjectManager::readTasksTOML()
{
   m_tasks.clear();
   auto doc = parseToml(readFile(configDir() + "/tasks.toml"));
   for (const auto& sec : doc) {
      if (sec.name == "task" && sec.isArray) {
         TaskConfig t;
         t.name = tomlStr(sec, "name");
         t.plc = tomlStr(sec, "plc");
         t.cycle_ms = static_cast<int>(tomlInt(sec, "cycle_ms", 1));
         t.priority = static_cast<int>(tomlInt(sec, "priority", 80));
         t.cpu_affinity = static_cast<int>(tomlInt(sec, "cpu_affinity", -1));
         t.programs = tomlArr(sec, "programs");
         if (!t.name.empty()) {
            m_tasks.push_back(std::move(t));
         }
      }
   }
   return true;
}

bool ProjectManager::readExportsTOML(const std::string& plcName)
{
   auto doc = parseToml(readFile(plcExportsPath(plcName)));
   for (const auto& sec : doc) {
      auto v = tomlArr(sec, "programs");
      if (!v.empty()) {
         m_exports[plcName] = v;
         return true;
      }
   }
   return true;
}

bool ProjectManager::writeProjectTOML() const
{
   std::ostringstream ss;
   ss << "# undoProject — project metadata\n"
      << "# Generated by undoStudio — do not rename the keys.\n\n"
      << "[project]\n"
      << "name       = " << q(m_config.name) << "\n"
      << "version    = " << q(m_config.version) << "\n"
      << "author     = " << q(m_config.author) << "\n"
      << "created    = " << q(m_config.created) << "\n\n"
      << "[target]\n"
      << "arch       = " << q(m_config.arch) << "\n"
      << "os         = " << q(m_config.os) << "\n"
      << "rt_kernel  = " << q(m_config.rt_kernel) << "\n\n"
      << "[semantics]\n"
      << "strictness = " << q(m_config.strictness == "off" ? "off" : "on") << "\n";
   return writeFile(configDir() + "/project.toml", ss.str());
}

bool ProjectManager::writePLCsTOML() const
{
   std::ostringstream ss;
   ss << "# undoProject — PLC list\n\n";
   for (const auto& plc : m_plcs) {
      ss << "[[plc]]\n"
         << "name        = " << q(plc.name) << "\n"
         << "description = " << q(plc.description) << "\n\n";
   }
   return writeFile(configDir() + "/plcs.toml", ss.str());
}

bool ProjectManager::writeTasksTOML() const
{
   std::ostringstream ss;
   ss << "# undoProject — Task list\n\n";
   for (const auto& t : m_tasks) {
      ss << "[[task]]\n"
         << "name         = " << q(t.name) << "\n"
         << "plc          = " << q(t.plc) << "\n"
         << "cycle_ms     = " << t.cycle_ms << "\n"
         << "priority     = " << t.priority << "\n"
         << "cpu_affinity = " << t.cpu_affinity << "\n"
         << "programs     = " << arrStr(t.programs) << "\n\n";
   }
   return writeFile(configDir() + "/tasks.toml", ss.str());
}

bool ProjectManager::writeExportsTOML(const std::string& plcName) const
{
   std::ostringstream ss;
   ss << "# exports.toml — PROGRAM execution order for " << plcName << "\n"
      << "# Programs are executed in the listed order at each task cycle.\n\n";
   auto it = m_exports.find(plcName);
   if (it != m_exports.end()) {
      ss << "programs = " << arrStr(it->second) << "\n";
   } else {
      ss << "programs = []\n";
   }
   return writeFile(plcExportsPath(plcName), ss.str());
}

bool ProjectManager::writeTaskDetailTOML(const TaskConfig& t) const
{
   std::ostringstream ss;
   ss << "# undoCore task: " << t.name << "\n\n"
      << "[task]\n"
      << "name         = " << q(t.name) << "\n"
      << "plc          = " << q(t.plc) << "\n"
      << "cycle_ms     = " << t.cycle_ms << "\n"
      << "priority     = " << t.priority << "\n"
      << "cpu_affinity = " << t.cpu_affinity << "\n"
      << "programs     = " << arrStr(t.programs) << "\n";
   return writeFile(taskFilePath(t.name), ss.str());
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

   bool ok = writeProjectTOML() && writePLCsTOML() && writeTasksTOML();
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

   bool ok = readProjectTOML() && readPLCsTOML() && readTasksTOML();
   for (const auto& plc : m_plcs) {
      readExportsTOML(plc.name);
   }

   if (!ok) {
      closeProject();
      return false;
   }

   std::cout << "[ProjectManager] Opened project: " << m_config.name << " @ " << m_projectPath << std::endl;
   notifyChanged();
   return true;
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
   bool ok = writeProjectTOML() && writePLCsTOML() && writeTasksTOML();
   for (const auto& plc : m_plcs) {
      ok &= writeExportsTOML(plc.name);
   }
   for (const auto& t : m_tasks) {
      ok &= writeTaskDetailTOML(t);
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
   writeExportsTOML(name);
   writePLCsTOML();

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
   writePLCsTOML();
   writeTasksTOML();
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
         writePLCsTOML();
         writeTasksTOML();
         writeExportsTOML(newName);
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
   writeTasksTOML();
   writeTaskDetailTOML(task);
   notifyChanged();
   return true;
}

bool ProjectManager::removeTask(const std::string& name)
{
   auto it = std::find_if(m_tasks.begin(), m_tasks.end(), [&](const TaskConfig& t) { return t.name == name; });
   if (it == m_tasks.end()) {
      return false;
   }
   // Remove the task detail .toml file
   std::error_code ec;
   fs::remove(taskFilePath(name), ec);
   m_tasks.erase(it);
   writeTasksTOML();
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
   writeTasksTOML();
   writeTaskDetailTOML(task);
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
   return writeExportsTOML(plcName);
}

std::vector<std::string> ProjectManager::getExports(const std::string& plcName) const
{
   auto it = m_exports.find(plcName);
   return (it != m_exports.end()) ? it->second : std::vector<std::string>{};
}

} // namespace core
} // namespace undoStudio