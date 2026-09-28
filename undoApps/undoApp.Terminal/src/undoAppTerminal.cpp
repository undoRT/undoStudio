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

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>

namespace undoApp {
namespace Terminal {

namespace {

/// @brief Name the panel is registered under
constexpr const char* kPanelName = "undoApp Terminal";

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
   loadFontSize();
   // The font is loaded here rather than on the first frame that draws the
   // terminal: an atlas built in the middle of a frame is not on the GPU until
   // the next one, and the first thing the user would see is a grid of nothing.
   ensureFont();
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
   // Loaded without a size: the terminal picks the size it wants, and ImGui bakes
   // the font at each one that is asked for. Baking it at a fixed size here would
   // make the first size the only one available.
   m_font = ImGui::GetIO().Fonts->AddFontFromFileTTF("resources/fonts/DejaVuSansMono.ttf", 0.0f);
   if (m_font == nullptr) {
      // Said out loud in the pane, not only on the console: without this font the
      // grid is drawn with Roboto, which is proportional, and every column is a
      // guess. The message is the difference between a terminal that looks odd on
      // purpose and one that looks broken.
      m_fontMissing = true;
      m_font = ImGui::GetFont();
      std::cerr << "[undoApp.Terminal] resources/fonts/DejaVuSansMono.ttf not found: "
                   "run the IDE from the repository root" << std::endl;
   } else {
      ImGui::GetIO().Fonts->Build();
      // Said on success too: a terminal drawn with the wrong font looks like a
      // broken one, and the console is where that gets settled. The size is read
      // off the font rather than measured, since measuring needs a frame and this
      // runs before the first one.
      std::cout << "[undoApp.Terminal] monospace font loaded, starting at " << m_fontSize << " px" << std::endl;
   }
}

void TerminalApp::loadFontSize()
{
   if (m_fontSizeLoaded) {
      return;
   }
   m_fontSizeLoaded = true;
   // The size is a preference, so it is remembered: a terminal the user has made
   // larger should still be larger tomorrow. A missing or unreadable file is not
   // a problem, it only means the default stands.
   std::ifstream saved(kSettingsFile);
   std::string line;
   while (std::getline(saved, line)) {
      const size_t equals = line.find('=');
      if (equals == std::string::npos || line.compare(0, 9, "fontsize=") != 0) {
         continue;
      }
      try {
         const float size = std::stof(line.substr(equals + 1));
         m_fontSize = std::max(kMinFontSize, std::min(kMaxFontSize, size));
      } catch (const std::exception&) {
         // A value that is not a number leaves the default in place.
      }
   }
}

void TerminalApp::saveFontSize() const
{
   std::ofstream out(kSettingsFile);
   if (!out.good()) {
      return; // nowhere to keep it, which costs the preference and nothing else
   }
   out << "# undoApp.Terminal preferences, written by undoStudio\n"
       << "fontsize=" << m_fontSize << "\n";
}

void TerminalApp::setFontSize(float size)
{
   const float clamped = std::max(kMinFontSize, std::min(kMaxFontSize, size));
   if (clamped == m_fontSize) {
      return;
   }
   m_fontSize = clamped;
   saveFontSize();
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
      tab.view->handleInput(*tab.session, ImGui::IsWindowFocused(), m_fontSize, kDefaultFontSize);
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

      // The size, in the open. Ctrl and the wheel do the same thing and neither is
      // discoverable, and a terminal sized by a shortcut nobody can find is a
      // terminal people stop reading.
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - 84.0f);
      if (ImGui::SmallButton("A-")) {
         setFontSize(m_fontSize - 1.0f);
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Smaller text");
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("A+")) {
         setFontSize(m_fontSize + 1.0f);
      }
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("Larger text");
      }
      ImGui::SameLine();
      ImGui::Text("%.0f px", m_fontSize);
   }
}

void TerminalApp::render()
{
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

            // The view changes the size for Ctrl and the wheel, and for Ctrl with
            // the plus, the minus or a zero. Whatever it settled on is clamped and
            // written out here, so a size set with the keyboard is remembered like
            // one set with the buttons, and the view needs to know nothing about
            // preferences existing.
            const float before = m_fontSize;
            tab.view->draw(*tab.session, m_font, m_fontSize, ImGui::IsWindowFocused());
            handleInput(tab);
            if (m_fontSize != before) {
               setFontSize(m_fontSize);
            }
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
