/**
 * @file TerminalView.hpp
 * @brief Drawing one terminal session, and the keyboard that drives it
 * @ingroup undoapps
 *
 * The session knows what is on the screen and never touches a graphics API. This
 * is the other half: it paints the cell grid, draws the cursor and the selection,
 * and turns key presses into what the session should be sent.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "TerminalSession.hpp"

#include <string>

struct ImFont;

namespace undoApp {
namespace Terminal {

/**
 * @brief The view of one session
 */
class TerminalView
{
public:
   /**
    * @brief Draw the session inside the current ImGui window
    * @param session  Session to draw
    * @param font     Monospace font to draw with
    * @param fontSize Size to draw it at, adjusted by Ctrl and the wheel
    * @param focused  Whether this pane holds the keyboard
    *
    * The pane sizes the session to the space it was given, so a resize here is a
    * real terminal resize as far as the shell is concerned. A larger font makes
    * the cells larger and the session smaller: the grid cannot overlap, because
    * every glyph is drawn inside a cell measured from that same font and size.
    */
   void draw(TerminalSession& session, ImFont* font, float& fontSize, bool focused);

   /// @brief Turn this frame's key presses into what the session should be sent
   /// @param session Session to send to
   /// @param focused Whether this pane holds the keyboard
   void handleInput(TerminalSession& session, bool focused);

   /// @brief Copy the selection to the clipboard
   /// @param session Session to copy from
   void copySelection(TerminalSession& session);

   /// @brief Paste the clipboard into the session
   /// @param session Session to paste into
   void paste(TerminalSession& session);

   /// @brief Whether there is a selection to copy
   bool hasSelection() const { return m_selecting || m_hasSelection; }

private:
   void clampSelection();
   void zoom(float& fontSize, float notches) const;
   int rowAt(float y) const;
   int colAt(float x) const;

   Selection m_selection;   ///< Where the drag is, or was
   bool m_selecting = false; ///< A drag is in progress
   bool m_hasSelection = false; ///< A finished selection is still there
   bool m_dragging = false;
   int m_viewTop = 0;        ///< First viewport row under the cursor
   int m_viewLeft = 0;       ///< First viewport column under the cursor
   float m_cellWidth = 0.0f;
   float m_cellHeight = 0.0f;
   float m_fontSize = 0.0f;   ///< Size the glyphs are actually drawn at
};

} // namespace Terminal
} // namespace undoApp
