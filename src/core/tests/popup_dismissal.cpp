/**
 * @file popup_dismissal.cpp
 * @brief Popups asked for by a flag, driven inside real ImGui frames without a window
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The bug this covers: the flag is what says "open it", so it has to be consumed
// the moment the popup is opened. A flag that only its own buttons clear is still
// standing on the next frame, and `if (flag) OpenPopup(...)` asks again. Escape
// closes the box and the frame after brings it back, so the modal cannot be
// dismissed and the frames around a dismissal look like the frames before it. That
// repetition is what reads as flickering.
//
// The question asked is whether the box is on screen after Escape, not whether the
// flag reads false: a flag that reads false while the box is still up has fixed
// nothing, and one that reads true while it is legitimately open is the bug itself.
//
// What is driven here is ImGuiManager's own About popup, through the code that owns
// it, so the assertion is about the product and not about a copy of the pattern.

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <cstring>
#include <string>

#define private public
#include "undoStudio/ui/ImGuiManager.hpp"
#undef private

using undoStudio::ui::ImGuiManager;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

/// The main menu bar's window, which is what owns the About popup.
static ImGuiWindow* menuBarWindow() {
  ImGuiContext& ctx = *ImGui::GetCurrentContext();
  for (int i = 0; i < ctx.Windows.Size; ++i) {
    if (std::strstr(ctx.Windows.Data[i]->Name, "MainMenuBar") != nullptr) {
      return ctx.Windows.Data[i];
    }
  }
  return nullptr;
}

/// Whether the About popup is on screen.
///
/// Asked as "is the popup the menu bar owns still on the open stack". The window
/// itself is not the answer: ImGui keeps a dismissed popup's window around, so it
/// is still there afterwards and its presence says only that it was opened once.
///
/// The popup cannot be looked up by name instead. Its id is resolved against the
/// window that owns it, and by the time the menu bar has closed that window is no
/// longer the current one, so a lookup from outside hashes against a different seed
/// and reports it absent even while it is up. ImGui also renames the window to
/// `##Popup_<hash>`, leaving nothing of the name to match on.
static bool aboutIsOpen() {
  ImGuiContext& ctx = *ImGui::GetCurrentContext();
  const ImGuiWindow* bar = menuBarWindow();
  for (int i = 0; i < ctx.OpenPopupStack.Size; ++i) {
    const ImGuiWindow* window = ctx.OpenPopupStack.Data[i].Window;
    if (window != nullptr && window->ParentWindow == bar) {
      return true;
    }
  }
  return false;
}

/**
 * One frame of the menu bar, which is where the About popup is drawn.
 *
 * renderMenuBar() opens and closes the menu bar itself, so the frame calls it rather
 * than wrapping it in a second one. The popup is asked for by setting the flag the
 * manager keeps for it, which is what the Help menu item does. Escape and the click
 * are delivered the way a backend delivers them.
 */
static void frame(ImGuiManager& mgr, bool ask, bool escape = false, bool clickOutside = false) {
  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(1280, 800);
  // A click far from the menu bar is the click-outside dismissal; one over it is an
  // ordinary click, which must dismiss nothing.
  io.AddMousePosEvent(clickOutside ? 900.0f : 400.0f, clickOutside ? 700.0f : 300.0f);
  if (escape) {
    io.AddKeyEvent(ImGuiKey_Escape, true);
  }
  if (clickOutside) {
    io.AddMouseButtonEvent(0, true);
  }

  ImGui::NewFrame();
  if (ask) {
    mgr.m_showAboutPopup = true;
  }
  mgr.renderMenuBar();
  ImGui::EndFrame();

  if (escape) {
    io.AddKeyEvent(ImGuiKey_Escape, false);
  }
  if (clickOutside) {
    io.AddMouseButtonEvent(0, false);
  }
}

/// A few ordinary frames: what a user does while looking at the box.
static void settle(ImGuiManager& mgr, int frames = 3) {
  for (int i = 0; i < frames; ++i) {
    frame(mgr, false);
  }
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  // The keyboard navigation is on in the product; without it Escape closes no popup
  // at all, and the test would be checking a build nobody ships.
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.DisplaySize = ImVec2(1280, 800);
  io.Fonts->AddFontDefault();
  io.Fonts->Build();

  auto& mgr = ImGuiManager::getInstance();

  // --- nothing asked for ---
  frame(mgr, false);
  settle(mgr);
  check(!aboutIsOpen(), "a frame with nothing asked for opens no popup");
  check(!mgr.m_showAboutPopup, "and leaves the ask down");

  // --- asked for, it opens ---
  frame(mgr, true);
  settle(mgr);
  check(aboutIsOpen(), "the About popup opens once it has been asked for");

  // --- it stays open while it is being looked at ---
  settle(mgr, 5);
  check(aboutIsOpen(), "it stays open over several frames, as a user reading it would");

  // --- Escape dismisses it, and it does not come back ---
  //
  // This is the whole point. The ask is only cleared inside the box's own Close
  // button, so a dismissal that does not go through that button leaves the ask
  // standing and the next frame asks again.
  frame(mgr, false, /*escape=*/true);
  settle(mgr, 3);
  check(!aboutIsOpen(), "Escape dismisses the popup and it does not come back");

  // --- a click outside dismisses it too ---
  frame(mgr, true);
  settle(mgr);
  check(aboutIsOpen(), "the popup can be asked for again after being dismissed");

  frame(mgr, false, /*escape=*/false, /*clickOutside=*/true);
  settle(mgr, 3);
  check(!aboutIsOpen(), "a click outside dismisses it and it does not come back");

  // --- a click on the menu bar is not a dismissal ---
  //
  // The other side of the same thing: clearing the ask on every frame would take the
  // popup down the moment anything else is clicked, which is a different bug.
  frame(mgr, true);
  settle(mgr);
  check(aboutIsOpen(), "the popup is open again before the click");
  frame(mgr, false, /*escape=*/false, /*clickOutside=*/false);
  settle(mgr, 2);
  check(aboutIsOpen(), "a click on the menu bar does not dismiss it");

  ImGui::DestroyContext();

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}