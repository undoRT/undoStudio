/**
 * @file Application.cpp
 * @brief Implementation of the Application class
 * @ingroup core
 * 
 * This file defines the main Application class that manages the lifecycle
 * of the undoStudio IDE. It follows the Singleton pattern and provides
 * access to core services.
 * 
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoStudio/core/Application.hpp"
#include "undoStudio/services/WindowService.hpp"
// #include "undoStudio/services/RenderService.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"
#include "undoStudio/core/PluginManager.hpp"
#include "undoStudio/core/Settings.hpp"

#include <iostream>
#include <memory>
#include <chrono>
#include <thread>

namespace undoStudio {
namespace core {

// ============================================================================
// Singleton Instance
// ============================================================================

Application& Application::getInstance()
{
   static Application instance;
   return instance;
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

bool Application::initialize(int argc, char** argv)
{
   // Suppress unused parameter warnings
   (void) argc;
   (void) argv;

   std::cout << "[undoStudio] Initializing application..." << std::endl;

   try {
      // Initialize window service
      std::cout << "[undoStudio] Initializing window service..." << std::endl;
      auto& windowService = services::WindowService::getInstance();
      services::WindowConfig config;
      config.title = "undoStudio - Industrial Automation IDE";
      // The size is the one from the last run, or the one in the struct when there
      // is no last run. A window the user has sized to their screen should not be
      // dragged back to a default every morning, and the fallback for a first run
      // is the same constant it always was.
      config.width = core::settings::getInt(core::settings::kCoreFile, "window", "width", config.width);
      config.height = core::settings::getInt(core::settings::kCoreFile, "window", "height", config.height);
      config.fullscreen = false;
      config.vsync = true;

      if (!windowService.initialize(config)) {
         std::cerr << "[undoStudio] ERROR: Failed to initialize window service" << std::endl;
         return false;
      }
      registerService(&windowService);

      // Initialize ImGui
      std::cout << "[undoStudio] Initializing ImGui..." << std::endl;
      auto& imguiManager = ui::ImGuiManager::getInstance();
      if (!imguiManager.initialize(windowService.getNativeHandle())) {
         std::cerr << "[undoStudio] ERROR: Failed to initialize ImGui" << std::endl;
         windowService.shutdown();
         return false;
      }
      registerService(&imguiManager);

      // Setup theme
      imguiManager.setUndoRTTheme();
      imguiManager.enableDocking(true);

      // Load default font
      std::cout << "[undoStudio] Loading fonts..." << std::endl;
      if (!imguiManager.loadFont("resources/fonts/Roboto-Regular.ttf", 20.0f)) {
         std::cout << "[undoStudio] Warning: Default font not found, using embedded font" << std::endl;
      }

      // Load undoApp plugins (shared libraries). Each plugin registers its
      // own panels during createUndoApp(), so this must happen after ImGui
      // is initialized but can happen any time before the main loop starts.
      //
      // The IDE is started from the repository root, where the resources are, so
      // an out-of-source build has put the plugins one level down. Both are tried
      // and the second only when the first has nothing: an in-source build has
      // plugins/ at the root and build/ is not where anything else lives.
      std::cout << "[undoStudio] Loading plugins..." << std::endl;
      auto& pluginManager = PluginManager::getInstance();
      if (pluginManager.loadPluginsFromDirectory("plugins") == 0) {
         pluginManager.loadPluginsFromDirectory("build/plugins");
      }
      registerService(&pluginManager);

      // Setup close callback
      windowService.setCloseCallback([this]() { requestQuit(); });

      m_isRunning = true;
      m_quitRequested = false;

      std::cout << "[undoStudio] Application initialized successfully" << std::endl;
      return true;

   } catch (const std::exception& e) {
      std::cerr << "[undoStudio] ERROR during initialization: " << e.what() << std::endl;
      return false;
   }
}

void Application::shutdown()
{
   std::cout << "[undoStudio] Shutting down application..." << std::endl;

   try {
      // Unload plugins first: their panels are std::function objects whose
      // code lives inside the plugin's shared library. destroyUndoApp() is
      // expected to remove them from ImGuiManager before we dlclose().
      PluginManager::getInstance().shutdownAll();

      // Shutdown ImGui
      auto& imguiManager = ui::ImGuiManager::getInstance();
      imguiManager.shutdown();

      // The window's size is written on the way out, after the plugins are gone
      // and before the window goes: it is the last moment at which the size the
      // user left it at can still be read.
      auto& windowService = services::WindowService::getInstance();
      const glm::ivec2 size = windowService.getWindowSize();
      if (size.x > 0 && size.y > 0) {
         core::settings::setInt(core::settings::kCoreFile, "window", "width", size.x);
         core::settings::setInt(core::settings::kCoreFile, "window", "height", size.y);
      }

      windowService.shutdown();

      m_isRunning = false;
      m_services.clear();

      std::cout << "[undoStudio] Application shutdown complete" << std::endl;

   } catch (const std::exception& e) {
      std::cerr << "[undoStudio] ERROR during shutdown: " << e.what() << std::endl;
   }
}

// ============================================================================
// Main Loop
// ============================================================================

int Application::run()
{
   std::cout << "[undoStudio] Starting main loop..." << std::endl;

   auto& windowService = services::WindowService::getInstance();
   auto& imguiManager = ui::ImGuiManager::getInstance();

   // Frame timing
   auto lastTime = std::chrono::high_resolution_clock::now();
   const double targetFrameTime = 1.0 / 60.0; // 60 FPS target

   // Main event loop
   while (isRunning() && !windowService.shouldClose()) {
      auto currentTime = std::chrono::high_resolution_clock::now();
      double deltaTime = std::chrono::duration<double>(currentTime - lastTime).count();
      lastTime = currentTime;

      // Poll window events
      windowService.pollEvents();

      // Begin ImGui frame
      imguiManager.newFrame();

      // Render ImGui UI (all registered panels)
      imguiManager.render();

      // End ImGui frame
      imguiManager.endFrame();

      // Swap buffers
      windowService.swapBuffers();

      // Frame rate limiting
      if (deltaTime < targetFrameTime) {
         std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((targetFrameTime - deltaTime) * 1000)));
      }
   }

   std::cout << "[undoStudio] Main loop exited" << std::endl;
   return 0;
}

// ============================================================================
// Application Control
// ============================================================================

void Application::requestQuit()
{
   std::cout << "[undoStudio] Quit requested" << std::endl;
   m_quitRequested = true;
   m_isRunning = false;
}

bool Application::isRunning() const
{
   return m_isRunning && !m_quitRequested;
}

} // namespace core
} // namespace undoStudio
