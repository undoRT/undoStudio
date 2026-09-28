/**
 * @file DemoApp.hpp
 * @brief Demo undoApp for undoStudio
 * @ingroup undoapps
 * 
 * This file demonstrates how to create a custom undoApp for
 * the undoStudio IDE. It shows the basic structure for extending
 * the IDE with new functionality.
 * 
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <string>

namespace undoApp {
namespace Demo {

/**
 * @brief Main class for the demo undoApp
 * 
 * This class demonstrates the integration of a custom undoApp
 * into the undoStudio IDE. It registers UI panels and provides
 * example functionality.
 */
class DemoApp
{
public:
   /**
     * @brief Get the singleton instance
     * @return Reference to the single DemoApp instance
     */
   static DemoApp& getInstance();

   /**
     * @brief Initialize the demo app
     * @return true on success
     */
   bool initialize();

   /**
     * @brief Shutdown the demo app
     */
   void shutdown();

   /**
     * @brief Register UI panels with the ImGui manager
     */
   void registerPanels();

private:
   /**
     * @brief Render the demo panel content
     */
   static void renderPanel();

   /**
     * @brief Render the about panel
     */
   static void renderAboutPanel();

   bool m_initialized = false;
};

} // namespace Demo
} // namespace undoApp