/**
 * @file ImGuiManager.cpp
 * @brief Implementation of the ImGuiManager class
 * @ingroup ui
 * 
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoStudio/ui/ImGuiManager.hpp"
#include "undoStudio/core/ProjectManager.hpp"
#include "undoStudio/core/RecentFiles.hpp"
#include "undoStudio/services/WindowService.hpp"
#include "version.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#include <GLFW/glfw3.h>

#include <stb_image.h>
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace undoStudio {
namespace ui {

namespace fs = std::filesystem;

// ============================================================================
// Singleton Instance
// ============================================================================

ImGuiManager& ImGuiManager::getInstance()
{
   static ImGuiManager instance;
   return instance;
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

bool ImGuiManager::initialize(void* window)
{
   std::cout << "[ImGui] Initializing..." << std::endl;

   try {
      IMGUI_CHECKVERSION();
      ImGui::CreateContext();
      ImPlot::CreateContext();

      ImGuiIO& io = ImGui::GetIO();
      io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
      io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
      io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

// Enable .ini file for layout persistence
    io.IniFilename = m_iniFilename.c_str();

      setDarkTheme();

      if (!ImGui_ImplGlfw_InitForOpenGL(static_cast<GLFWwindow*>(window), true)) {
         std::cerr << "[ImGui] Failed to initialize GLFW backend" << std::endl;
         return false;
      }

      const char* glslVersion = "#version 330 core";
      if (!ImGui_ImplOpenGL3_Init(glslVersion)) {
         std::cerr << "[ImGui] Failed to initialize OpenGL backend" << std::endl;
         ImGui_ImplGlfw_Shutdown();
         return false;
      }

      std::cout << "[ImGui] Initialization successful" << std::endl;
      return true;

   } catch (const std::exception& e) {
      std::cerr << "[ImGui] ERROR during initialization: " << e.what() << std::endl;
      return false;
   }
}

void ImGuiManager::shutdown()
{
   std::cout << "[ImGui] Shutting down..." << std::endl;

   ImGui_ImplOpenGL3_Shutdown();
   ImGui_ImplGlfw_Shutdown();
   ImPlot::DestroyContext();
   ImGui::DestroyContext();

   std::cout << "[ImGui] Shutdown complete" << std::endl;
}

// ============================================================================
// Frame Management
// ============================================================================

void ImGuiManager::newFrame()
{
   ImGui_ImplOpenGL3_NewFrame();
   ImGui_ImplGlfw_NewFrame();
   ImGui::NewFrame();
}

void ImGuiManager::render()
{
   // Render menu bar
   renderMenuBar();

   // Render docking layout first
   if (m_dockingEnabled) {
      renderDockingLayout();
   }

   // Render all visible panels
   for (auto& panel : m_panels) {
      if (panel.visible && panel.renderFunc) {
         ImGuiWindowFlags flags = ImGuiWindowFlags_None;
         if (m_dockingEnabled) {
            flags |= ImGuiWindowFlags_NoCollapse;
         }

         if (ImGui::Begin(panel.name.c_str(), &panel.open, flags)) {
            panel.renderFunc();
         }
          ImGui::End();

          // If panel was closed, hide it
          if (!panel.open) {
             panel.visible = false;
          }
       }
    }
}

void ImGuiManager::endFrame()
{
   ImGui::Render();
   ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
       GLFWwindow* backupContext = glfwGetCurrentContext();
       ImGui::UpdatePlatformWindows();
       ImGui::RenderPlatformWindowsDefault();
       glfwMakeContextCurrent(backupContext);
    }
}

// ============================================================================
// Docking Layout
// ============================================================================

void ImGuiManager::renderDockingLayout()
{
   static bool dockspaceOpen = true;

   ImGuiViewport* viewport = ImGui::GetMainViewport();
   ImGui::SetNextWindowPos(viewport->WorkPos);
   ImGui::SetNextWindowSize(viewport->WorkSize);
   ImGui::SetNextWindowViewport(viewport->ID);

   ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse
                                  | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus
                                  | ImGuiWindowFlags_NoNavFocus;

   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

   ImGui::Begin("DockSpace", &dockspaceOpen, windowFlags);
   ImGui::PopStyleVar(3);

   m_dockspaceID = ImGui::GetID("MyDockSpace");
   ImGui::DockSpace(m_dockspaceID, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

   // Create default layout ONLY on first run (or after an explicit reset) and
   // ONLY if no layout file exists. DO NOT call DockBuilder during drag
   // operations - it causes conflicts.
   if (!m_layoutInitialized || m_layoutResetRequested) {
      bool hasSavedLayout = false;
      if (!m_layoutResetRequested) {
         hasSavedLayout = !m_iniFilename.empty() && std::ifstream(m_iniFilename).good();
         // Nothing arranged yet: take the layout the project ships, so a fresh
         // install opens the way it is meant to be rather than on the DockBuilder
         // arrangement in createDefaultLayout(). An explicit reset does not, or
         // there would be no way back from it.
         if (!hasSavedLayout) {
            adoptShippedLayout();
            hasSavedLayout = !m_iniFilename.empty() && std::ifstream(m_iniFilename).good();
         }
      }

      if (!hasSavedLayout) {
         std::cout << "[ImGui] No saved layout found, creating default..." << std::endl;
         createDefaultLayout();
      } else {
         std::cout << "[ImGui] Using saved layout from: " << m_iniFilename << std::endl;
      }

      m_layoutInitialized = true;
      m_layoutResetRequested = false;
   }

   ImGui::End();
}

void ImGuiManager::createDefaultLayout()
{
   // IMPORTANT: Only call this when absolutely needed (first run or reset)
   // Never call this during drag operations

   ImGui::DockBuilderRemoveNode(m_dockspaceID);
   ImGui::DockBuilderAddNode(m_dockspaceID, ImGuiDockNodeFlags_DockSpace);

   ImGuiViewport* viewport = ImGui::GetMainViewport();
   ImGui::DockBuilderSetNodeSize(m_dockspaceID, viewport->WorkSize);

   if (m_panels.empty()) {
      // Nothing registered yet: leave a single empty dockspace rather than
      // pre-assigning docks for windows that don't exist. Docking a name
      // that never calls ImGui::Begin() leaves an orphaned/empty dock node,
      // and dragging tabs around an orphaned node is what corrupts the
      // layout at runtime.
      ImGui::DockBuilderFinish(m_dockspaceID);
      return;
   }

   // Split in two columns: left (30%) and right (70%)
   ImGuiID dockLeft, dockRight;
   ImGui::DockBuilderSplitNode(m_dockspaceID, ImGuiDir_Left, 0.30f, &dockLeft, &dockRight);

   // Split left in two rows: top (50%) and bottom (50%), only if we have
   // more than one panel to put there.
   ImGuiID dockLeftTop = dockLeft;
   ImGuiID dockLeftBottom = dockLeft;
   if (m_panels.size() > 1) {
      ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Up, 0.50f, &dockLeftTop, &dockLeftBottom);
   }

   // Assign panels to docks dynamically: only ever reference names of
   // panels that are actually registered right now. The first panel goes
   // top-left, the second bottom-left, everything else goes to the right
   // column (as tabs).
   for (std::size_t i = 0; i < m_panels.size(); ++i) {
      ImGuiID target = dockRight;
      if (i == 0) {
         target = dockLeftTop;
      } else if (i == 1) {
         target = dockLeftBottom;
      }
      ImGui::DockBuilderDockWindow(m_panels[i].name.c_str(), target);
   }

   ImGui::DockBuilderFinish(m_dockspaceID);

   // Make all panels visible
   for (auto& panel : m_panels) {
      panel.visible = true;
      panel.open = true;
   }
}

ImTextureID ImGuiManager::loadImageTexture(const std::string& path)
{
   int width, height, channels;
   unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4); // forza 4 canali RGBA
   if (!data) {
      std::cerr << "[ImGui] Failed to load image: " << path << std::endl;
      return 0;
   }

   GLuint texture;
   glGenTextures(1, &texture);
   glBindTexture(GL_TEXTURE_2D, texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
   glBindTexture(GL_TEXTURE_2D, 0);

   stbi_image_free(data);

   std::cout << "[ImGui] Loaded texture: " << path << " (" << width << "x" << height << ")" << std::endl;
   return texture;
}

// ============================================================================
// Menu Bar
// ============================================================================

void ImGuiManager::renderMenuBar()
{
   // Save the current style
   ImGuiStyle& style = ImGui::GetStyle();
   ImVec2 oldFramePadding = style.FramePadding;
   ImVec2 oldItemSpacing = style.ItemSpacing;
   ImVec2 oldWindowPadding = style.WindowPadding;

   // APply bigger styles for the menu bar
   style.FramePadding = ImVec2(12.0f, 10.0f);
   style.ItemSpacing = ImVec2(14.0f, 12.0f);
   style.WindowPadding = ImVec2(16.0f, 10.0f);

   if (ImGui::BeginMainMenuBar()) {
      // Load logo if present
      if (m_logoTexture == 0) {
         m_logoTexture = loadImageTexture("resources/icons/undoRT_logo.png");
      }
      // Show logo if present
      if (m_logoTexture) {
         // Fixed dimension
         float logoHeight = 36.0f;
         float logoWidth = logoHeight;
         ImGui::Image(m_logoTexture, ImVec2(logoWidth, logoHeight));
         ImGui::SameLine(0.0f, 20.0f); // spaziatura dopo il logo
      }

      // Ctrl and R: the recent projects, the way an editor is expected to. Held
      // off while anything owns the keyboard, because inside a code editor or a
      // terminal the same two keys mean something else entirely, and stealing them
      // would break the program the user is in.
      ImGuiIO& io = ImGui::GetIO();
      if (io.KeyCtrl && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_R) && !io.WantTextInput) {
         openRecentProjects();
      }
      // Ctrl+P, which is what reaches for a file in VS Code. Ctrl+R is taken by the
      // recent projects, so the file list could not have had it, and the two are
      // worth telling apart on the keyboard as well as in the menu.
      if (io.KeyCtrl && !io.KeyAlt && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_P) && !io.WantTextInput) {
         openRecentFiles();
      }

      if (ImGui::BeginMenu("File")) {
         if (ImGui::MenuItem("Open Recent Projects\tCtrl+R")) {
            openRecentProjects();
         }
         if (ImGui::MenuItem("Open Recent Files\tCtrl+P")) {
            openRecentFiles();
         }
         ImGui::EndMenu();
      }

      if (m_showRecentsPopup) {
         renderRecentProjects();
      }
      if (m_showRecentFilesPopup) {
         renderRecentFiles();
      }

      // Menu
      if (ImGui::BeginMenu("View")) {
         if (ImGui::MenuItem("Reset Layout")) {
            resetLayout();
         }
         ImGui::Separator();

         // Show/hide panels
         for (auto& panel : m_panels) {
            bool visible = panel.visible;
            if (ImGui::MenuItem(panel.name.c_str(), NULL, &visible)) {
               showPanel(panel.name, visible);
            }
         }

         ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Help")) {
         if (ImGui::MenuItem("About undoStudio")) {
            m_showAboutPopup = true;
            // The menu closes on this very click.
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndMenu();
      }

      // A plain popup, not a modal: ImGui closes one of these with Escape and with
      // a click anywhere else, without the box having to offer a way out itself.
      // A modal here would be closable only by its own button, and Escape does not
      // reach it.
      //
      // The ask is spent here, the moment it is passed on. ImGui takes the
      // dismissal before this code runs and takes it off the open stack, so a flag
      // left standing would ask again in the very same frame and the box would
      // never be gone: closing it and reopening it is what reads as flickering, and
      // there is no way to tell such a popup apart from one that cannot be closed.
      if (m_showAboutPopup) {
         ImGui::OpenPopup("About undoStudio");
         m_showAboutPopup = false;
      }
      if (ImGui::BeginPopup("About undoStudio", ImGuiWindowFlags_AlwaysAutoResize)) {
         ImGui::TextUnformatted("undoStudio v" STUDIO_VERSION_STRING);
         ImGui::TextDisabled("Industrial Automation IDE for Structured Text");
         ImGui::Separator();
         ImGui::Text("Version: %s", STUDIO_VERSION_STRING);
         ImGui::Text("Build: %s", STUDIO_BUILD_DATE);
         ImGui::Text("Compiler: %s", STUDIO_COMPILER);
         ImGui::Separator();
         ImGui::TextUnformatted("Copyright (c) 2025-2026 undoRT");
         ImGui::TextUnformatted("All rights reserved.");
         ImGui::Text("Author: Salvatore Bamundo");
         ImGui::TextDisabled("Licence: GPL-3.0-or-later");
         ImGui::Separator();
         ImGui::TextDisabled("Built with ImGui, ImPlot, GLFW, st2cpp");
         if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      } else {
         // A plain popup goes away on Escape and on a click outside it, without
         // going through the button above. Clearing the ask here rather than in the
         // button is what lets it: a flag left standing after a dismissal asks
         // again on the next frame, and the popup cannot be closed.
         m_showAboutPopup = false;
      }

      ImGui::EndMainMenuBar();
   }

   // Restore original style
   style.FramePadding = oldFramePadding;
   style.ItemSpacing = oldItemSpacing;
   style.WindowPadding = oldWindowPadding;
}

// ============================================================================
// Panel Management
// ============================================================================

void ImGuiManager::addPanel(const std::string& name, PanelRenderFunc renderFunc)
{
   // Check if panel already exists
   for (auto& panel : m_panels) {
      if (panel.name == name) {
         panel.renderFunc = renderFunc;
         panel.visible = true;
         panel.open = true;
         return;
      }
   }

   m_panels.push_back({name, renderFunc, true, true});
   std::cout << "[ImGui] Panel added: " << name << std::endl;
}

void ImGuiManager::removePanel(const std::string& name)
{
   auto it = std::remove_if(m_panels.begin(), m_panels.end(), [&name](const Panel& p) { return p.name == name; });

   if (it != m_panels.end()) {
      m_panels.erase(it, m_panels.end());
      std::cout << "[ImGui] Panel removed: " << name << std::endl;
   }
}

void ImGuiManager::showPanel(const std::string& name, bool show)
{
   for (auto& panel : m_panels) {
      if (panel.name == name) {
         panel.visible = show;
         if (show) {
            panel.open = true;
         }
         return;
      }
   }
}

bool ImGuiManager::isPanelVisible(const std::string& name) const
{
   for (const auto& panel : m_panels) {
      if (panel.name == name) {
         return panel.visible;
      }
   }
   return false;
}

// ============================================================================
// Layout Control
// ============================================================================

void ImGuiManager::resetLayout()
{
   std::cout << "[ImGui] Resetting layout..." << std::endl;

   // Delete the layout file
   if (!m_iniFilename.empty()) {
      std::remove(m_iniFilename.c_str());
   }

   // Tell renderDockingLayout() to rebuild the default layout on the very
   // next frame, ignoring any (now-deleted) saved layout.
   m_layoutResetRequested = true;

   // Make all panels visible
   for (auto& panel : m_panels) {
      panel.visible = true;
      panel.open = true;
   }
}

void ImGuiManager::saveLayout(const std::string& filename)
{
   // ImGui automatically saves layout to .ini file
   // This is just a wrapper for manual saving if needed
   (void) filename;
   std::cout << "[ImGui] Layout saved to: " << m_iniFilename << std::endl;
}

/**
 * @brief Copy the layout shipped with the project into place, if there is none yet
 *
 * The arrangement the project opens with is a file in resources/, not a builder
 * call: it is the one the dock layout is saved from, so what a new user gets is
 * what the project settled on rather than a second description of it that can
 * drift away from the first.
 */
void ImGuiManager::adoptShippedLayout()
{
   if (m_iniFilename.empty()) {
      return;
   }
   if (std::ifstream(m_iniFilename).good()) {
      return;
   }
   static const char* kShipped = "resources/undoStudio_layout.ini";
   std::ifstream source(kShipped);
   if (!source.good()) {
      return; // nothing to adopt, the DockBuilder arrangement will be used
   }
   std::ofstream target(m_iniFilename);
   if (!target.good()) {
      std::cerr << "[ImGui] Cannot write the shipped layout to " << m_iniFilename << std::endl;
      return;
   }
   target << source.rdbuf();
   std::cout << "[ImGui] Adopted the shipped layout: " << kShipped << " -> " << m_iniFilename << std::endl;
}

void ImGuiManager::openRecentProjects()
{
   // OpenPopup is what puts the list on ImGui's stack of open popups. Setting a
   // flag and drawing it is not enough: BeginPopup looks at that stack and, not
   // finding itself on it, returns false without opening anything, which leaves
   // the matching EndPopup closing a popup that was never there.
   m_showRecentsPopup = true;
   ImGui::OpenPopup(kRecentProjectsPopup);
}

void ImGuiManager::renderRecentProjects()
{
   auto& pm = core::ProjectManager::getInstance();

   ImGui::SetNextWindowPos(ImVec2(200.0f, 80.0f), ImGuiCond_Appearing);
   ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);

   // Given the keyboard on the frame it opens, so the list can be walked with the
   // arrows and chosen with Enter without a mouse. It has to be asked for before
   // the Begin, not after: what is set here is read by the window that follows.
   ImGui::SetNextWindowFocus();
   const bool open = ImGui::BeginPopup(kRecentProjectsPopup, ImGuiWindowFlags_NoSavedSettings);
   if (open) {
      // Copied, not held: forgetting a project rewrites the list the one being
      // walked is a view of, and iterating that view while it shrinks is the kind
      // of bug that reads as a random crash rather than as this.
      const std::vector<std::string> recent = pm.recentProjects();
      if (recent.empty()) {
         ImGui::TextDisabled("No project has been opened yet.");
      } else {
         for (const std::string& path : recent) {
            const std::string name = fs::path(path).filename().string();
            const bool onDisk = fs::exists(path);

            // The path is pushed as the id around everything on the row, and stayed
            // pushed past the button to the name beside it: the name is not unique —
            // two projects can be called "main", in two different folders — and a row
            // whose id is only its name is one widget drawn twice. The second one
            // never becomes hovered, so its name cannot be clicked and only the first
            // project of that name can be opened.
            ImGui::PushID(path.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.45f, 0.45f, 1.0f));
            if (ImGui::SmallButton("x")) {
               pm.forgetProject(path);
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (!onDisk) {
               // Said here rather than only when the entry is picked, because a
               // name in a list that does nothing is worse than one that says why.
               ImGui::TextDisabled("%s (missing)", name.c_str());
            } else if (ImGui::Selectable(name.c_str())) {
               m_pendingOpenProject = path;
               m_showRecentsPopup = false;
               ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
               ImGui::SetTooltip("%s", path.c_str());
            }
         }
         ImGui::Separator();

         // The limit, edited where the list it governs is. There is no settings
         // panel to put it in, and a limit reached by opening more than ten
         // projects is a limit you cannot reach deliberately.
         int limit = static_cast<int>(pm.maxRecentProjects());
         ImGui::SetNextItemWidth(80.0f);
         if (ImGui::InputInt("&Remember", &limit, 1, 5)) {
            pm.setMaxRecentProjects(static_cast<size_t>(limit < 0 ? 0 : limit));
            // InputInt leaves the entry showing the value that was clamped away, so
            // it is put back to what was actually stored.
            limit = static_cast<int>(pm.maxRecentProjects());
         }
         if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How many projects to remember (%u to %u)",
                              static_cast<unsigned>(core::ProjectManager::kMinRecentLimit),
                              static_cast<unsigned>(core::ProjectManager::kMaxRecentLimit));
         }

         ImGui::SameLine();
         if (ImGui::Button("Clear the list")) {
            pm.clearRecentProjects();
            m_showRecentsPopup = false;
            ImGui::CloseCurrentPopup();
         }
      }
   }
   if (open) {
      ImGui::EndPopup();
   }
   if (!open) {
      // Esc or a click elsewhere. ImGui closes the popup, and the flag has to
      // follow it or the next frame opens a new one.
      m_showRecentsPopup = false;
   }

   // The project is not opened here. The workspace that shows its tree belongs to
   // an undoApp, and it is the one that has to rebuild that tree, so the pick
   // leaves a request behind for it. A path that is no longer on disk says so
   // rather than leaving a click that appears to do nothing.
   if (!m_pendingOpenProject.empty()) {
      if (!fs::exists(m_pendingOpenProject)) {
         std::cerr << "[ImGui] No such project: " << m_pendingOpenProject << std::endl;
      }
   }
}

void ImGuiManager::openRecentFiles()
{
   // OpenPopup is what puts the list on ImGui's stack of open popups; setting the
   // flag and drawing it is not enough, for the reason given in openRecentProjects.
   m_showRecentFilesPopup = true;
   ImGui::OpenPopup(kRecentFilesPopup);
}

void ImGuiManager::renderRecentFiles()
{
   auto& recents = core::RecentFiles::getInstance();

   ImGui::SetNextWindowPos(ImVec2(200.0f, 80.0f), ImGuiCond_Appearing);
   ImGui::SetNextWindowSize(ImVec2(620.0f, 0.0f), ImGuiCond_Appearing);

   // Given the keyboard on the frame it opens, so the list can be walked with the
   // arrows and chosen with Enter. Asked for before the Begin: what is set here is
   // read by the window that follows.
   ImGui::SetNextWindowFocus();
   const bool open = ImGui::BeginPopup(kRecentFilesPopup, ImGuiWindowFlags_NoSavedSettings);
   if (open) {
      // Copied rather than held: forgetting a file rewrites the list this one is a
      // view of, and walking a list while it shrinks is a crash that reads as random.
      const std::vector<std::string> recent = recents.recentFiles();
      if (recent.empty()) {
         ImGui::TextDisabled("No file has been opened yet.");
      } else {
         for (const std::string& path : recent) {
            const fs::path file(path);
            const std::string name = file.filename().string();
            const bool onDisk = fs::exists(path);

            // The path is pushed as the id around everything on the row, and stayed
            // pushed past the button to the name beside it: the name is not unique —
            // two files can be called undoFB.st in two different projects — and a row
            // whose id is only its name is one widget drawn twice. The second one never
            // becomes hovered, so its name cannot be clicked and only the first file of
            // that name can be opened.
            ImGui::PushID(path.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.8f, 0.35f, 0.35f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.9f, 0.45f, 0.45f, 1.0f));
            if (ImGui::SmallButton("x")) {
               recents.forgetFile(path);
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine();
            if (!onDisk) {
               // Said here rather than only when picked: a name in a list that does
               // nothing is worse than one that says why it does nothing.
               ImGui::TextDisabled("%s (missing)", name.c_str());
            } else if (ImGui::Selectable(name.c_str())) {
               // The pick is a request, not an open. The editor that shows a file
               // belongs to an undoApp and the core asks rather than calls, which is
               // the same crossing the recent projects list uses.
               requestOpenFile(path);
               m_showRecentFilesPopup = false;
               ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
            if (ImGui::IsItemHovered()) {
               ImGui::SetTooltip("%s", path.c_str());
            }
         }
         ImGui::Separator();

         // The limit, edited where the list it governs is. Shared with the project
         // list on purpose: it is one "remember N" setting, not two.
         int limit = static_cast<int>(recents.maxRecentFiles());
         ImGui::SetNextItemWidth(80.0f);
         if (ImGui::InputInt("&Remember", &limit, 1, 5)) {
            recents.setMaxRecentFiles(static_cast<size_t>(limit < 0 ? 0 : limit));
            // InputInt leaves the entry showing the value that was clamped away, so
            // it is put back to what was actually stored.
            limit = static_cast<int>(recents.maxRecentFiles());
         }
         if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("How many files to remember (%u to %u)",
                              static_cast<unsigned>(core::RecentFiles::kMinRecentLimit),
                              static_cast<unsigned>(core::RecentFiles::kMaxRecentLimit));
         }

         ImGui::SameLine();
         if (ImGui::Button("Clear the list")) {
            recents.clearRecentFiles();
            m_showRecentFilesPopup = false;
            ImGui::CloseCurrentPopup();
         }
      }
   }
   if (open) {
      ImGui::EndPopup();
   }
   if (!open) {
      // Esc or a click elsewhere: ImGui has closed the popup and the flag has to
      // follow it, or the next frame opens a new one.
      m_showRecentFilesPopup = false;
   }
}

const std::string* ImGuiManager::consumeOpenProjectRequest(std::string& projectPath)
{
   if (m_pendingOpenProject.empty()) {
      return nullptr;
   }
   projectPath = m_pendingOpenProject;
   m_pendingOpenProject.clear();
   return &projectPath;
}

void ImGuiManager::requestOpenProject(const std::string& projectPath)
{
   if (projectPath.empty()) {
      return;
   }
   m_pendingOpenProject = projectPath;
}

void ImGuiManager::requestOpenFile(const std::string& filePath)
{
   if (filePath.empty()) {
      return;
   }
   // Bounded on purpose: a drop of a whole directory from a file manager, or a
   // command line built by a shell glob, can name hundreds of files, and holding
   // them all until an undoApp gets round to them is a way to use a lot of memory
   // for no gain. The ones beyond this are dropped rather than queued behind the
   // ones that came first.
   constexpr size_t kMaxPendingFiles = 64;
   if (m_pendingOpenFiles.size() >= kMaxPendingFiles) {
      std::cerr << "[ImGui] More files than can be opened at once, ignoring " << filePath << std::endl;
      return;
   }
   m_pendingOpenFiles.push_back(filePath);
}

const std::string* ImGuiManager::consumeOpenFileRequest(std::string& filePath)
{
   if (m_pendingOpenFiles.empty()) {
      return nullptr;
   }
   filePath = m_pendingOpenFiles.front();
   m_pendingOpenFiles.pop_front();
   return &filePath;
}

void ImGuiManager::loadLayout(const std::string& filename)
{
    // Store the filename ourselves before handing a pointer to ImGui: io.IniFilename
    // must stay valid for the lifetime of the context, and the `filename`
    // parameter (or a temporary, if called with the default argument) does not.
    m_iniFilename = filename;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = m_iniFilename.c_str();

   // ImGui only auto-loads the ini file once, right before the first frame
   // after context creation. Loading a *different* file at runtime needs an
   // explicit reload; this does not touch the dockspace, so no need to go
   // through createDefaultLayout().
   ImGui::LoadIniSettingsFromDisk(m_iniFilename.c_str());

   std::cout << "[ImGui] Layout loaded from: " << m_iniFilename << std::endl;
}

// ============================================================================
// Theme Management
// ============================================================================

void ImGuiManager::setUndoRTTheme()
{
   // Colori presi da undoRT.css / undoRT-home.css
   // --bg-main: #030712
   // --bg-card: #0b1329
   // --border: #1c2541
   // --cyan: #00b4d8
   // --lavender: #b0a8cc
   // --text: #f3f4f6
   // --text-muted: #9ca3af
   // --green: #34d399

   ImGuiStyle& style = ImGui::GetStyle();

   // Colori di base
   style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.96f, 1.00f);         // #f3f4f6
   style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.61f, 0.64f, 0.69f, 1.00f); // #9ca3af
   style.Colors[ImGuiCol_WindowBg] = ImVec4(0.01f, 0.03f, 0.07f, 1.00f);     // #030712
   style.Colors[ImGuiCol_ChildBg] = ImVec4(0.04f, 0.08f, 0.16f, 1.00f);      // #0b1329
   style.Colors[ImGuiCol_PopupBg] = ImVec4(0.04f, 0.08f, 0.16f, 1.00f);      // #0b1329
   style.Colors[ImGuiCol_Border] = ImVec4(0.11f, 0.15f, 0.25f, 1.00f);       // #1c2541
   style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
   style.Colors[ImGuiCol_FrameBg] = ImVec4(0.04f, 0.08f, 0.16f, 1.00f); // #0b1329
   style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.07f, 0.12f, 0.22f, 1.00f);
   style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.09f, 0.15f, 0.28f, 1.00f);
   style.Colors[ImGuiCol_TitleBg] = ImVec4(0.01f, 0.03f, 0.07f, 1.00f); // #030712
   style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.01f, 0.03f, 0.07f, 1.00f);
   style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.01f, 0.03f, 0.07f, 1.00f);
   style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.01f, 0.03f, 0.07f, 1.00f);     // #030712
   style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.08f, 0.16f, 1.00f);   // #0b1329
   style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.11f, 0.15f, 0.25f, 1.00f); // #1c2541
   style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.15f, 0.20f, 0.32f, 1.00f);
   style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.20f, 0.25f, 0.40f, 1.00f);
   style.Colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 0.71f, 0.85f, 1.00f); // #00b4d8
   style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 0.71f, 0.85f, 1.00f);
   style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 0.80f, 0.95f, 1.00f);
   style.Colors[ImGuiCol_Button] = ImVec4(0.04f, 0.08f, 0.16f, 1.00f); // #0b1329
   style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.07f, 0.12f, 0.22f, 1.00f);
   style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.09f, 0.15f, 0.28f, 1.00f);
   style.Colors[ImGuiCol_Header] = ImVec4(0.04f, 0.08f, 0.16f, 1.00f);
   style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.07f, 0.12f, 0.22f, 1.00f);
   style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.09f, 0.15f, 0.28f, 1.00f);
   style.Colors[ImGuiCol_Separator] = ImVec4(0.11f, 0.15f, 0.25f, 1.00f);
   style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.15f, 0.20f, 0.32f, 1.00f);
   style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.20f, 0.25f, 0.40f, 1.00f);
   style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.11f, 0.15f, 0.25f, 1.00f);
   style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.15f, 0.20f, 0.32f, 1.00f);
   style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.20f, 0.25f, 0.40f, 1.00f);
   style.Colors[ImGuiCol_Tab] = ImVec4(0.04f, 0.08f, 0.16f, 1.00f);
   style.Colors[ImGuiCol_TabHovered] = ImVec4(0.07f, 0.12f, 0.22f, 1.00f);
   style.Colors[ImGuiCol_TabActive] = ImVec4(0.09f, 0.15f, 0.28f, 1.00f);
   style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.03f, 0.06f, 0.12f, 1.00f);
   style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.06f, 0.10f, 0.20f, 1.00f);
   style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.00f, 0.71f, 0.85f, 0.50f); // cyan con trasparenza
   style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.01f, 0.03f, 0.07f, 1.00f);
   style.Colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 0.71f, 0.85f, 1.00f);
   style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.69f, 0.66f, 0.80f, 1.00f); // #b0a8cc
   style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.00f, 0.71f, 0.85f, 1.00f);
   style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.69f, 0.66f, 0.80f, 1.00f);
   style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.04f, 0.08f, 0.16f, 1.00f);
   style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.11f, 0.15f, 0.25f, 1.00f);
   style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.08f, 0.11f, 0.18f, 1.00f);
   style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.01f, 0.03f, 0.07f, 1.00f);
   style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.04f, 0.08f, 0.16f, 1.00f);
   style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 0.71f, 0.85f, 0.35f);
   style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.00f, 0.71f, 0.85f, 0.50f);
   style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.00f, 0.71f, 0.85f, 0.50f);
   style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.00f, 0.71f, 0.85f, 0.50f);
   style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.01f, 0.03f, 0.07f, 0.50f);
   style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.01f, 0.03f, 0.07f, 0.60f);

   // Stili aggiuntivi (arrotondamenti, padding, ecc.) – possiamo mantenere quelli della dark theme
   style.WindowRounding = 4.0f;
   style.FrameRounding = 4.0f;
   style.ScrollbarRounding = 4.0f;
   style.GrabRounding = 4.0f;
   style.TabRounding = 4.0f;
   style.WindowPadding = ImVec2(8.0f, 8.0f);
   style.FramePadding = ImVec2(6.0f, 4.0f);
   style.ItemSpacing = ImVec2(8.0f, 4.0f);
   style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
   style.IndentSpacing = 21.0f;
   style.ScrollbarSize = 14.0f;

   std::cout << "[ImGui] undoRT theme applied" << std::endl;
}

void ImGuiManager::setDarkTheme()
{
   ImGui::StyleColorsDark();
   auto& style = ImGui::GetStyle();

   style.WindowRounding = 4.0f;
   style.FrameRounding = 4.0f;
   style.ScrollbarRounding = 4.0f;
   style.GrabRounding = 4.0f;
   style.TabRounding = 4.0f;
   style.WindowPadding = ImVec2(8.0f, 8.0f);
   style.FramePadding = ImVec2(6.0f, 4.0f);
   style.ItemSpacing = ImVec2(8.0f, 4.0f);
   style.ItemInnerSpacing = ImVec2(4.0f, 4.0f);
   style.IndentSpacing = 21.0f;
   style.ScrollbarSize = 14.0f;

   style.Colors[ImGuiCol_WindowBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
   style.Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
   style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
   style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);

   std::cout << "[ImGui] Dark theme applied" << std::endl;
}

void ImGuiManager::setLightTheme()
{
   ImGui::StyleColorsLight();
   auto& style = ImGui::GetStyle();

   style.WindowRounding = 4.0f;
   style.FrameRounding = 4.0f;
   style.ScrollbarRounding = 4.0f;
   style.GrabRounding = 4.0f;
   style.TabRounding = 4.0f;

   std::cout << "[ImGui] Light theme applied" << std::endl;
}

void ImGuiManager::setCustomTheme(const std::string& themePath)
{
   std::cout << "[ImGui] Custom theme loading not yet implemented: " << themePath << std::endl;
}

// ============================================================================
// Font Management
// ============================================================================

bool ImGuiManager::loadFont(const std::string& path, float size, const std::string& /*name*/)
{
   ImGuiIO& io = ImGui::GetIO();

   ImFont* font = io.Fonts->AddFontFromFileTTF(path.c_str(), size);
   if (!font) {
      std::cerr << "[ImGui] Failed to load font: " << path << std::endl;
      return false;
   }

   io.FontDefault = font;
   io.Fonts->Build();

   std::cout << "[ImGui] Font loaded: " << path << " (size: " << size << ")" << std::endl;
   return true;
}

// ============================================================================
// Docking Control
// ============================================================================

void ImGuiManager::enableDocking(bool enable)
{
   m_dockingEnabled = enable;
   ImGuiIO& io = ImGui::GetIO();

   if (enable) {
      io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
      std::cout << "[ImGui] Docking enabled" << std::endl;
   } else {
      io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
      std::cout << "[ImGui] Docking disabled" << std::endl;
   }
}

ImGuiIO& ImGuiManager::getIO()
{
   return ImGui::GetIO();
}

} // namespace ui
} // namespace undoStudio