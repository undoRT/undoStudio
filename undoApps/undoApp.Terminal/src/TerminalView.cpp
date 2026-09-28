/**
 * @file TerminalView.cpp
 * @brief Drawing one terminal session, and the keyboard that drives it
 * @ingroup undoapps
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "TerminalView.hpp"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace undoApp {
namespace Terminal {

namespace {

/// @brief How many rows a scroll of the wheel moves
constexpr float kLinesPerWheel = 3.0f;

/**
 * @brief Wrap a 0xRRGGBB value in an ImGui colour
 * @param rgb The colour
 * @return The colour as ImGui draws it
 */
ImU32 toImU32(uint32_t rgb)
{
   return IM_COL32((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 0xFF);
}

/**
 * @brief The control character a letter stands for when Ctrl is held
 * @param letter The letter, already lowercased
 * @return The control code, or 0 when the letter has none
 */
char controlFor(char letter)
{
   if (letter >= 'a' && letter <= 'z') {
      return static_cast<char>(letter - 'a' + 1);
   }
   if (letter == '[') {
      return 27; // escape
   }
   if (letter == '\\') {
      return 28;
   }
   if (letter == ']') {
      return 29;
   }
   if (letter == '^') {
      return 30;
   }
   if (letter == '_' || letter == ' ') {
      return 31; // rubout
   }
   return 0;
}

} // namespace

int TerminalView::rowAt(float y) const
{
   if (m_cellHeight <= 0.0f) {
      return 0;
   }
   const int row = static_cast<int>(std::floor(y / m_cellHeight)) + m_viewTop;
   return std::max(0, row);
}

int TerminalView::colAt(float x) const
{
   if (m_cellWidth <= 0.0f) {
      return 0;
   }
   const int col = static_cast<int>(std::floor(x / m_cellWidth)) + m_viewLeft;
   return std::max(0, col);
}

void TerminalView::clampSelection()
{
   m_selection.startCol = std::max(0, m_selection.startCol);
   m_selection.endCol = std::max(0, m_selection.endCol);
   m_selection.startRow = std::max(0, m_selection.startRow);
   m_selection.endRow = std::max(0, m_selection.endRow);
}

void TerminalView::zoom(float& fontSize, float notches) const
{
   // Whole pixels, and bounded at both ends: below about 10 the box drawing a
   // full-screen program is made of stops being legible, and past about 32 the
   // pane is three columns wide and there is nothing left to read a line in.
   fontSize = std::max(10.0f, std::min(32.0f, fontSize + notches));
}

void TerminalView::draw(TerminalSession& session, ImFont* font, float& fontSize, bool focused)
{
   ImGuiIO& io = ImGui::GetIO();
   const ImVec2 available = ImGui::GetContentRegionAvail();
   if (available.x < 40.0f || available.y < 20.0f) {
      return;
   }

   // A terminal owns the keyboard while it is focused, and the character stream
   // is how text arrives. Without this the keys go to whichever panel has focus
   // and typing into the terminal does nothing.
   if (focused) {
      io.WantTextInput = true;
   }

   // The cell is measured with the terminal font pushed, and the glyphs are drawn
   // with the same font on the draw list's own stack. Asking for a size
   // separately is how a grid ends up sized for one font and painted with
   // another: ImGui 1.92 scales the font handed to AddText by the size passed
   // beside it, so a cell measured at 15 and drawn at 20 puts every glyph a third
   // wider than the space it has.
   // Ctrl and the wheel change the size, and ImGui bakes the font at whatever
   // size is asked for, so nothing has to be rebuilt for it.
   if (io.KeyCtrl && io.MouseWheel != 0.0f) {
      zoom(fontSize, io.MouseWheel);
   }

   if (font != nullptr) {
      ImGui::PushFont(font, fontSize);
   }
   m_cellWidth = ImGui::CalcTextSize("M").x;
   m_cellHeight = ImGui::GetTextLineHeight();
   m_fontSize = ImGui::GetFontSize();
   if (font != nullptr) {
      ImGui::PopFont();
   }
   if (m_cellWidth <= 0.0f || m_cellHeight <= 0.0f) {
      return;
   }

   // The screen is whatever fits, at least a few rows, and the shell is told when
   // that differs from what it has: a resize here is a real terminal resize.
   const int rows = std::max(3, static_cast<int>(std::floor(available.y / m_cellHeight)));
   const int cols = std::max(20, static_cast<int>(std::floor(available.x / m_cellWidth)));
   if (rows != session.rows() || cols != session.cols()) {
      session.resize(rows, cols);
   }
   const int visibleRows = std::min(rows, static_cast<int>(session.viewport().size()));

   ImDrawList* drawList = ImGui::GetWindowDrawList();
   const ImVec2 origin = ImGui::GetCursorScreenPos();
   const uint32_t backgroundColour = terminalPalette().background;

   // The glyph sits in the middle of its cell rather than against the top of it:
   // the line height carries leading the glyph itself does not. The size handed
   // to AddText is the one the cell was measured at, not whatever the context
   // happens to be on, which is what keeps the two in step.
   ImFont* drawFont = font;
   const float textOffset = (m_cellHeight - m_fontSize) * 0.5f;

   // Clipped to the pane. The grid is sized to fit, but a glyph a fraction of a
   // pixel wider than its cell puts the last column of every row over whatever is
   // beside the pane, and a terminal that paints on the tab bar is not a terminal
   // anyone can read.
   const ImVec2 clipMax(origin.x + cols * m_cellWidth, origin.y + visibleRows * m_cellHeight);
   drawList->PushClipRect(origin, clipMax, true);

   for (int row = 0; row < visibleRows; ++row) {
      const CellLine& line = session.viewport()[static_cast<size_t>(row)];
      if (line.empty()) {
         continue;
      }
      const float y = origin.y + static_cast<float>(row) * m_cellHeight;
      int col = 0;
      while (col < static_cast<int>(line.size())) {
         const Cell& cell = line[static_cast<size_t>(col)];
         const int span = (cell.width == 2) ? 2 : 1;
         if (cell.width == 0) {
            ++col; // the tail of a wide glyph was drawn with it
            continue;
         }

         const uint32_t foreground = cell.reverse ? cell.bg : cell.fg;
         const uint32_t background = cell.reverse ? cell.fg : cell.bg;

         // Only the cells the program painted get a background. The rest is left
         // to the pane behind, so the end of a line is the IDE's own background
         // rather than a bar of a colour the eye reads as part of the text.
         if (background != backgroundColour) {
            const float x0 = origin.x + static_cast<float>(col) * m_cellWidth;
            const float x1 = origin.x + static_cast<float>(col + span) * m_cellWidth;
            drawList->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + m_cellHeight), toImU32(background));
         }

         // Contiguous cells that would be drawn the same way become one draw
         // call. A full screen is eight thousand cells, and one call per cell is
         // not something to do sixty times a second.
         int end = col;
         while (end < static_cast<int>(line.size())) {
            const Cell& next = line[static_cast<size_t>(end)];
            if (next.width == 0) {
               ++end;
               continue;
            }
            const uint32_t nextForeground = next.reverse ? next.bg : next.fg;
            const bool sameBold = (next.bold == cell.bold);
            const bool sameUnderline = (next.underline == cell.underline);
            if (nextForeground != foreground || !sameBold || !sameUnderline) {
               break;
            }
            end += (next.width == 2) ? 2 : 1;
         }
         end = std::min(end, static_cast<int>(line.size()));

         std::string run;
         for (int i = col; i < end; ++i) {
            run += line[static_cast<size_t>(i)].text;
         }
         if (!run.empty()) {
            const ImU32 colour = toImU32(foreground);
            const ImVec2 at(origin.x + static_cast<float>(col) * m_cellWidth, y + textOffset);
            if (cell.bold) {
               // A synthetic bold: DejaVu Sans Mono is loaded in one weight, and
               // the same glyph a pixel to the right is what stands in for the
               // other one.
               drawList->AddText(drawFont, m_fontSize, ImVec2(at.x + 1.0f, at.y), colour, run.c_str());
            }
            drawList->AddText(drawFont, m_fontSize, at, colour, run.c_str());
            if (cell.underline) {
               drawList->AddLine(ImVec2(at.x, y + m_cellHeight - 1.0f),
                                 ImVec2(origin.x + static_cast<float>(end) * m_cellWidth, y + m_cellHeight - 1.0f),
                                 colour, 1.0f);
            }
         }
         col = std::max(end, col + 1);
      }
   }

   // The cursor, as a block, since a terminal is not a text editor and has no
   // caret to blend into the glyph.
   if (focused && session.cursorVisible() && session.scrollOffset() == 0) {
      const int cursorRow = session.cursor().row;
      const int cursorCol = session.cursor().col;
      if (cursorRow >= 0 && cursorRow < visibleRows && cursorCol >= 0) {
         const CellLine& line = session.viewport()[static_cast<size_t>(cursorRow)];
         if (cursorCol < static_cast<int>(line.size())) {
            const Cell& cell = line[static_cast<size_t>(cursorCol)];
            const ImVec2 at(origin.x + static_cast<float>(cursorCol) * m_cellWidth,
                            origin.y + static_cast<float>(cursorRow) * m_cellHeight);
            const ImVec2 to(at.x + m_cellWidth * (cell.width == 2 ? 2.0f : 1.0f), at.y + m_cellHeight);
            const uint32_t foreground = cell.reverse ? cell.bg : cell.fg;
            const uint32_t background = cell.reverse ? cell.fg : cell.bg;
            drawList->AddRectFilled(at, to, toImU32(foreground));
            if (!cell.text.empty()) {
               drawList->AddText(at, toImU32(background), cell.text.c_str());
            }
         } else {
            // The caret is past the end of the line, which is where a shell leaves
            // it while waiting. There is no cell there to invert, so the block is
            // painted in the foreground colour on its own.
            const ImVec2 at(origin.x + static_cast<float>(cursorCol) * m_cellWidth,
                            origin.y + static_cast<float>(cursorRow) * m_cellHeight);
            drawList->AddRectFilled(at, ImVec2(at.x + m_cellWidth, at.y + m_cellHeight),
                                    IM_COL32(0xc8, 0xcc, 0xd4, 0xFF));
         }
      }
   }

   drawList->PopClipRect();

   // The selection, over whatever it covers.
   if (m_hasSelection || m_selecting) {
      clampSelection();
      const int first = std::min(m_selection.startRow, m_selection.endRow);
      const int last = std::max(m_selection.startRow, m_selection.endRow);
      for (int row = std::max(0, first); row <= std::min(last, visibleRows - 1); ++row) {
         const int from = (row == first) ? std::min(m_selection.startCol, m_selection.endCol) : 0;
         const int to = (row == last) ? std::max(m_selection.startCol, m_selection.endCol) : cols - 1;
         const ImVec2 at(origin.x + static_cast<float>(from) * m_cellWidth,
                         origin.y + static_cast<float>(row) * m_cellHeight);
         const ImVec2 to2(origin.x + static_cast<float>(to + 1) * m_cellWidth, at.y + m_cellHeight);
         drawList->AddRectFilled(at, to2, IM_COL32(255, 255, 255, 40));
      }
   }

   // The pane is a real ImGui item, so it is the one that gets the mouse and the
   // focus. The size is what the next frame will ask the session to be.
   ImGui::InvisibleButton("##terminal", ImVec2(cols * m_cellWidth, visibleRows * m_cellHeight),
                          ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

   if (ImGui::IsItemHovered()) {
      // The plain wheel scrolls. With Ctrl held it is already changing the size,
      // and scrolling as well would move the viewport out from under the text the
      // user is resizing.
      if (io.MouseWheel != 0.0f && !io.KeyCtrl) {
         session.scrollBy(static_cast<int>(-io.MouseWheel * kLinesPerWheel));
      }
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
         // A click is a click, and it moves the focus here: the terminal is only
         // the keyboard holder once something has put the caret in it.
         ImGui::SetKeyboardFocusHere();
         const ImVec2 mouse = io.MousePos;
         m_selection.startRow = rowAt(mouse.y - origin.y);
         m_selection.startCol = colAt(mouse.x - origin.x);
         m_selection.endRow = m_selection.startRow;
         m_selection.endCol = m_selection.startCol;
         m_selecting = true;
         m_hasSelection = false;
      }
      if (m_selecting && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
         const ImVec2 mouse = io.MousePos;
         m_selection.endRow = rowAt(mouse.y - origin.y);
         m_selection.endCol = colAt(mouse.x - origin.x);
         m_dragging = true;
      }
   }
   if (m_selecting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      m_selecting = false;
      clampSelection();
      // A click that never moved is not a selection.
      m_hasSelection = m_dragging || m_selection.startRow != m_selection.endRow ||
                       m_selection.startCol != m_selection.endCol;
      m_dragging = false;
   }

   session.consumeBell();
}

void TerminalView::handleInput(TerminalSession& session, bool focused)
{
   if (!focused || !session.running()) {
      return;
   }
   ImGuiIO& io = ImGui::GetIO();
   const bool ctrl = io.KeyCtrl;
   const bool shift = io.KeyShift;
   const bool alt = io.KeyAlt;

   // The clipboard keys are the IDE's, not the shell's: Ctrl+Shift+C copies and
   // Ctrl+Shift+V pastes, while a plain Ctrl+C is the interrupt the shell expects.
   if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_C)) {
      copySelection(session);
      return;
   }
   if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_V)) {
      paste(session);
      return;
   }
   if (ctrl && shift && ImGui::IsKeyPressed(ImGuiKey_A)) {
      return;
   }

   if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
      session.sendChar('\r', static_cast<VTermModifier>(0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
      session.sendKey(VTERM_KEY_BACKSPACE, static_cast<VTermModifier>(ctrl ? VTERM_MOD_CTRL : 0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
      session.sendKey(VTERM_KEY_DEL, static_cast<VTermModifier>(ctrl ? VTERM_MOD_CTRL : 0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
      session.sendChar('\t', static_cast<VTermModifier>(0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      session.sendChar(27, static_cast<VTermModifier>(0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
      session.sendKey(VTERM_KEY_UP, static_cast<VTermModifier>(ctrl ? VTERM_MOD_CTRL : 0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
      session.sendKey(VTERM_KEY_DOWN, static_cast<VTermModifier>(ctrl ? VTERM_MOD_CTRL : 0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
      session.sendKey(VTERM_KEY_LEFT, static_cast<VTermModifier>(0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
      session.sendKey(VTERM_KEY_RIGHT, static_cast<VTermModifier>(0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
      session.sendKey(VTERM_KEY_HOME, static_cast<VTermModifier>(ctrl ? VTERM_MOD_CTRL : 0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_End)) {
      session.sendKey(VTERM_KEY_END, static_cast<VTermModifier>(ctrl ? VTERM_MOD_CTRL : 0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
      session.sendKey(VTERM_KEY_PAGEUP, static_cast<VTermModifier>(0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
      session.sendKey(VTERM_KEY_PAGEDOWN, static_cast<VTermModifier>(0));
      return;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Insert)) {
      session.sendKey(VTERM_KEY_INS, static_cast<VTermModifier>(0));
      return;
   }
   // Ctrl with a letter is the control character, not the letter.
   if (ctrl && !shift && !alt) {
      for (int key = ImGuiKey_A; key <= ImGuiKey_Z; ++key) {
         if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(key))) {
            const char control = controlFor(static_cast<char>('a' + (key - ImGuiKey_A)));
            if (control != 0) {
               session.sendChar(static_cast<uint32_t>(control), VTERM_MOD_NONE);
            }
            return;
         }
      }
   }

   // Anything else printable arrives as a character. Alt is left out on purpose:
   // it is the "send escape" modifier in a terminal, and a dead key on some
   // layouts, and both of those are better left to the shell's own bindings.
   if (alt) {
      return;
   }
   for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
      const ImWchar wide = io.InputQueueCharacters[i];
      if (wide != 0 && wide < 0x80) {
         session.sendChar(static_cast<uint32_t>(wide), static_cast<VTermModifier>(0));
      }
   }
   io.InputQueueCharacters.clear();
}

void TerminalView::copySelection(TerminalSession& session)
{
   if (!m_hasSelection && !m_selecting) {
      return;
   }
   clampSelection();
   const std::string text = session.selectionText(m_selection);
   if (!text.empty()) {
      ImGui::SetClipboardText(text.c_str());
   }
   m_hasSelection = false;
}

void TerminalView::paste(TerminalSession& session)
{
   const char* clipboard = ImGui::GetClipboardText();
   if (clipboard != nullptr && clipboard[0] != '\0') {
      session.sendText(clipboard);
   }
}

} // namespace Terminal
} // namespace undoApp
