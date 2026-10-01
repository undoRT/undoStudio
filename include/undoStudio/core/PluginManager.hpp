/**
 * @file PluginManager.hpp
 * @brief Dynamic plugin loader for undoApps
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * Loads undoApp plugins (shared libraries built like undoApp.Demo) from
 * plugins/ at runtime via dlopen/dlsym. Each plugin exports two C functions:
 *
 *     extern "C" void* createUndoApp();
 *     extern "C" void  destroyUndoApp(void* app);
 *
 * createUndoApp() is expected to fully initialize the app (register its
 * panels, etc.) before returning, and destroyUndoApp() is expected to fully
 * tear it down (including removing any panels it registered) before
 * returning - the manager treats both pointers as opaque.
 *
 * Two panels with the same name are one panel: a registration replaces the
 * callback rather than adding beside it, so the load order decides which body
 * is drawn.
 */

#pragma once

#include <string>
#include <vector>

namespace undoStudio {
namespace core {

using CreateUndoAppFunc = void* (*) ();
using DestroyUndoAppFunc = void (*)(void*);

/**
 * @brief Loads and owns the lifetime of dynamically-loaded undoApp plugins
 */
class PluginManager
{
public:
   /**
     * @brief Get the singleton instance of PluginManager
     */
   static PluginManager& getInstance();

   /**
     * @brief Scan a directory for *.so files and load each as a plugin
     * @param directory Path to scan (relative or absolute)
     * @return Number of plugins successfully loaded
     */
   int loadPluginsFromDirectory(const std::string& directory);

   /**
     * @brief Load a single plugin by path
     * @param path Path to the shared library
     * @return true on success
     */
   bool loadPlugin(const std::string& path);

   /**
     * @brief Tear down and unload every loaded plugin, in reverse load order
     *
     * Must be called BEFORE ImGuiManager::shutdown(): plugin panels are
     * std::function objects whose code lives inside the plugin's shared
     * library, so ImGuiManager must forget about them before dlclose()
     * unmaps that memory.
     */
   void shutdownAll();

   /**
     * @brief Number of currently loaded plugins
     */
   std::size_t pluginCount() const { return m_plugins.size(); }

private:
   PluginManager() = default;
   ~PluginManager() = default;
   PluginManager(const PluginManager&) = delete;
   PluginManager& operator=(const PluginManager&) = delete;

   struct LoadedPlugin
   {
      std::string path;
      void* handle = nullptr;
      void* instance = nullptr;
      DestroyUndoAppFunc destroyFunc = nullptr;
   };

   std::vector<LoadedPlugin> m_plugins;
};

} // namespace core
} // namespace undoStudio