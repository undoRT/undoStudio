/**
 * @file undoAppTerminal.cpp
 * @brief The terminal undoApp: a panel with one shell per tab
 * @ingroup undoapps
 *
 * Registers the panel and owns the tabs. A session is started the first time its
 * tab is drawn, in the project root when a project is open, so the shell a user
 * opens from the IDE is already in the right directory.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "TerminalApp.hpp"

#include "undoStudio/core/ProjectManager.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"

#include <imgui.h>

#include <cstdio>
#include <iostream>

namespace undoApp {
namespace Terminal {

namespace {

/// @brief Name the panel is registered under
constexpr const char* kPanelName = "undoApp Terminal";

/// @brief Point size of the monospace font
constexpr float kFontSize = 15.0f;

/**
 * @brief The directory a new shell should start in
 * @return The open project's root, or an empty string when there is none
 */
std::string projectRoot()
{
   const undoStudio::core::ProjectManager& pm = undoStudio::core::ProjectManager::getInstance();
   return pm.isProjectOpen() ? pm.getProjectPath() : std::string();
}

} // namespace

TerminalApp& TerminalApp::getInstance()
{
   static TerminalApp instance;
   return instance;
}

bool TerminalApp::initialize()
{
   if (m_panelRegistered) {
      return true;
   }
   undoStudio::ui::ImGuiManager::getInstance().addPanel(kPanelName, [this]() { render(); });
   m_panelRegistered = true;
   std::cout << "[undoApp.Terminal] panel registered" << std::endl;
   return true;
}

void TerminalApp::shutdown()
{
   if (m_panelRegistered) {
      undoStudio::ui::ImGuiManager::getInstance().removePanel(kPanelName);
      m_panelRegistered = false;
   }
   m_tabs.clear();
   m_active = 0;
   std::cout << "[undoApp.Terminal] shut down" << std::endl;
}

void TerminalApp::ensureFont()
{
   if (m_fontTried) {
      return;
   }
   m_fontTried = true;
   // A terminal needs a monospace font with the box drawing glyphs in it: drawn
   // with a proportional font, a full-screen program turns into a wall of
   // misplaced rules.
   m_font = ImGui::GetIO().Fonts->AddFontFromFileTTF("resources/fonts/DejaVuSansMono.ttf", kFontSize);
   if (m_font == nullptr) {
      std::cerr << "[undoApp.Terminal] resources/fonts/DejaVuSansMono.ttf not found, "
                   "the terminal will be drawn with the default font" << std::endl;
      m_font = ImGui::GetFont();
   } else {
      ImGui::GetIO().Fonts->Build();
   }
}

void TerminalApp::newTab()
{
   m_tabs.emplace_back();
   m_active = static_cast<int>(m_tabs.size()) - 1;
}

void TerminalApp::closeTab(int index)
{
   if (index < 0 || index >= static_cast<int>(m_tabs.size())) {
      return;
   }
   // The Tab destructor ends the shell: a tab that is gone takes its process
   // with it rather than leaving one behind for every tab ever opened.
   m_tabs.erase(m_tabs.begin() + index);
   m_active = std::max(0, std::min(m_active, static_cast<int>(m_tabs.size()) - 1));
}

void TerminalApp::renameActive(const std::string& name)
{
   if (m_active >= 0 && m_active < static_cast<int>(m_tabs.size()) && !name.empty()) {
      m_tabs[static_cast<size_t>(m_active)].title = name;
   }
}

void TerminalApp::startSession(Tab& tab)
{
   if (tab.session != nullptr) {
      return;
   }
   tab.session = std::make_unique<TerminalSession>(std::string(), projectRoot());
   tab.view = std::make_unique<TerminalView>();
   std::string error;
   if (!tab.session->start(24, 80, error)) {
      std::cerr << "[undoApp.Terminal] cannot start a shell: " << error << std::endl;
   }
}

void TerminalApp::ensureTab()
{
   if (m_tabs.empty()) {
      newTab();
   }
}

void TerminalApp::handleInput(Tab& tab)
{
   if (tab.view != nullptr) {
      tab.view->handleInput(*tab.session, ImGui::IsWindowFocused());
   }
}

void TerminalApp::drawTabs()
{
   // Reordering is a property of the bar. The close button comes from handing
   // BeginTabItem a pointer to close, which is what `open` below is.
   const ImGuiTabBarFlags flags = ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs;
   if (ImGui::BeginTabBar("##terminalTabs", flags)) {
      for (size_t i = 0; i < m_tabs.size(); ++i) {
         bool open = true;
         char label[128];
         std::snprintf(label, sizeof(label), "%s", m_tabs[i].title.c_str());
         if (ImGui::BeginTabItem(label, &open)) {
            m_active = static_cast<int>(i);
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            if (!open) {
               // Asked to close, but the contents are drawn by the caller.
            }
            ImGui::EndTabItem();
         }
         if (!open) {
            closeTab(static_cast<int>(i));
            break;
         }
      }
      if (ImGui::TabItemButton("+")) {
         newTab();
      }
      ImGui::EndTabBar();
   }
}

void TerminalApp::render()
{
   ensureFont();
   ensureTab();

   drawTabs();

   // The terminal area is a child so that it can take the keyboard and the mouse
   // on its own, which is what a pane of a tabbed panel needs.
   const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
   if (ImGui::BeginChild("##terminalContent", ImVec2(0.0f, 0.0f), childFlags)) {
      if (m_active >= 0 && m_active < static_cast<int>(m_tabs.size())) {
         Tab& tab = m_tabs[static_cast<size_t>(m_active)];
         startSession(tab);
         if (tab.session != nullptr && tab.view != nullptr) {
            tab.session->pump();
            tab.view->draw(*tab.session, m_font, ImGui::IsWindowFocused());
            handleInput(tab);
         }
      }
   }
   ImGui::EndChild();
}

} // namespace Terminal
} // namespace undoApp

// The entry points PluginManager looks for.
extern "C" {
void* createUndoApp()
{
   auto& app = undoApp::Terminal::TerminalApp::getInstance();
   app.initialize();
   return &app;
}

void destroyUndoApp(void* app)
{
   if (app != nullptr) {
      static_cast<undoApp::Terminal::TerminalApp*>(app)->shutdown();
   }
}
}
