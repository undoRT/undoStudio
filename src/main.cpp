/**
 * @file main.cpp
 * @brief Entry point for undoStudio application
 * 
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoStudio/core/Application.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"
#include "undoStudio/services/WindowService.hpp"

#include <iostream>
#include <implot.h>

int main(int argc, char** argv)
{
   using namespace undoStudio;

   (void) argc;
   (void) argv;

   std::cout << "=== undoStudio v" << STUDIO_VERSION_STRING << " ===" << std::endl;
   std::cout << "Industrial Automation IDE" << std::endl;
   std::cout << "=========================" << std::endl;

   try {
      auto& app = core::Application::getInstance();

      // Initialize method also checks if there are any other available plugins
      if (!app.initialize(argc, argv)) {
         std::cerr << "[ERROR] Failed to initialize application" << std::endl;
         return -1;
      }

      std::cout << "[Main] Registering UI panels..." << std::endl;
      auto& imguiManager = ui::ImGuiManager::getInstance();
      imguiManager.enableDocking(true);

      // RUN
      std::cout << "[Main] Starting application..." << std::endl;
      int result = app.run();

      app.shutdown();

      std::cout << "[Main] Application exited with code: " << result << std::endl;
      return result;

   } catch (const std::exception& e) {
      std::cerr << "[FATAL] Unhandled exception: " << e.what() << std::endl;
      return -1;
   } catch (...) {
      std::cerr << "[FATAL] Unknown exception occurred" << std::endl;
      return -1;
   }
}