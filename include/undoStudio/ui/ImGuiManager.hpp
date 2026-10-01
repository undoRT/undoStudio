/**
 * @file ImGuiManager.hpp
 * @brief ImGui UI management for undoStudio
 * @ingroup ui
 * 
 * This file defines the ImGuiManager class that provides
 * Dear ImGui integration for the undoStudio IDE. It handles
 * UI rendering, panel management, docking, and theming.
 * 
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <imgui.h>
#include <deque>
#include <memory>
#include <functional>
#include <vector>
#include <string>
#include <unordered_map>
#include "version.hpp"

namespace undoStudio {
namespace ui {

/**
 * @brief Manager class for Dear ImGui integration
 * 
 * ImGuiManager provides a high-level interface for managing
 * the ImGui-based user interface. It handles panel registration,
 * docking, theming, and font management.
 */
class ImGuiManager
{
public:
   /**
    * @brief Get the singleton instance
    * @return Reference to the single ImGuiManager instance
    */
   static ImGuiManager& getInstance();

   /**
    * @brief Initialize ImGui with the given window
    * @param window Native window handle
    * @return true on success, false on failure
    */
   bool initialize(void* window);

   /**
    * @brief Shutdown ImGui and release resources
    */
   void shutdown();

   /**
    * @brief Begin a new ImGui frame
    * 
    * This method should be called at the beginning of each frame
    * before any UI rendering.
    */
   void newFrame();

   /**
    * @brief Render all registered panels
    * 
    * This method renders all panels that have been registered
    * with the manager.
    */
   void render();

   /**
    * @brief End the ImGui frame
    * 
    * This method finalizes the frame and renders the ImGui draw data.
    */
   void endFrame();

   /**
    * @brief Write the display geometry to the log whenever it changes
    *
    * One line for the window, one per monitor and one per platform viewport, and
    * nothing at all while none of them moves. A popup is a platform viewport of
    * its own, and a mixed-DPI setup is invisible from here: the numbers that
    * decide whether a popup is drawn once or redrawn every frame are the scale
    * of the monitor the window is on and the scale ImGui gives the viewport,
    * and both are only visible at run time.
    *
    * A viewport that is created and destroyed every frame is what a log full of
    * lines means, so the identifier is in the report: the same popup appearing
    * under a new id on the next line is a popup being rebuilt, and one line per
    * frame is the reading to take from it.
    */
   void reportDisplayIfChanged();

   /**
    * @brief Panel rendering function type
    * 
    * This function type defines the signature for panel rendering
    * functions. Each panel provides its own rendering logic.
    */
   using PanelRenderFunc = std::function<void()>;

   /**
    * @brief Add a panel to the UI
    * @param name Unique panel name
    * @param renderFunc Function that renders the panel content
    */
   void addPanel(const std::string& name, PanelRenderFunc renderFunc);

   /**
    * @brief Remove a panel from the UI
    * @param name Name of the panel to remove
    */
   void removePanel(const std::string& name);

   /**
    * @brief Show or hide a panel
    * @param name Name of the panel
    * @param show true to show, false to hide
    */
   void showPanel(const std::string& name, bool show = true);

   /**
    * @brief Check if a panel is visible
    * @param name Name of the panel
    * @return true if visible, false otherwise
    */
   bool isPanelVisible(const std::string& name) const;

   /**
    * @brief Set the UI theme to match undoRT website colors
    */
   void setUndoRTTheme();

   /**
    * @brief Set the UI theme to dark mode
    */
   void setDarkTheme();

   /**
    * @brief Set the UI theme to light mode
    */
   void setLightTheme();

   /**
    * @brief Load a custom theme from file
    * @param themePath Path to the theme file
    */
   void setCustomTheme(const std::string& themePath);

   /**
    * @brief Load a font for the UI
    * @param path Path to the font file (.ttf)
    * @param size Font size in pixels
    * @param name Optional name for the font
    * @return true on success, false on failure
    */
   bool loadFont(const std::string& path, float size, const std::string& name = "default");

   /**
    * @brief Enable or disable window docking
    * @param enable true to enable docking, false to disable
    */
   void enableDocking(bool enable = true);

   /**
    * @brief Reset the docking layout to default
    */
   void resetLayout();

   /**
    * @brief Save the current layout to file
    * @param filename Path to the layout file
    */
   void saveLayout(const std::string& filename = "undoStudio_layout.ini");

   /**
    * @brief Load a layout from file
    * @param filename Path to the layout file
    */
   void loadLayout(const std::string& filename = "undoStudio_layout.ini");

   /// @brief Name of the recent projects popup, shared by the ask and the draw
   static constexpr const char* kRecentProjectsPopup = "##recentProjects";

   /// @brief Name of the recent files popup, shared by the ask and the draw
   static constexpr const char* kRecentFilesPopup = "##recentFiles";

   /// @brief Ask for the recent projects list to be shown on the next frame
   void openRecentProjects();

   /// @brief Draw the recent projects list, and set a request when one is picked
   void renderRecentProjects();

   /// @brief Ask for the recent files list to be shown on the next frame
   void openRecentFiles();

   /**
    * @brief Draw the recent files list, and ask for the picked one to be opened
    *
    * The pick goes through requestOpenFile rather than opening anything here, for
    * the same reason the project list does not open a project: the editor that shows
    * a file belongs to an undoApp, and the core asks rather than calls.
    */
   void renderRecentFiles();

   /**
    * @brief Take the project a menu asked to open, if one was asked for
    *
    * The menu bar is the core and the workspace that opens a project belongs to an
    * undoApp, so the request crosses as a flag rather than as a call: one click
    * leaves it, and the undoApp that owns the tree takes it on its next frame. The
    * same shape the add-METHOD dialog uses.
    *
    * @param projectPath Set when a project is wanted
    * @return The path, or nullptr when nothing was asked for
    */
   const std::string* consumeOpenProjectRequest(std::string& projectPath);

   /**
    * @brief Ask for a project to be opened
    *
    * The counterpart of requestOpenFile, and the same crossing: the recent list and
    * the keyboard shortcut already reach the Workspace this way, and a project
    * named on the command line or dropped as a folder belongs with them.
    *
    * @param projectPath The project directory, or its .undoProject folder
    */
   void requestOpenProject(const std::string& projectPath);

   /**
    * @brief Ask for a file to be opened in the editor
    *
    * Where a path handed to the IDE on the command line, or dropped onto the
    * window, is left until an undoApp takes it. Both of those arrive here and both
    * are the core, while the editor that opens them belongs to an undoApp, so the
    * request crosses the same way the recent-projects one does.
    *
    * A file handed over while another is already open replaces it, which is what
    * opening a file means everywhere else; the queue below holds one because a
    * drop can carry several and they are opened in the order they were given,
    * leaving the last one as the one on screen.
    *
    * @param filePath The file to open, as the user wrote it
    */
   void requestOpenFile(const std::string& filePath);

   /**
    * @brief Take the next file that was asked to be opened
    * @param filePath Set to the path, valid until the next call
    * @return The path, or nullptr when nothing was asked for
    */
   const std::string* consumeOpenFileRequest(std::string& filePath);

   /**
    * @brief Copy the layout shipped in resources/ into place when there is none
    *
    * A fresh install opens on the arrangement the project ships rather than on the
    * one createDefaultLayout() builds, so the two cannot drift apart.
    */
   void adoptShippedLayout();

   /**
    * @brief Get the ImGui IO structure
    * @return Reference to ImGuiIO
    */
   ImGuiIO& getIO();

private:
   ImGuiManager() = default;
   ~ImGuiManager() = default;
   ImGuiManager(const ImGuiManager&) = delete;
   ImGuiManager& operator=(const ImGuiManager&) = delete;

   /**
    * @brief Render the docking layout
    * 
    * This method renders the main docking space and manages
    * the layout of docked panels.
    */
   void renderDockingLayout();

   /**
    * @brief Render the main menu bar
    */
   void renderMenuBar();

   /**
    * @brief Create the default docking layout
    */
   void createDefaultLayout();

   /**
    * @brief Load an image as an OpenGL texture for ImGui
    * @param path Path to the image file (PNG recommended)
    * @return ImTextureID (GLuint) or 0 if failed
    */
   ImTextureID loadImageTexture(const std::string& path);

   /**
    * @brief Panel structure
    */
   struct Panel
   {
      std::string name;           ///< Panel unique name
      PanelRenderFunc renderFunc; ///< Panel rendering function
      bool visible = true;        ///< Panel visibility flag
      bool open = true;           ///< Panel open state
   };

   std::vector<Panel> m_panels;
   bool m_dockingEnabled = true;
   bool m_layoutInitialized = false;
   bool m_layoutResetRequested = false;
   ImGuiID m_dockspaceID = 0;
   std::string m_iniFilename = "undoStudio_layout.ini"; ///< Persistent storage for ImGuiIO::IniFilename
   ImTextureID m_logoTexture = 0;
   bool m_showAboutPopup = false; ///< True once Help > About undoStudio was asked for
bool m_showRecentsPopup = false; ///< True while the recent projects list is open
    std::string m_pendingOpenProject;   ///< Set by the menu, taken by the undoApp
    /// True while the recent files list is open, and cleared with it
    bool m_showRecentFilesPopup = false;
   /// Files asked to be opened, oldest first, by the command line or by a drop.
   std::deque<std::string> m_pendingOpenFiles;
   /// The last display geometry written to the log, so only a change is written
   std::string m_lastDisplayReport;
};

} // namespace ui
} // namespace undoStudio