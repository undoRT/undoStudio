/**
 * @file TerminalSession.cpp
 * @brief A shell under a pseudo-terminal, and the screen it draws
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "TerminalSession.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace undoApp {
namespace Terminal {

namespace {

/// @brief How much to read from the pty at a time
constexpr size_t kReadChunk = 8192;

/// @brief Upper bound on the scrollback, in lines
constexpr int kScrollbackLimit = 5000;

/**
 * @brief Append one code point to a string, as UTF-8
 * @param out   String to append to
 * @param cp    The code point
 */
void appendUtf8(std::string& out, uint32_t cp)
{
   if (cp < 0x80) {
      out += static_cast<char>(cp);
   } else if (cp < 0x800) {
      out += static_cast<char>(0xC0 | (cp >> 6));
      out += static_cast<char>(0x80 | (cp & 0x3F));
   } else if (cp < 0x10000) {
      out += static_cast<char>(0xE0 | (cp >> 12));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
   } else {
      out += static_cast<char>(0xF0 | (cp >> 18));
      out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (cp & 0x3F));
   }
}

/**
 * @brief The first code point of a UTF-8 string
 * @param text The string, at least one byte long
 * @return The code point, or 0 when the string is malformed
 */
uint32_t firstCodepoint(const std::string& text)
{
   if (text.empty()) {
      return 0;
   }
   const unsigned char lead = static_cast<unsigned char>(text[0]);
   const size_t available = text.size();
   if (lead < 0x80) {
      return lead;
   }
   if ((lead & 0xE0) == 0xC0 && available >= 2) {
      return ((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(text[1]) & 0x3Fu);
   }
   if ((lead & 0xF0) == 0xE0 && available >= 3) {
      return ((lead & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[1]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(text[2]) & 0x3Fu);
   }
   if ((lead & 0xF8) == 0xF0 && available >= 4) {
      return ((lead & 0x07u) << 18) | ((static_cast<unsigned char>(text[1]) & 0x3Fu) << 12) |
             ((static_cast<unsigned char>(text[2]) & 0x3Fu) << 6) |
             (static_cast<unsigned char>(text[3]) & 0x3Fu);
   }
   return 0;
}

/**
 * @brief A cell the emulator has nothing to say about
 *
 * The emulator only stores the cells it has written to. Asking for one it never
 * wrote leaves the caller's cell untouched, and a caller that starts from a
 * zeroed one gets a background of pure black, which is a different colour from
 * the terminal's and so gets painted: one black block at the end of every line.
 * An empty cell has to be built explicitly, in the default colours.
 *
 * @return A cell with no text, in the palette's own foreground and background
 */
Cell blankCell()
{
   const Palette& palette = terminalPalette();
   Cell cell;
   cell.fg = palette.foreground;
   cell.bg = palette.background;
   return cell;
}

/**
 * @brief Copy an emulator cell into ours, in the colours the emulator resolved
 * @param source The emulator's cell
 * @param screen Screen the colour is resolved against
 * @return Our cell
 */
Cell toCell(const VTermScreenCell& source, VTermScreen* screen)
{
   Cell cell;
   cell.width = source.width;
   cell.bold = source.attrs.bold != 0;
   cell.italic = source.attrs.italic != 0;
   cell.underline = source.attrs.underline != VTERM_UNDERLINE_OFF;
   cell.reverse = source.attrs.reverse != 0;

   // chars[] is a run of code points, most often one. A blank cell and the tail
   // of a double-width glyph both come back with a zero first code point, and
   // both are drawn as nothing.
   if (source.chars[0] != 0) {
      appendUtf8(cell.text, source.chars[0]);
      for (int i = 1; i < VTERM_MAX_CHARS_PER_CELL && source.chars[i] != 0; ++i) {
         appendUtf8(cell.text, source.chars[i]);
      }
   }

   // The colours are resolved even for a cell with no text in it. A cell is the
   // overwhelming majority of a terminal, and returning early for the empty ones
   // left them at zero, which is black and not the terminal's background: one
   // painted block at the end of every line.
   VTermColor fg = source.fg;
   VTermColor bg = source.bg;
   vterm_screen_convert_color_to_rgb(screen, &fg);
   vterm_screen_convert_color_to_rgb(screen, &bg);
   cell.fg = (static_cast<uint32_t>(fg.rgb.red) << 16) | (static_cast<uint32_t>(fg.rgb.green) << 8) | fg.rgb.blue;
   cell.bg = (static_cast<uint32_t>(bg.rgb.red) << 16) | (static_cast<uint32_t>(bg.rgb.green) << 8) | bg.rgb.blue;
   return cell;
}

/**
 * @brief Copy one of our cells back into an emulator cell
 * @param source Our cell
 * @param target The emulator's cell to fill
 *
 * Only used to hand a scrollback line back to the emulator, which happens when
 * the screen is shrunk. The colour is kept as an RGB type, so converting it back
 * to RGB is the identity.
 */
void fromCell(const Cell& source, VTermScreenCell& target)
{
   std::memset(&target, 0, sizeof(target));
   target.width = source.width;
   target.attrs.bold = source.bold ? 1 : 0;
   target.attrs.italic = source.italic ? 1 : 0;
   target.attrs.underline = source.underline ? VTERM_UNDERLINE_SINGLE : VTERM_UNDERLINE_OFF;
   target.attrs.reverse = source.reverse ? 1 : 0;
   target.fg.type = VTERM_COLOR_RGB;
   target.bg.type = VTERM_COLOR_RGB;
   vterm_color_rgb(&target.fg, static_cast<uint8_t>(source.fg >> 16), static_cast<uint8_t>(source.fg >> 8),
                   static_cast<uint8_t>(source.fg));
   vterm_color_rgb(&target.bg, static_cast<uint8_t>(source.bg >> 16), static_cast<uint8_t>(source.bg >> 8),
                   static_cast<uint8_t>(source.bg));
   if (!source.text.empty()) {
      target.chars[0] = firstCodepoint(source.text);
   }
}

/// @brief Put a descriptor into non-blocking mode
/// @param fd The descriptor
/// @return True on success
bool setNonBlocking(int fd)
{
   const int flags = ::fcntl(fd, F_GETFL, 0);
   return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

/// @brief Fill in a window size
/// @param size  The structure to fill
/// @param rows  Height in rows
/// @param cols  Width in columns
void fillWinsize(struct winsize& size, int rows, int cols)
{
   size.ws_row = static_cast<unsigned short>(rows);
   size.ws_col = static_cast<unsigned short>(cols);
   size.ws_xpixel = 0;
   size.ws_ypixel = 0;
}

} // namespace

const Palette& terminalPalette()
{
   static const Palette palette = {
      0x14161c, // background
      0xc8ccd4, // foreground
      {
         0x2b3038, // black          the darkest, kept off the background
         0xe06c75, // red
         0x98c379, // green
         0xe5c07b, // yellow
         0x61afef, // blue           xterm's navy is unreadable here
         0xc678dd, // magenta
         0x56b6c2, // cyan
         0xdcdfe4, // white
         0x5c6370, // bright black
         0xff7b86, // bright red
         0xb5e890, // bright green
         0xf0d399, // bright yellow
         0x7fc4ff, // bright blue
         0xdd8ff0, // bright magenta
         0x6fd3de, // bright cyan
         0xffffff, // bright white
      },
   };
   return palette;
}


// The emulator keeps the pointer to this rather than a copy, so it has to outlive
// the session: a local in start() is dangling by the time the first byte arrives.
const VTermScreenCallbacks TerminalSession::kCallbacks = {
   nullptr, // damage: the viewport is rebuilt whole, so nothing is tracked
   nullptr, // moverect: likewise
   TerminalSession::onMoveCursor,
   TerminalSession::onSetTermProp,
   TerminalSession::onBell,
   nullptr, // resize: the pane decides the size, not the program
   TerminalSession::onScrollbackPush,
   nullptr, // sb_popline: the viewport reads the scrollback directly
   nullptr, // sb_clear
   nullptr, // sb_pushline4: only for the ABI-compat path
};

TerminalSession::TerminalSession(std::string shell, std::string cwd) : m_shell(std::move(shell)), m_cwd(std::move(cwd))
{
   if (m_shell.empty()) {
      const char* fromEnv = std::getenv("SHELL");
      m_shell = (fromEnv != nullptr && fromEnv[0] != '\0') ? fromEnv : "/bin/sh";
   }
   if (m_cwd.empty()) {
      char buffer[4096];
      if (::getcwd(buffer, sizeof(buffer)) != nullptr) {
         m_cwd = buffer;
      }
   }
}

TerminalSession::~TerminalSession()
{
   stop();
   if (m_vterm != nullptr) {
      // The screen and the state belong to the VTerm and go with it.
      vterm_free(m_vterm);
      m_vterm = nullptr;
      m_screen = nullptr;
   }
}

bool TerminalSession::start(int rows, int cols, std::string& error)
{
   if (m_master >= 0) {
      error = "already running";
      return false;
   }
   if (m_vterm != nullptr) {
      // A session cannot be started twice, and it is not reusable after stop():
      // the emulator and the child are both gone.
      error = "already started";
      return false;
   }
   rows = std::max(1, rows);
   cols = std::max(1, cols);

   m_vterm = vterm_new(rows, cols);
   if (m_vterm == nullptr) {
      error = "cannot create the terminal emulator";
      return false;
   }
   vterm_set_utf8(m_vterm, 1);
   VTermState* state = vterm_obtain_state(m_vterm);
   m_screen = vterm_obtain_screen(m_vterm);
   vterm_screen_enable_reflow(m_screen, 1);

   // The reset is what installs the character encodings. Obtaining the state only
   // builds it, and without this the first byte that is not a control sequence
   // dereferences an encoding that was never set up.
   vterm_state_reset(state, 1);

   // The colours go in before anything is written, so the very first prompt is
   // painted with these and not with the emulator's idea of black on white.
   const Palette& palette = terminalPalette();
   for (int index = 0; index < 16; ++index) {
      VTermColor colour;
      vterm_color_rgb(&colour, static_cast<uint8_t>(palette.ansi[index] >> 16),
                      static_cast<uint8_t>(palette.ansi[index] >> 8), static_cast<uint8_t>(palette.ansi[index]));
      vterm_state_set_palette_color(state, index, &colour);
   }
   VTermColor background;
   vterm_color_rgb(&background, static_cast<uint8_t>(palette.background >> 16),
                  static_cast<uint8_t>(palette.background >> 8), static_cast<uint8_t>(palette.background));
   VTermColor foreground;
   vterm_color_rgb(&foreground, static_cast<uint8_t>(palette.foreground >> 16),
                   static_cast<uint8_t>(palette.foreground >> 8), static_cast<uint8_t>(palette.foreground));
   vterm_screen_set_default_colors(m_screen, &foreground, &background);

   vterm_screen_set_callbacks(m_screen, &kCallbacks, this);
   vterm_output_set_callback(m_vterm, &TerminalSession::onOutput, this);

   const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
   if (master < 0) {
      error = std::string("cannot open a pseudo-terminal: ") + std::strerror(errno);
      return false;
   }
   if (::grantpt(master) != 0 || ::unlockpt(master) != 0) {
      error = std::string("cannot use the pseudo-terminal: ") + std::strerror(errno);
      ::close(master);
      return false;
   }
   const char* slaveName = ::ptsname(master);
   if (slaveName == nullptr) {
      error = std::string("cannot name the pseudo-terminal: ") + std::strerror(errno);
      ::close(master);
      return false;
   }
   const std::string slavePath(slaveName);

   struct winsize size = {};
   fillWinsize(size, rows, cols);

   const pid_t pid = ::fork();
   if (pid < 0) {
      error = std::string("cannot fork: ") + std::strerror(errno);
      ::close(master);
      return false;
   }

   if (pid == 0) {
      // The child. It becomes a session leader, takes the slave as its
      // controlling terminal, and becomes the shell. Nothing here returns.
      ::setsid();
      const int slave = ::open(slavePath.c_str(), O_RDWR);
      if (slave >= 0) {
         ::ioctl(slave, TIOCSCTTY, 0);
         ::dup2(slave, STDIN_FILENO);
         ::dup2(slave, STDOUT_FILENO);
         ::dup2(slave, STDERR_FILENO);
         if (slave > STDERR_FILENO) {
            ::close(slave);
         }
      }
      if (!m_cwd.empty() && ::chdir(m_cwd.c_str()) != 0) {
         ::_exit(127); // a directory that is not there is not worth retrying over
      }
      ::setenv("TERM", "xterm-256color", 1);
      ::setenv("COLUMNS", std::to_string(cols).c_str(), 1);
      ::setenv("LINES", std::to_string(rows).c_str(), 1);

      std::string shell = m_shell;
      char* const argv[] = {shell.data(), nullptr};
      ::execv(shell.c_str(), argv);
      static const char kFailed[] = "\r\nundoStudio: cannot run " "\r\n";
      const ssize_t ignored = ::write(STDERR_FILENO, kFailed, sizeof(kFailed) - 1);
      (void)ignored;
      ::_exit(127);
   }

   // The parent keeps only the master. The slave is closed here as well: leaving
   // it open would keep the master from ever reporting EOF.
   const int slaveToClose = ::open(slavePath.c_str(), O_RDWR);
   if (slaveToClose >= 0) {
      ::close(slaveToClose);
   }

   m_master = master;
   m_pid = pid;
   m_rows = rows;
   m_cols = cols;
   m_exited = false;
   m_exitStatus = 0;
   m_scrollback.clear();
   m_scrollOffset = 0;
   if (!setNonBlocking(m_master)) {
      error = std::string("cannot put the terminal into non-blocking mode: ") + std::strerror(errno);
      stop();
      return false;
   }
   rebuildViewport();
   return true;
}

void TerminalSession::stop()
{
   if (m_master >= 0) {
      // Closing the master hangs the shell up, which is what a terminal does when
      // the window it is in goes away.
      ::close(m_master);
      m_master = -1;
   }
   if (m_pid > 0) {
      for (int attempt = 0; attempt < 50 && !m_exited; ++attempt) {
         reap();
         if (!m_exited) {
            ::usleep(10000);
         }
      }
      if (!m_exited) {
         ::kill(m_pid, SIGHUP);
         ::usleep(50000);
         reap();
      }
      if (!m_exited) {
         ::kill(m_pid, SIGKILL);
         reap();
      }
      m_pid = -1;
   }
}

void TerminalSession::pump()
{
   if (m_master < 0 || m_vterm == nullptr) {
      return;
   }
   bool moved = false;
   char buffer[kReadChunk];
   for (;;) {
      const ssize_t got = ::read(m_master, buffer, sizeof(buffer));
      if (got > 0) {
         vterm_input_write(m_vterm, buffer, static_cast<size_t>(got));
         moved = true;
         continue;
      }
      if (got == 0) {
         break; // the shell is gone
      }
      if (errno == EINTR) {
         continue;
      }
      break; // EAGAIN: nothing more for now
   }

   reap();
   if (moved) {
      vterm_screen_flush_damage(m_screen);
      rebuildViewport();
   }
}

void TerminalSession::sendText(const std::string& utf8)
{
   if (m_master < 0) {
      return;
   }
   // Bracketed, so that a shell knows it is a paste and a program reading the
   // terminal does not act on the first line of it.
   vterm_keyboard_start_paste(m_vterm);
   writeToPty(utf8.data(), utf8.size());
   vterm_keyboard_end_paste(m_vterm);
}

void TerminalSession::sendKey(VTermKey key, VTermModifier mod)
{
   if (m_master < 0 || m_vterm == nullptr) {
      return;
   }
   // The emulator turns the key into the bytes a terminal would send, and
   // reaches onOutput with them.
   vterm_keyboard_key(m_vterm, key, mod);
}

void TerminalSession::sendChar(uint32_t codepoint, VTermModifier mod)
{
   if (m_master < 0 || m_vterm == nullptr) {
      return;
   }
   vterm_keyboard_unichar(m_vterm, codepoint, mod);
}

void TerminalSession::resize(int rows, int cols)
{
   rows = std::max(1, rows);
   cols = std::max(1, cols);
   if (rows == m_rows && cols == m_cols) {
      return;
   }
   m_rows = rows;
   m_cols = cols;
   if (m_vterm != nullptr) {
      vterm_set_size(m_vterm, rows, cols);
   }
   if (m_master >= 0) {
      struct winsize size = {};
      fillWinsize(size, rows, cols);
      // This is what makes the shell learn about the resize, through SIGWINCH.
      ::ioctl(m_master, TIOCSWINSZ, &size);
   }
   m_scrollOffset = 0;
   rebuildViewport();
}

void TerminalSession::scrollBy(int delta)
{
   m_scrollOffset = std::max(0, std::min(scrollbackSize(), m_scrollOffset + delta));
   rebuildViewport();
}

std::string TerminalSession::selectionText(const Selection& selection) const
{
   if (m_viewport.empty()) {
      return {};
   }
   const int lastRow = static_cast<int>(m_viewport.size()) - 1;
   const int firstRow = std::max(0, std::min(std::min(selection.startRow, selection.endRow), lastRow));
   const int finalRow = std::max(0, std::min(std::max(selection.startRow, selection.endRow), lastRow));

   // A selection made upwards reads from the row the drag finished on to the row
   // it started on, the way dragging backwards in a text editor does. Within a
   // row it always reads left to right.
   const bool backwards = selection.startRow > selection.endRow ||
                          (selection.startRow == selection.endRow && selection.startCol > selection.endCol);
   const int anchorOnFirst = backwards ? selection.endCol : selection.startCol;
   const int anchorOnLast = backwards ? selection.startCol : selection.endCol;

   std::string text;
   for (int row = firstRow; row <= finalRow; ++row) {
      const CellLine& line = m_viewport[static_cast<size_t>(row)];
      if (line.empty()) {
         if (row != finalRow) {
            text += '\n';
         }
         continue;
      }
      const int lastCol = static_cast<int>(line.size()) - 1;
      int from = 0;
      int to = lastCol;
      if (row == firstRow) {
         from = std::max(0, std::min(anchorOnFirst, lastCol));
      }
      if (row == finalRow) {
         to = std::max(0, std::min(anchorOnLast, lastCol));
      }
      if (from > to) {
         std::swap(from, to);
      }
      for (int col = from; col <= to; ++col) {
         text += line[static_cast<size_t>(col)].text;
      }
      // The break belongs between the rows the selection spans, and nowhere else:
      // a drag that stops short of the end of its last line has not selected the
      // line break after it.
      if (row != finalRow) {
         text += '\n';
      }
   }
   return text;
}

bool TerminalSession::consumeBell()
{
   const bool bell = m_bell;
   m_bell = false;
   return bell;
}

void TerminalSession::writeToPty(const char* bytes, size_t len)
{
   if (m_master < 0) {
      return;
   }
   size_t written = 0;
   while (written < len) {
      const ssize_t put = ::write(m_master, bytes + written, len - written);
      if (put > 0) {
         written += static_cast<size_t>(put);
         continue;
      }
      if (put < 0 && errno == EINTR) {
         continue;
      }
      break; // EAGAIN: the shell is not reading this frame
   }
}

void TerminalSession::reap()
{
   if (m_pid <= 0 || m_exited) {
      return;
   }
   int status = 0;
   if (::waitpid(m_pid, &status, WNOHANG) == m_pid) {
      m_exited = true;
      m_exitStatus = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
      m_pid = -1;
   }
}

void TerminalSession::rebuildViewport()
{
   if (m_screen == nullptr) {
      return;
   }
   m_viewport.clear();
   m_viewport.reserve(static_cast<size_t>(m_rows));

   // Scrolled back, the viewport starts in the scrollback and the live screen is
   // its tail. At the bottom it is the screen alone.
   const size_t scrollbackLines = m_scrollback.size();
   const size_t fromScrollback = scrollbackLines - static_cast<size_t>(std::min(m_scrollOffset, static_cast<int>(scrollbackLines)));
   for (size_t i = fromScrollback; i < scrollbackLines; ++i) {
      m_viewport.push_back(m_scrollback[i]);
   }

   for (int row = 0; row < m_rows && m_viewport.size() < static_cast<size_t>(m_rows); ++row) {
      CellLine line(static_cast<size_t>(m_cols));
      for (int col = 0; col < m_cols; ++col) {
         VTermPos pos;
         pos.row = row;
         pos.col = col;
         VTermScreenCell cell;
         line[static_cast<size_t>(col)] = (vterm_screen_get_cell(m_screen, pos, &cell) > 0)
                                              ? toCell(cell, m_screen)
                                              : blankCell();
      }
      m_viewport.push_back(std::move(line));
   }
}

void TerminalSession::onOutput(const char* bytes, size_t len, void* user)
{
   static_cast<TerminalSession*>(user)->writeToPty(bytes, len);
}

int TerminalSession::onScrollbackPush(int cols, const VTermScreenCell* cells, void* user)
{
   auto* self = static_cast<TerminalSession*>(user);
   CellLine line(static_cast<size_t>(std::max(0, cols)));
   for (int col = 0; col < cols; ++col) {
      line[static_cast<size_t>(col)] = toCell(cells[col], self->m_screen);
   }
   self->m_scrollback.push_back(std::move(line));
   while (static_cast<int>(self->m_scrollback.size()) > kScrollbackLimit) {
      self->m_scrollback.pop_front();
   }
   // The view is following the bottom, which is where new output goes. Staying
   // put would hide the output the user just asked for.
   if (self->m_scrollOffset == 0) {
      self->rebuildViewport();
   }
   return 1;
}

int TerminalSession::onBell(void* user)
{
   static_cast<TerminalSession*>(user)->m_bell = true;
   return 1;
}

int TerminalSession::onSetTermProp(VTermProp prop, VTermValue* value, void* user)
{
   if (prop == VTERM_PROP_TITLE && value != nullptr && vterm_get_prop_type(prop) == VTERM_VALUETYPE_STRING &&
       value->string.str != nullptr) {
      static_cast<TerminalSession*>(user)->m_title.assign(value->string.str, value->string.len);
   }
   return 1;
}

int TerminalSession::onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void* user)
{
   (void)oldpos;
   auto* self = static_cast<TerminalSession*>(user);
   self->m_cursor = pos;
   self->m_cursorVisible = visible != 0;
   return 1;
}

} // namespace Terminal
} // namespace undoApp
