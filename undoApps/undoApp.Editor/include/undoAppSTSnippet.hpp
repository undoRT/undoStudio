/**
 * @file undoAppSTSnippet.hpp
 * @brief ST statement skeletons offered by the editor's suggestion list
 * @ingroup undoapps
 *
 * A snippet is the text an editor writes for you when you accept a suggestion:
 * typing IF and confirming it leaves the THEN, the body and the END_IF in place,
 * with the caret on the condition, instead of leaving three keywords to type by
 * hand. Everything here is free of any UI dependency, so which statement matches
 * a prefix, how a skeleton is indented and where the caret lands can be exercised
 * without a graphics context.
 *
 * The two kinds of snippet are kept apart by the pane they belong to: the
 * Variables pane holds VAR sections, the Body pane holds statements. A skeleton
 * is only ever offered in the pane it makes sense in, since a stray END_VAR in
 * the middle of a loop body is not a suggestion anybody wants to accept.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
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
 * The template is stored the way it reads in the language, with its own
 * indentation relative to the line the caret was on. Indenting a nested
 * skeleton therefore means indenting the template, not rewriting it.
 *
 * The caret position is stated relative to the template: `caretLine` counts
 * template lines from zero, and `caretColumn` counts glyphs into that line
 * *before* the base indentation is prepended. The placeholder itself is not
 * part of the text, so what the editor receives is a skeleton with the caret
 * already inside it.
 */
struct StatementSnippet
{
   std::string label;                ///< What the list shows, e.g. "IF ... END_IF"
   std::string detail;               ///< Dimmed right-hand column: what it is for
   std::string insert;               ///< Keyword written when the skeleton is accepted
   SnippetPane pane = SnippetPane::Body;
   std::vector<std::string> lines;   ///< Template lines, "\t" standing for one indent step
   int caretLine = 0;                ///< Template line the caret lands on
   int caretColumn = 0;              ///< Caret column, before the base indentation
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
 * @brief The word being typed at the caret
 *
 * A word is a run of characters that can be part of a keyword, which is what
 * separates a statement prefix from ordinary code: `IF` and `i` are prefixes,
 * while the empty span after `x.` is not, and neither is anything at all when
 * the caret sits in a comment or a string literal.
 *
 * @param line      The line the caret is on
 * @param column    Caret column, 0-based
 * @param startCol  Set to the first column of the word
 * @return The word left of the caret, empty when there is none
 */
std::string statementPrefixAt(const std::string& line, int column, int& startCol);

/**
 * @brief Whether the list is worth opening for what has been typed
 *
 * The gate is deliberately stricter than the matching that rankSnippets does.
 * Matching inside the list is a subsequence, so "end" finds every END_ and "wh"
 * finds WHILE, but a subsequence is also what "re" does to REPEAT while the user
 * is halfway through typing "reSetFlag" or "motore.re". The list only opens when
 * the word is a genuine prefix of a keyword, which is what leaves an identifier
 * that happens to start like a statement to the identifier list that belongs to
 * it.
 *
 * @param prefix What the user has typed
 * @return true when at least one keyword starts with it
 */
bool hasStatementPrefix(const std::string& prefix);

/**
 * @brief Keep the snippets matching a prefix, best match first
 *
 * Matching is a subsequence, not a prefix, so `fi` still finds `IF` and `wh`
 * finds `WHILE`. Best match is decided by where the match begins: a label that
 * opens with what was typed is about that word, and one that merely contains it
 * further along is not, which is what puts `END_IF` ahead of `IF ... END_IF`
 * once `end` has been typed. Snippets that score alike keep the order the table
 * is written in.
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
 * template line, and each "\t" in the template is widened to the editor's tab
 * size, so a skeleton inserted inside a loop comes out indented like the code
 * around it rather than flush with the margin.
 *
 * @param snippet    Skeleton to write
 * @param base       Indentation to insert under
 * @param tab        Text standing for one indent step
 * @param text       Set to the text to write, newlines included
 * @param caretLine  Set to the line the caret belongs on, 0-based in the text
 * @param caretCol   Set to the column it belongs at, 0-based in that line
 */
void expandSnippet(const StatementSnippet& snippet, const std::string& base, const std::string& tab, std::string& text,
                   int& caretLine, int& caretCol);

} // namespace ST
} // namespace undoApp
