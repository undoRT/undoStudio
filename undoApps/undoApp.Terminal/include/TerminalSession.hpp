/**
 * @file TerminalSession.hpp
 * @brief A shell under a pseudo-terminal, and the screen it draws
 * @ingroup undoapps
 *
 * One session is one shell process. The pty is the child's end of a
 * pseudo-terminal, so the shell believes it is talking to a terminal and turns
 * on line editing, colours and full-screen programs the way it would against a
 * real terminal emulator.
 *
 * The bytes coming back are not interpreted here. libvterm does that and keeps a
 * screen of cells, and the screen is what gets read for drawing. Scrollback is
 * the lines the screen hands over as they scroll off the top.
 *
 * Nothing here touches a graphics API, so a session can be started, fed and
 * inspected without a window.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "vterm.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace undoApp {
namespace Terminal {

/**
 * @brief The colours the session is drawn in
 *
 * The emulator's own palette is xterm's, and its blue is a dark navy that
 * disappears on a dark background. These are the sixteen ANSI colours picked for
 * this background instead, with the blue light enough to read and the greys not
 * so far apart that ordinary output looks like an error.
 */
struct Palette
{
   uint32_t background; ///< Behind the text
   uint32_t foreground; ///< The text, when the program asked for no colour
   uint32_t ansi[16];    ///< The sixteen, in the order a program indexes them
};

/**
 * @brief The palette this session uses
 * @return The colours, as 0xRRGGBB
 */
const Palette& terminalPalette();

/**
 * @brief One cell of the screen, as far as drawing is concerned
 */
struct Cell
{
   std::string text;  ///< UTF-8 of the cell, empty for the tail of a wide glyph
   uint32_t fg = 0;   ///< 0xRRGGBB
   uint32_t bg = 0;   ///< 0xRRGGBB
   bool bold = false;
   bool italic = false;
   bool underline = false;
   bool reverse = false;
   char width = 1;   ///< 0 when this is the tail of a double-width glyph
};

/// @brief One line of cells
using CellLine = std::vector<Cell>;

/**
 * @brief A selection, as a pair of cells in the viewport
 */
struct Selection
{
   int startRow = 0; ///< Row the drag began on
   int startCol = 0;
   int endRow = 0;   ///< Row the drag ended on
   int endCol = 0;
};

/**
 * @brief A shell process and the screen it is drawing
 */
class TerminalSession
{
public:
   /**
    * @brief Prepare a session, without starting anything
    * @param shell Program to run, or empty for $SHELL
    * @param cwd   Directory to start in, or empty for the current one
    */
   explicit TerminalSession(std::string shell = {}, std::string cwd = {});
   ~TerminalSession();

   TerminalSession(const TerminalSession&) = delete;
   TerminalSession& operator=(const TerminalSession&) = delete;

   /**
    * @brief Start the shell
    * @param rows  Initial height in rows
    * @param cols  Initial width in columns
    * @param error Set to the reason when this returns false
    * @return True when the shell is running
    */
   bool start(int rows, int cols, std::string& error);

   /// @brief End the shell, if it is still running
   void stop();

   /// @brief Whether a shell is running on this session
   bool running() const { return m_master >= 0; }

   /**
    * @brief Move what the shell has written into the screen
    *
    * Reads what is pending on the pty, feeds it to the emulator and collects the
    * lines that scrolled off. Call this once per frame.
    */
   void pump();

   /**
    * @brief Send text, as if it had been typed
    * @param utf8 The text, UTF-8
    */
   void sendText(const std::string& utf8);

   /**
    * @brief Send a key, letting the emulator encode it
    * @param key The key, in libvterm's vocabulary
    * @param mod Modifiers held with it
    *
    * Going through the emulator is what makes an arrow an escape sequence and
    * Ctrl+C a 0x03, rather than a character the shell has to be told about.
    */
   void sendKey(VTermKey key, VTermModifier mod);

   /**
    * @brief Send a character with modifiers held
    * @param codepoint The character, as a code point
    * @param mod       Modifiers held with it
    *
    * The way a letter is sent. Ctrl+C is this with 'c' and VTERM_MOD_CTRL, which
    * the emulator turns into 0x03; a plain 'c' with the same modifier is the same
    * interrupt.
    */
   void sendChar(uint32_t codepoint, VTermModifier mod);

   /**
    * @brief Tell the shell and the emulator about a new size
    * @param rows New height in rows
    * @param cols New width in columns
    */
   void resize(int rows, int cols);

   int rows() const { return m_rows; }
   int cols() const { return m_cols; }

   /// @brief The cursor, in viewport coordinates
   VTermPos cursor() const { return m_cursor; }

   /// @brief Whether the cursor is to be drawn
   bool cursorVisible() const { return m_cursorVisible; }

   /**
    * @brief The lines to draw, top first
    * @return rows() lines, taken from the scrollback and the screen
    *
    * Scrolled back, the viewport starts in the scrollback and the live screen is
    * its tail.
    */
   const std::vector<CellLine>& viewport() const { return m_viewport; }

   /// @brief How many lines the shell has scrolled off the top
   int scrollbackSize() const { return static_cast<int>(m_scrollback.size()); }

   /// @brief How far the viewport is scrolled back, 0 being the live bottom
   int scrollOffset() const { return m_scrollOffset; }

   /**
    * @brief Scroll the viewport
    * @param delta Lines to move, positive to look further back
    */
   void scrollBy(int delta);

   /// @brief The text a selection covers, in the direction it was made
   std::string selectionText(const Selection& selection) const;

   /// @brief Whether the shell has exited
   bool exited() const { return m_exited; }

   /// @brief The shell's exit status, once it has exited
   int exitStatus() const { return m_exitStatus; }

   /// @brief Whether the shell rang the bell, clearing the flag
   bool consumeBell();

   /// @brief The title the shell set, empty when it has not set one
   const std::string& title() const { return m_title; }

private:
   /// @brief The emulator hands the bytes a key produced to the pty
   static void onOutput(const char* bytes, size_t len, void* user);

   /// @brief The emulator reports a line that has scrolled off the top
   static int onScrollbackPush(int cols, const VTermScreenCell* cells, void* user);

   static int onBell(void* user);
   static int onSetTermProp(VTermProp prop, VTermValue* value, void* user);
   static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user);

   /// @brief The callbacks the emulator is given, in the order it declares them
   static const VTermScreenCallbacks kCallbacks;

   void writeToPty(const char* bytes, size_t len);
   void rebuildViewport();
   void reap();

   std::string m_shell;
   std::string m_cwd;

   VTerm* m_vterm = nullptr;
   VTermScreen* m_screen = nullptr;

   int m_master = -1; ///< Our end of the pty
   int m_pid = -1;    ///< The shell

   int m_rows = 24;
   int m_cols = 80;

   std::deque<CellLine> m_scrollback;
   std::vector<CellLine> m_viewport;
   int m_scrollOffset = 0;

   VTermPos m_cursor{};
   bool m_cursorVisible = true;
   bool m_bell = false;
   bool m_exited = false;
   int m_exitStatus = 0;
   std::string m_title;
};

} // namespace Terminal
} // namespace undoApp
