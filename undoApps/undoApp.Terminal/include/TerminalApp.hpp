/**
 * @file TerminalApp.hpp
 * @brief The terminal undoApp: the panel, and the sessions it holds
 * @ingroup undoapps
 *
 * One panel, one session per tab, each with its own shell. A session is started
 * the first time its tab is shown, in the project root when a project is open and
 * in the current directory otherwise, so a shell opened from the IDE is already
 * where the work is.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "TerminalSession.hpp"
#include "TerminalView.hpp"

struct ImFont;

#include <memory>
#include <string>
#include <vector>

namespace undoApp {
namespace Terminal {

/// @brief One tab: a shell, and the view that draws it
struct Tab
{
   std::unique_ptr<TerminalSession> session;
   std::unique_ptr<TerminalView> view;
   std::string title = "Terminal";
};

/**
 * @brief The terminal undoApp
 */
class TerminalApp
{
public:
   /// @brief The one instance the plugin loader is handed
   static TerminalApp& getInstance();

   /// @brief Register the panel
   /// @return True when it is registered
   bool initialize();

   /// @brief Remove the panel and end every shell
   void shutdown();

   /// @brief Open one more tab
   void newTab();

   /// @brief Close a tab, ending its shell
   /// @param index Tab to close
   void closeTab(int index);

   /// @brief Rename the active tab
   /// @param name New title
   void renameActive(const std::string& name);

   /// @brief Draw the panel
   void render();

   int tabCount() const { return static_cast<int>(m_tabs.size()); }

   /// @brief Point size the terminal starts at, and what Ctrl+0 returns to
   static constexpr float kDefaultFontSize = 16.0f;

   /// @brief Where the chosen size is kept between runs
   static constexpr const char* kSettingsFile = "undoApp.Terminal.ini";

   /// @brief Smallest and largest the size may be set to
   static constexpr float kMinFontSize = 9.0f;
   static constexpr float kMaxFontSize = 40.0f;

private:
   TerminalApp() = default;

   void ensureTab();
   void startSession(Tab& tab);
   void handleInput(Tab& tab);
   void drawTabs();
   void ensureFont();
   void loadFontSize();
   void saveFontSize() const;
   void setFontSize(float size);

   ImFont* m_font = nullptr;   ///< Monospace font, or the default when it is missing
   float m_fontSize = kDefaultFontSize; ///< Size the terminal is drawn at
   bool m_fontSizeLoaded = false;      ///< The saved size has been read already

   std::vector<Tab> m_tabs;
   int m_active = 0;
   bool m_panelRegistered = false;
   bool m_fontTried = false;
   bool m_fontMissing = false; ///< The monospace font was not found
};

} // namespace Terminal
} // namespace undoApp
