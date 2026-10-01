/**
 * @file undoAppTerminal.cpp
 * @brief The terminal undoApp: a panel with one shell per tab
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * Registers the panel and owns the tabs. A session is started the first time its
 * tab is drawn, in the project root when a project is open, so the shell a user
 * opens from the IDE is already in the right directory.
 */

#include "TerminalApp.hpp"

#include "undoStudio/core/ProjectManager.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"

#include <imgui.h>

#include <algorithm>
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
   // Reordering is not offered, because the order of the tabs lives in m_tabs and
   // dragging one only reorders ImGui's own array: the bar would move a tab and the
   // panel would keep drawing the one it thinks is there, which is what m_active
   // indexes. A tab moved by hand has to move m_tabs too, or not move at all.
   // The close button comes from handing BeginTabItem a pointer to close, which is
   // what `open` below is.
   const ImGuiTabBarFlags flags = ImGuiTabBarFlags_AutoSelectNewTabs;
   if (ImGui::BeginTabBar("##terminalTabs", flags)) {
      for (size_t i = 0; i < m_tabs.size(); ++i) {
         bool open = true;
         // The title is not an identity. Every tab is titled "Terminal" until the
         // shell renames it, and ImGui derives a tab's ID from its label alone, so
         // without this the second and later tabs are folded into the first: the
         // shells run, and the user cannot see them or close them.
         ImGui::PushID(static_cast<int>(i));
         if (ImGui::BeginTabItem(m_tabs[i].title.c_str(), &open)) {
            m_active = static_cast<int>(i);
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            ImGui::EndTabItem();
         }
         if (!open) {
            closeTab(static_cast<int>(i));
            ImGui::PopID();
            break;
         }
         ImGui::PopID();
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

   // Every session is pumped, not only the one on screen. A shell left in a
   // background tab is a process that is still running: leaving its PTY
   // unread means its output sits in the kernel buffer until the user comes
   // back, and then arrives all at once. The read is non-blocking, so a tab
   // with nothing to say costs one failing read.
   for (Tab& tab : m_tabs) {
      startSession(tab);
      if (tab.session != nullptr) {
         tab.session->pump();
         // An OSC title from the shell is how a renamed tab learns its name, and
         // that can arrive while the tab is in the background.
         const std::string reported = tab.session->title();
         if (!reported.empty() && reported != tab.title) {
            tab.title = reported;
         }
      }
   }

   // The terminal area is a child so that it can take the keyboard and the mouse
   // on its own, which is what a pane of a tabbed panel needs.
   const ImGuiChildFlags childFlags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
   if (ImGui::BeginChild("##terminalContent", ImVec2(0.0f, 0.0f), childFlags)) {
      if (m_active >= 0 && m_active < static_cast<int>(m_tabs.size())) {
         Tab& tab = m_tabs[static_cast<size_t>(m_active)];
         if (tab.session != nullptr && tab.view != nullptr) {

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
