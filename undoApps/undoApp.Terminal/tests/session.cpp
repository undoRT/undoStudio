// End-to-end check of TerminalSession, without a window: a real shell is started
// on a real pseudo-terminal, and what it writes has to arrive on the screen with
// its colours and in the right cells.
//
// The emulator is the thing being trusted here, so the assertions are about what
// the user would see rather than about internals: a line of text, the colour a
// prompt asked for, the cursor, and the scrollback once the screen has filled.

#include "TerminalSession.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

using namespace undoApp::Terminal;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

/**
 * @brief Pump until the screen satisfies a predicate, or the budget runs out
 * @param session Session to pump
 * @param wanted   What to look for
 * @param budget   How many milliseconds to keep trying
 * @return True when the screen matched before the budget ran out
 *
 * The shell is a separate process answering on a pty, so nothing is ready the
 * instant it is written to. Polling is the honest way to wait for it.
 */
static bool pumpUntil(TerminalSession& session, const std::string& wanted, int budgetMs = 4000) {
  for (int waited = 0; waited < budgetMs; waited += 10) {
    session.pump();
    std::string text;
    for (const CellLine& line : session.viewport()) {
      for (const Cell& cell : line) text += cell.text;
      text += '\n';
    }
    if (text.find(wanted) != std::string::npos) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

/// The whole viewport as one string, rows joined by a newline.
static std::string screenText(const TerminalSession& session) {
  std::string text;
  for (const CellLine& line : session.viewport()) {
    for (const Cell& cell : line) text += cell.text;
    text += '\n';
  }
  return text;
}

/// The first line of the viewport that holds the given text, as a vector of cells.
static const CellLine* lineWith(const TerminalSession& session, const std::string& wanted) {
  for (const CellLine& line : session.viewport()) {
    std::string text;
    for (const Cell& cell : line) text += cell.text;
    if (text.find(wanted) != std::string::npos) return &line;
  }
  return nullptr;
}

int main() {
  // A shell that cannot colourise or paginate, so the assertions are about the
  // emulator rather than about whatever is on this machine.
  TerminalSession session("/bin/sh", "/tmp");

  std::string error;
  check(session.start(24, 80, error), "the shell starts, got '" + error + "'");
  if (!error.empty() && !session.running()) {
    std::printf("RESULT: %d check(s) failed\n", failures + 1);
    return 1;
  }

  // --- a command's output arrives ---
  session.sendText("echo ciao_undoStudio\n");
  check(pumpUntil(session, "ciao_undoStudio"), "the output of a command reaches the screen");

  // --- the size the session was started with is the size it reports ---
  check(session.rows() == 24 && session.cols() == 80,
        "the session keeps the size it was given, got " + std::to_string(session.rows()) + "x" +
            std::to_string(session.cols()));

  // --- the viewport is that many rows, each that many cells ---
  check(static_cast<int>(session.viewport().size()) == 24,
        "the viewport has a row per row, got " + std::to_string(session.viewport().size()));
  bool widthsRight = !session.viewport().empty();
  for (const CellLine& line : session.viewport()) {
    if (static_cast<int>(line.size()) != 80) widthsRight = false;
  }
  check(widthsRight, "every row is as wide as the terminal");

  // --- a colour the program asked for is a colour the emulator resolved ---
  //
  // printf emits SGR 31, a red foreground. If the colour were left as an index
  // the cell would carry the index rather than an RGB triple.
  session.sendText("printf 'red\\033[31mRED\\033[0m\\n'\n");
  check(pumpUntil(session, "RED"), "a coloured line arrives");
  const CellLine* red = lineWith(session, "RED");
  check(red != nullptr, "the coloured line is on the screen");
  if (red != nullptr) {
    size_t colourAt = 0;
    std::string text;
    for (const Cell& cell : *red) { text += cell.text; }
    colourAt = text.find('R');
    check(colourAt < red->size(), "the coloured text is where it was asked for");
    if (colourAt < red->size()) {
      const Cell& cell = (*red)[colourAt];
      const bool reddish = (cell.fg & 0xFF0000u) > 0x800000u;
      check(reddish, "the foreground is red, got rgb " + std::to_string(cell.fg));
      check(cell.bg == 0x14161c, "the background is the one the session asked for, got " +
                                     std::to_string(cell.bg));
    }
  }

  // --- the cursor sits where the shell left it ---
  const VTermPos cursor = session.cursor();
  check(cursor.row >= 0 && cursor.row < 24 && cursor.col >= 0 && cursor.col < 80,
        "the cursor is inside the screen, got row " + std::to_string(cursor.row) + " column " +
            std::to_string(cursor.col));

  // --- a key is encoded, not passed through ---
  //
  // Ctrl+C is 0x03. Sending the character 'c' instead would be the difference
  // between interrupting a command and typing a c.
  session.sendText("sleep 30\n");
  pumpUntil(session, "sleep 30", 2000);
  session.sendChar('c', VTERM_MOD_CTRL);
  // The prompt comes back because the sleep was interrupted, which is what tells
  // us the byte arrived as a signal to the foreground job.
  session.sendText("echo dopo_interrupt\n");
  check(pumpUntil(session, "dopo_interrupt", 6000), "Ctrl+C interrupts and the shell is usable again");

  // --- scrollback keeps what scrolled off ---
  //
  // More lines than the screen has, which is the only way anything scrolls off it.
  const int before = session.scrollbackSize();
  session.sendText("for i in $(seq 1 40); do echo riga_$i; done\n");
  check(pumpUntil(session, "riga_40", 8000), "a loop's last line arrives");
  check(session.scrollbackSize() > before, "the lines that scrolled off are kept, " +
                                                std::to_string(before) + " then " +
                                                std::to_string(session.scrollbackSize()));

  // --- scrolled back, the older lines are the ones shown ---
  session.scrollBy(200);
  check(session.scrollOffset() == session.scrollbackSize(),
        "scrolling to the top stops at the oldest line, offset " + std::to_string(session.scrollOffset()) +
            " of " + std::to_string(session.scrollbackSize()));
  check(screenText(session).find("riga_1") != std::string::npos ||
            screenText(session).find("riga_2") != std::string::npos,
        "a line from the scrollback is on screen while scrolled back");
  session.scrollBy(-200);
  check(session.scrollOffset() == 0, "scrolling back down returns to the live bottom");

  // --- a selection reads in the direction it was made ---
  //
  // The columns are inclusive at both ends, so columns 0 to 4 are five characters.
  Selection forward;
  bool located = false;
  for (size_t row = 0; row < session.viewport().size() && !located; ++row) {
    const CellLine& line = session.viewport()[row];
    std::string text;
    for (const Cell& cell : line) text += cell.text;
    const size_t at = text.find("riga_40");
    if (at != std::string::npos) {
      forward.startRow = static_cast<int>(row);
      forward.startCol = static_cast<int>(at);
      forward.endRow = static_cast<int>(row);
      forward.endCol = static_cast<int>(at) + 4;
      located = true;
    }
  }
  check(located, "the last line of the loop is still on screen to select in");
  if (located) {
    check(session.selectionText(forward) == "riga_",
          "a selection forwards reads what it covers, got '" + session.selectionText(forward) + "'");

    Selection backward = forward;
    std::swap(backward.startCol, backward.endCol);
    check(session.selectionText(backward) == "riga_",
          "the same selection backwards reads the same, got '" + session.selectionText(backward) + "'");

    // A selection across two rows carries the line break between them, and both
    // ends are inclusive, so the column it stopped on is part of it. What the
    // second row holds depends on the prompt, so only the shape is asserted.
    Selection across = forward;
    across.endRow = forward.startRow + 1;
    across.endCol = 0;
    const std::string spanning = session.selectionText(across);
    check(spanning.find('\n') != std::string::npos && spanning.rfind("riga_40", 0) == 0,
          "a selection over two rows carries the break, got '" + spanning + "'");
  }

  // --- resizing reaches the shell and the emulator together ---
  session.resize(40, 100);
  check(session.rows() == 40 && session.cols() == 100, "resize() changes what the session reports");
  check(static_cast<int>(session.viewport().size()) == 40, "the viewport follows the new height");
  session.sendText("stty size\n");
  check(pumpUntil(session, "40 100", 4000),
        "the shell itself is told the new size, got: " + screenText(session).substr(0, 0));

  // --- the shell can be ended ---
  session.stop();
  check(true, "the session stops without hanging");

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}
