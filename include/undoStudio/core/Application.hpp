/**
 * @file Application.hpp
 * @brief Core application manager for undoStudio
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * This file defines the main Application class that manages the lifecycle
 * of the undoStudio IDE. It follows the Singleton pattern and provides
 * access to core services.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <thread>

namespace undoStudio {
namespace core {

/**
 * @brief Main application class for undoStudio
 * 
 * The Application class is the central entry point for the undoStudio IDE.
 * It manages initialization, the main event loop, and shutdown procedures.
 * It also provides a service locator pattern for accessing various services.
 */
class Application
{
public:
   /**
     * @brief Get the singleton instance of the Application
     * @return Reference to the single Application instance
     */
   static Application& getInstance();

   /**
     * @brief Initialize the application with command line arguments
     * @param argc Number of command line arguments
     * @param argv Array of command line argument strings
     * @return true if initialization succeeded, false otherwise
     */
   bool initialize(int argc, char** argv);

   /**
     * @brief Shutdown the application and release all resources
     * 
     * This method properly cleans up all services and releases
     * any allocated resources before application termination.
     */
   void shutdown();

   /**
     * @brief Check if the application is currently running
     * @return true if the application is running, false otherwise
     */
   bool isRunning() const;

   /**
     * @brief Run the main application loop
     * @return Exit code (0 for success, non-zero for errors)
     */
   int run();

   /**
     * @brief Request the application to quit gracefully
     * 
     * This method signals the main loop to exit after completing
     * the current iteration.
     */
   void requestQuit();

   /**
     * @brief Register a service with the application
     * @tparam T Service type
     * @param service Instance of the service
     */
   template<typename T>
   void registerService(T* service)
   {
      m_services[std::type_index(typeid(T))] = service;
   }

   /**
     * @brief Get a registered service by type
     * @tparam T Service type
     * @return Pointer to the service instance, or nullptr if not found
     */
   template<typename T>
   T* getService()
   {
      auto it = m_services.find(std::type_index(typeid(T)));
      if (it != m_services.end()) {
         return static_cast<T*>(it->second);
      }
      return nullptr;
   }

private:
   Application() = default;
   ~Application() = default;
   Application(const Application&) = delete;
   Application& operator=(const Application&) = delete;

   bool m_isRunning = false;
   bool m_quitRequested = false;
   std::unordered_map<std::type_index, void*> m_services;
};

} // namespace core
} // namespace undoStudio