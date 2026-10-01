/**
 * @file undoAppSTSnippet.hpp
 * @brief Statement skeletons offered by the editor's suggestion list
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * A snippet is the text the editor writes out when a suggestion is accepted:
 * typing IF and accepting it leaves the THEN, the body and the END_IF in place,
 * with the caret on the condition, instead of leaving three keywords to type.
 *
 * Skeletons are offered per pane. The Variables pane holds the VAR sections and
 * the Body pane holds the cyclic code, so a FOR loop is not offered among the
 * declarations and a VAR_INPUT is not offered in the middle of a loop.
 *
 * The mapping from a typed word to a skeleton, and the writing of a skeleton at a
 * line, are kept free of any UI dependency so they can be exercised without a
 * graphics context.
 */

#pragma once

#include <string>
#include <vector>

namespace undoApp {
namespace ST {

/**
 * @brief The pane a snippet is written into
 */
enum class SnippetPane
{
   Variables, ///< The Variables pane, which holds the VAR sections
   Body       ///< The Body pane, which holds the cyclic code
};

/**
 * @brief One statement skeleton the suggestion list can write out
 *
 * The template is stored the way the statement reads, indented relative to the
 * line the caret is on, so a nested skeleton is a matter of indenting the
 * template rather than rewriting it. A "\t" in the template stands for one
 * indent step and is widened to the editor's tab size on the way out.
 *
 * The caret is stated against the template: caretLine counts template lines from
 * zero, and caretColumn counts glyphs into that line before the base indentation
 * is prepended. There is no placeholder character in the written text; the caret
 * is placed inside it.
 */
struct StatementSnippet
{
   std::string label;              ///< What the list shows, e.g. "IF ... END_IF"
   std::string detail;             ///< Dimmed right-hand column: what it is for
   std::string insert;             ///< Keyword written when the skeleton is accepted
   SnippetPane pane = SnippetPane::Body;
   std::vector<std::string> lines; ///< Template lines, "\t" for one indent step
   int caretLine = 0;              ///< Template line the caret lands on
   int caretColumn = 0;            ///< Caret column, before the base indentation
};

/**
 * @brief The statements on offer
 * @return Every snippet, in the order the list shows them
 */
const std::vector<StatementSnippet>& allStatementSnippets();

/**
 * @brief The snippets belonging to one pane
 * @param pane Pane the caret is in
 * @return The subset offered there, in list order
 */
std::vector<StatementSnippet> snippetsForPane(SnippetPane pane);

/**
 * @brief Whether the list is worth opening for what has been typed
 *
 * The gate is stricter than the matching rankSnippets does. Matching inside the
 * list is a subsequence, so "end" finds every END_ and "wh" finds WHILE, but
 * "re" is also a subsequence match for REPEAT while the user is halfway through
 * "reSetFlag" or "motore.re". The list only opens when the word is a genuine
 * prefix of a keyword, which leaves an identifier that happens to begin like a
 * statement to the identifier list.
 *
 * @param prefix What the user has typed
 * @return true when at least one keyword starts with it
 */
bool hasStatementPrefix(const std::string& prefix);

/**
 * @brief The word being typed at the caret
 * @param line     The line the caret is on
 * @param column   Caret column, 0-based
 * @param startCol Set to the first column of the word
 * @return The word left of the caret, empty when there is none
 *
 * The word has to end at the caret. A caret inside one is editing it, and
 * replacing "IF" with a skeleton there would leave "IF" welded to what followed.
 */
std::string statementPrefixAt(const std::string& line, int column, int& startCol);

/**
 * @brief Keep the snippets matching a prefix, best match first
 *
 * Matching is a subsequence, not a prefix, so "fi" still finds IF and "wh"
 * finds WHILE. Best match is where the match begins: a label opening with what
 * was typed is about that word, one that only contains it further along is not.
 * That is what puts END_IF ahead of IF ... END_IF once "end" has been typed.
 * Snippets that score alike keep the order the table is written in.
 *
 * @param snippets In/out list, reordered in place
 * @param prefix   What the user has typed
 */
void rankSnippets(std::vector<StatementSnippet>& snippets, const std::string& prefix);

/**
 * @brief Indentation of a line, spaces and tabs alike
 * @param line Line to measure
 * @return The leading run of blanks
 */
std::string leadingWhitespace(const std::string& line);

/**
 * @brief Write a snippet out at a line, in the indentation already in force there
 *
 * The base indentation of the line the caret is on is prepended to every
 * template line, so a skeleton accepted inside a loop comes out indented like
 * the code around it. The caret is resolved against the written text, where an
 * indent step is as wide as the editor shows a tab.
 *
 * @param snippet   Skeleton to write
 * @param base      Indentation to insert under
 * @param tab       Text standing for one indent step
 * @param text      Set to the text to write, newlines included
 * @param caretLine Set to the line the caret belongs on, 0-based in the text
 * @param caretCol  Set to the column it belongs at, 0-based in that line
 */
void expandSnippet(const StatementSnippet& snippet, const std::string& base, const std::string& tab, std::string& text,
                   int& caretLine, int& caretCol);

} // namespace ST
} // namespace undoApp
