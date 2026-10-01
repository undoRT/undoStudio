/**
 * @file PluginManager.cpp
 * @brief Implementation of the PluginManager class
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoStudio/core/PluginManager.hpp"

#include <dlfcn.h>
#include <dirent.h>

#include <algorithm>
#include <iostream>

namespace undoStudio {
namespace core {

PluginManager& PluginManager::getInstance()
{
   static PluginManager instance;
   return instance;
}

int PluginManager::loadPluginsFromDirectory(const std::string& directory)
{
   DIR* dir = opendir(directory.c_str());
   if (!dir) {
      std::cout << "[PluginManager] Plugin directory not found: " << directory << " (skipping)" << std::endl;
      return 0;
   }

   std::vector<std::string> candidates;
   struct dirent* entry;
   while ((entry = readdir(dir)) != nullptr) {
      std::string name = entry->d_name;
      if (name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0) {
         candidates.push_back(directory + "/" + name);
      }
   }
   closedir(dir);

   // Deterministic load order (alphabetical), so behaviour doesn't depend
   // on filesystem iteration order.
   std::sort(candidates.begin(), candidates.end());

   int loaded = 0;
   for (const auto& path : candidates) {
      if (loadPlugin(path)) {
         ++loaded;
      }
   }

   std::cout << "[PluginManager] Loaded " << loaded << " of " << candidates.size() << " candidate(s) from " << directory << std::endl;
   return loaded;
}

bool PluginManager::loadPlugin(const std::string& path)
{
   // RTLD_NOW: resolve every symbol immediately. If the plugin references
   // something undoStudioCore doesn't export, we want to fail loudly here,
   // not crash mid-frame the first time an unresolved symbol is touched.
   void* handle = dlopen(path.c_str(), RTLD_NOW);
   if (!handle) {
      std::cerr << "[PluginManager] Failed to load " << path << ": " << dlerror() << std::endl;
      return false;
   }

   dlerror(); // clear any existing error before dlsym

   auto createFunc = reinterpret_cast<CreateUndoAppFunc>(dlsym(handle, "createUndoApp"));
   const char* createErr = dlerror();

   auto destroyFunc = reinterpret_cast<DestroyUndoAppFunc>(dlsym(handle, "destroyUndoApp"));
   const char* destroyErr = dlerror();

   if (createErr || destroyErr || !createFunc || !destroyFunc) {
      std::cerr << "[PluginManager] " << path << " does not export createUndoApp/destroyUndoApp - skipping" << std::endl;
      dlclose(handle);
      return false;
   }

   void* instance = createFunc();
   if (!instance) {
      std::cerr << "[PluginManager] " << path << ": createUndoApp() returned nullptr" << std::endl;
      dlclose(handle);
      return false;
   }

   m_plugins.push_back(LoadedPlugin{path, handle, instance, destroyFunc});
   std::cout << "[PluginManager] Loaded plugin: " << path << std::endl;
   return true;
}

void PluginManager::shutdownAll()
{
   // Reverse load order: tear down the most recently loaded plugin first.
   for (auto it = m_plugins.rbegin(); it != m_plugins.rend(); ++it) {
      if (it->destroyFunc && it->instance) {
         it->destroyFunc(it->instance);
      }
      if (it->handle) {
         dlclose(it->handle);
      }
      std::cout << "[PluginManager] Unloaded plugin: " << it->path << std::endl;
   }
   m_plugins.clear();
}

} // namespace core
} // namespace undoStudio