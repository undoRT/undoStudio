/**
 * @file undoAppSTSnippet.cpp
 * @brief Statement skeletons offered by the editor's suggestion list
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoAppSTSnippet.hpp"

#include <algorithm>
#include <cctype>

namespace undoApp {
namespace ST {

namespace {

/// @brief Characters a statement prefix is made of
///
/// A keyword character run. A dot or a bracket would otherwise be swallowed into
/// the word along with the expression to the left of the caret.
bool isWordChar(char c)
{
   return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

/**
 * @brief The statement skeletons, in the order the list offers them
 *
 * The order is the order the language teaches: the constructs that nest, then
 * the keywords that close them, then the statements that leave a body. Ranking
 * reorders this, but only among what actually matched.
 */
std::vector<StatementSnippet> buildSnippets()
{
   std::vector<StatementSnippet> snippets;

   // ------------------------------------------------------------------
   // Body pane: control flow
   // ------------------------------------------------------------------
   snippets.push_back({"IF ... END_IF", "conditional", "IF", SnippetPane::Body,
                       {"IF  THEN", "\t", "END_IF"}, 0, 3});
   snippets.push_back({"IF / ELSE", "conditional with an alternative", "IF", SnippetPane::Body,
                       {"IF  THEN", "\t", "ELSE", "\t", "END_IF"}, 0, 3});
   // The caret sits where typing on completes the statement, so "FOR | := 0 TO
   // BY 1 DO" becomes "FOR i := 0 TO n BY 1 DO" and only the upper bound is left
   // over. The := is the IEC 61131-3 shape, where the control variable is
   // assigned rather than named.
   snippets.push_back({"FOR ... END_FOR", "counted loop", "FOR", SnippetPane::Body,
                       {"FOR  := 0 TO  BY 1 DO", "\t", "END_FOR"}, 0, 4});
   snippets.push_back({"WHILE ... END_WHILE", "loop on a condition", "WHILE", SnippetPane::Body,
                       {"WHILE  DO", "\t", "END_WHILE"}, 0, 6});
   snippets.push_back({"REPEAT ... END_REPEAT", "loop until a condition", "REPEAT", SnippetPane::Body,
                       {"REPEAT", "\t", "UNTIL ", "END_REPEAT"}, 2, 6});
   // A CASE branch is a label and a statement. The skeleton carries one branch in
   // that shape already: a bare ';' is a valid statement, so the branch parses
   // before the label is typed.
   snippets.push_back({"CASE ... END_CASE", "choice on a value", "CASE", SnippetPane::Body,
                       {"CASE  OF", "\t;", "ELSE", "\t;", "END_CASE"}, 0, 5});

   // Closing keywords, so a construct can be closed from either end. A body that
   // was pasted in still needs them one at a time.
   snippets.push_back({"ELSIF ... THEN", "another condition", "ELSIF", SnippetPane::Body,
                       {"ELSIF  THEN", "\t"}, 0, 6});
   snippets.push_back({"ELSE", "otherwise", "ELSE", SnippetPane::Body, {"ELSE", "\t"}, 0, 0});
   snippets.push_back({"END_IF", "close a conditional", "END_IF", SnippetPane::Body, {"END_IF"}, 0, 0});
   snippets.push_back({"END_FOR", "close a counted loop", "END_FOR", SnippetPane::Body, {"END_FOR"}, 0, 0});
   snippets.push_back({"END_WHILE", "close a conditional loop", "END_WHILE", SnippetPane::Body, {"END_WHILE"}, 0, 0});
   snippets.push_back({"UNTIL", "close the condition of a REPEAT", "UNTIL", SnippetPane::Body, {"UNTIL "}, 0, 6});
   snippets.push_back({"END_REPEAT", "close a REPEAT", "END_REPEAT", SnippetPane::Body, {"END_REPEAT"}, 0, 0});
   snippets.push_back({"END_CASE", "close a choice", "END_CASE", SnippetPane::Body, {"END_CASE"}, 0, 0});

   // ------------------------------------------------------------------
   // Body pane: leaving a body
   // ------------------------------------------------------------------
   snippets.push_back({"RETURN", "leave the POU or METHOD", "RETURN", SnippetPane::Body, {"RETURN;"}, 0, 7});
   snippets.push_back({"EXIT", "leave the innermost loop", "EXIT", SnippetPane::Body, {"EXIT;"}, 0, 5});
   snippets.push_back({"CONTINUE", "next iteration of the loop", "CONTINUE", SnippetPane::Body, {"CONTINUE;"}, 0, 9});

   // ------------------------------------------------------------------
   // Variables pane: the sections a POU declares things in
   //
   // The declaration line carries only the semicolon and the caret sits on the
   // name, so typing "x : INT" completes the line. Spelling out ": INT;" instead
   // would leave two blanks and one caret, and the second blank is the one nobody
   // goes looking for.
   // ------------------------------------------------------------------
   snippets.push_back({"VAR_INPUT", "values coming in", "VAR_INPUT", SnippetPane::Variables,
                       {"VAR_INPUT", "\t;", "END_VAR"}, 1, 1});
   snippets.push_back({"VAR_OUTPUT", "values going out", "VAR_OUTPUT", SnippetPane::Variables,
                       {"VAR_OUTPUT", "\t;", "END_VAR"}, 1, 1});
   snippets.push_back({"VAR_IN_OUT", "values both ways", "VAR_IN_OUT", SnippetPane::Variables,
                       {"VAR_IN_OUT", "\t;", "END_VAR"}, 1, 1});
   snippets.push_back({"VAR", "local state", "VAR", SnippetPane::Variables, {"VAR", "\t;", "END_VAR"}, 1, 1});
   snippets.push_back({"VAR CONSTANT", "named constant", "VAR CONSTANT", SnippetPane::Variables,
                       {"VAR CONSTANT", "\t;", "END_VAR"}, 1, 1});
   snippets.push_back({"VAR_GLOBAL", "shared with the rest of the POU", "VAR_GLOBAL", SnippetPane::Variables,
                       {"VAR_GLOBAL", "\t;", "END_VAR"}, 1, 1});
   snippets.push_back({"VAR_EXTERNAL", "declared elsewhere", "VAR_EXTERNAL", SnippetPane::Variables,
                       {"VAR_EXTERNAL", "\t;", "END_VAR"}, 1, 1});
   snippets.push_back({"VAR_TEMP", "scratch, never stored", "VAR_TEMP", SnippetPane::Variables,
                       {"VAR_TEMP", "\t;", "END_VAR"}, 1, 1});
   snippets.push_back({"END_VAR", "close a section", "END_VAR", SnippetPane::Variables, {"END_VAR"}, 0, 0});

   return snippets;
}

} // namespace

const std::vector<StatementSnippet>& allStatementSnippets()
{
   static const std::vector<StatementSnippet> snippets = buildSnippets();
   return snippets;
}

std::vector<StatementSnippet> snippetsForPane(SnippetPane pane)
{
   std::vector<StatementSnippet> result;
   for (const StatementSnippet& snippet : allStatementSnippets()) {
      if (snippet.pane == pane) {
         result.push_back(snippet);
      }
   }
   return result;
}

std::string statementPrefixAt(const std::string& line, int column, int& startCol)
{
   startCol = column;
   if (column <= 0 || static_cast<size_t>(column) > line.size()) {
      return {};
   }
   // The word has to end at the caret. A caret inside one is editing it, and
   // replacing "IF" with a skeleton there would leave it welded to what follows.
   if (static_cast<size_t>(column) < line.size() && isWordChar(line[static_cast<size_t>(column)])) {
      return {};
   }
   int start = column;
   while (start > 0 && isWordChar(line[static_cast<size_t>(start) - 1])) {
      --start;
   }
   startCol = start;
   return line.substr(static_cast<size_t>(start), static_cast<size_t>(column - start));
}

bool hasStatementPrefix(const std::string& prefix)
{
   if (prefix.empty()) {
      return false;
   }
   std::string needle = prefix;
   std::transform(needle.begin(), needle.end(), needle.begin(),
                  [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

   for (const StatementSnippet& snippet : allStatementSnippets()) {
      std::string keyword = snippet.insert;
      std::transform(keyword.begin(), keyword.end(), keyword.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      if (keyword.compare(0, needle.size(), needle) == 0) {
         return true;
      }
   }
   return false;
}

void rankSnippets(std::vector<StatementSnippet>& snippets, const std::string& prefix)
{
   // Case-insensitive, so "if" and "IF" ask the same question.
   std::string needle = prefix;
   std::transform(needle.begin(), needle.end(), needle.begin(),
                  [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

   // The score is where the match begins, not how close it is. That is what puts
   // END_IF ahead of IF ... END_IF once "end" has been typed, and what keeps the
   // whole-statement skeletons in front of the closing keywords while the prefix
   // is still "IF".
   std::vector<int> scores;
   scores.reserve(snippets.size());
   std::vector<StatementSnippet> kept;
   kept.reserve(snippets.size());

   for (StatementSnippet& snippet : snippets) {
      if (needle.empty()) {
         scores.push_back(0);
         kept.push_back(std::move(snippet));
         continue;
      }
      // A subsequence match rather than a prefix one, so that "fi" finds IF and
      // "wh" finds WHILE. `insert` is searched along with the label so that an
      // ellipsis in the label cannot hide the keyword actually written.
      std::string haystack = snippet.label;
      haystack += ' ';
      haystack += snippet.insert;
      std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

      size_t at = 0;
      int score = -1;
      bool matched = true;
      for (char c : needle) {
         const size_t found = haystack.find(c, at);
         if (found == std::string::npos) {
            matched = false;
            break;
         }
         if (score < 0) {
            score = static_cast<int>(found);
         }
         at = found + 1;
      }
      if (matched) {
         scores.push_back(score);
         kept.push_back(std::move(snippet));
      }
   }

   // Stable, so snippets that score alike keep the order the table gives them.
   std::vector<size_t> order(kept.size());
   for (size_t i = 0; i < order.size(); ++i) {
      order[i] = i;
   }
   std::stable_sort(order.begin(), order.end(),
                    [&scores](size_t a, size_t b) { return scores[a] < scores[b]; });

   std::vector<StatementSnippet> ranked;
   ranked.reserve(kept.size());
   for (size_t i : order) {
      ranked.push_back(std::move(kept[i]));
   }
   snippets = std::move(ranked);
}

std::string leadingWhitespace(const std::string& line)
{
   size_t i = 0;
   while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) {
      ++i;
   }
   return line.substr(0, i);
}

void expandSnippet(const StatementSnippet& snippet, const std::string& base, const std::string& tab, std::string& text,
                   int& caretLine, int& caretCol)
{
   text.clear();
   for (size_t i = 0; i < snippet.lines.size(); ++i) {
      if (i > 0) {
         text += '\n';
      }
      text += base;
      for (char c : snippet.lines[i]) {
         if (c == '\t') {
            text += tab;
         } else {
            text += c;
         }
      }
   }

   // The caret is stated against the template, where a "\t" is one character, and
   // is resolved here against the written text, where it is as wide as the editor
   // shows a tab. The two differ wherever the line starts with an indent step,
   // which is where the carets that matter are.
   caretLine = snippet.caretLine;
   caretCol = static_cast<int>(base.size());
   if (snippet.caretLine >= 0 && static_cast<size_t>(snippet.caretLine) < snippet.lines.size()) {
      const std::string& templateLine = snippet.lines[static_cast<size_t>(snippet.caretLine)];
      const int upto = std::min<int>(snippet.caretColumn, static_cast<int>(templateLine.size()));
      for (int i = 0; i < upto; ++i) {
         caretCol += (templateLine[static_cast<size_t>(i)] == '\t') ? static_cast<int>(tab.size()) : 1;
      }
   }
}

} // namespace ST
} // namespace undoApp
