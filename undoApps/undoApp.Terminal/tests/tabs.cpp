/**
 * @file tabs.cpp
 * @brief The tab bar of the terminal panel, driven inside a real ImGui frame without a window
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The bug this covers is that a tab's identity is its label and nothing else, so
// two tabs titled "Terminal" are the same tab to ImGui: the second one is silently
// merged into the first, and a shell the user opened with "+" becomes a shell
// they cannot see or close. Counting the tabs in the bar is what tells the two
// apart.
//
// What is driven here is the tab bar the panel draws, through the panel itself,
// so the assertions are about what a user would see rather than about labels.

#include <imgui.h>
// The tab bar's own storage. How many tabs a bar ended up with, and what it
// identifies them by, is not part of imgui.h: BeginTabItem gives back a bool and
// nothing else. Reaching into it is the only way to assert that the tabs stayed
// distinct, which is the whole point of this test.
#include <imgui_internal.h>

#include <cstdio>
#include <string>
#include <vector>

#define private public
#include "TerminalApp.hpp"
#undef private

using undoApp::Terminal::TerminalApp;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

/**
 * One frame of the panel's tab bar, without a session behind it.
 *
 * The panel is asked to draw only its tabs: a shell is a process, and starting
 * one per tab to count tabs would make a test that cannot fail on a machine with
 * no shell. drawTabs() is what is under test, and it is what decides the
 * identities.
 */
static void frame(TerminalApp& app) {
  ImGui::NewFrame();
  ImGui::SetNextWindowSize(ImVec2(900, 500));
  if (ImGui::Begin("##terminalTabsHost", nullptr, ImGuiWindowFlags_NoTitleBar)) {
    app.drawTabs();
  }
  ImGui::End();
  ImGui::EndFrame();
}

/**
 * The tab bar the last frame drew.
 *
 * CurrentTabBar is popped when the bar ends, so it cannot be read afterwards. The
 * pool outlives the frame, and the map is how a pool is walked: FreeIdx reuses
 * slots, so going by index would hand back a dead bar. There is one bar here,
 * because the test never nests one.
 */
static const ImGuiTabBar* lastBar() {
  ImGuiContext& ctx = *ImGui::GetCurrentContext();
  const ImGuiTabBar* found = nullptr;
  for (int n = 0; n < ctx.TabBars.GetMapSize(); ++n) {
    if (const ImGuiTabBar* bar = ctx.TabBars.TryGetMapData(n)) {
      found = bar;
    }
  }
  return found;
}

/// The bar's tabs, without the "+" button, which shares the bar but is not a tab.
static std::vector<const ImGuiTabItem*> tabsInBar() {
  std::vector<const ImGuiTabItem*> tabs;
  const ImGuiTabBar* bar = lastBar();
  if (bar == nullptr) {
    return tabs;
  }
  for (int n = 0; n < bar->Tabs.Size; ++n) {
    const ImGuiTabItem& tab = bar->Tabs[n];
    if ((tab.Flags & ImGuiTabItemFlags_Button) == 0) {
      tabs.push_back(&tab);
    }
  }
  return tabs;
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  auto& app = TerminalApp::getInstance();

  // --- one tab ---
  //
  // Every tab carries the same title, and that is the point: identical labels are
  // what used to merge. The panel opens one tab of its own so there is always
  // something to type into.
  check(app.tabCount() == 0, "the panel holds no tabs before it is drawn");
  app.ensureTab();
  check(app.tabCount() == 1, "drawing the panel for the first time opens one tab");

  frame(app);
  check(tabsInBar().size() == 1, "it is drawn as one tab, got " + std::to_string(tabsInBar().size()));

  // --- a second one, the way "+" adds it ---
  //
  // The new tab is added between frames rather than during one, which is what the
  // "+" button ends up doing: it is read while the bar is being laid out, and the
  // tab it creates is first drawn on the next frame. Both tabs are titled
  // "Terminal", so a bar that keys on the label holds one and the user sees
  // nothing happen.
  app.newTab();
  frame(app);
  frame(app);
  check(app.tabCount() == 2, "two tabs are open, got " + std::to_string(app.tabCount()));
  check(tabsInBar().size() == 2, "both are drawn as tabs, got " + std::to_string(tabsInBar().size()));

  // A third, and the bar has to keep them apart over time rather than for one
  // frame: a bar resolves which tab is current by identity, so a merge shows up
  // as a bar that quietly lost one.
  app.newTab();
  frame(app);
  frame(app);
  check(tabsInBar().size() == 3, "the third is drawn too, got " + std::to_string(tabsInBar().size()));

  // Every tab has an identity of its own, which is what a shared label takes away.
  {
    const std::vector<const ImGuiTabItem*> tabs = tabsInBar();
    bool allDistinct = tabs.size() == 3;
    for (size_t a = 0; a < tabs.size() && allDistinct; ++a) {
      for (size_t b = a + 1; b < tabs.size(); ++b) {
        if (tabs[a]->ID == tabs[b]->ID) allDistinct = false;
      }
    }
    check(allDistinct, "no two tabs share an identity");
  }

  // The newest tab is the one whose contents are drawn. It was added after the
  // others were already there, so this is the tab ImGui selects as newly appeared,
  // and the panel has to agree with the bar about it.
  check(app.m_active == 2, "the newest tab is the active one, got " + std::to_string(app.m_active));

  // --- closing one leaves the others ---
  //
  // The other half of the same bug: a tab folded into another one cannot be
  // closed on its own either, so the count here is what proves the tabs are
  // separate objects rather than one tab drawn three times.
  app.closeTab(2);
  frame(app);
  frame(app);
  check(app.tabCount() == 2, "closing a tab leaves the rest, got " + std::to_string(app.tabCount()));
  check(tabsInBar().size() == 2, "the bar agrees there are two now, got " + std::to_string(tabsInBar().size()));

  app.closeTab(0);
  frame(app);
  frame(app);
  check(tabsInBar().size() == 1, "closing again leaves one, got " + std::to_string(tabsInBar().size()));

  // The bar still has to point at a tab that exists: m_active is the index the
  // contents are drawn with, and a stale one draws the wrong shell or none.
  check(app.m_active >= 0 && app.m_active < app.tabCount(),
        "the active tab still points at a tab that exists, got " + std::to_string(app.m_active));

  // A close that names no tab changes nothing, rather than dropping the last one.
  app.closeTab(99);
  app.closeTab(-1);
  frame(app);
  check(app.tabCount() == 1, "a close that names no tab changes nothing, got " + std::to_string(app.tabCount()));
  check(tabsInBar().size() == 1, "and the bar still has one, got " + std::to_string(tabsInBar().size()));

  ImGui::DestroyContext();

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}