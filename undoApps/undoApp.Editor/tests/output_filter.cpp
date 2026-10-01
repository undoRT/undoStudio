/**
 * @file output_filter.cpp
 * @brief Which lines of the ST Output panel are shown, and what the counts beside the switches say
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The panel holds three kinds of line in one list, and the generated ST dumped in
// full for debugging is hundreds of lines for a file of any size. Hiding it is
// the whole point of the filter, so what is checked here is that hiding it leaves
// the diagnostics reachable, and that the numbers next to the switches still tell
// the truth about what is behind them.
//
// The count is the part that is easy to get wrong and hard to notice. Counting
// what already passed the filter makes every hidden line read as zero, so the
// switch that says "0 info" is the one line nobody opens, and the two errors
// under it never get looked at.

#include <cstdio>
#include <string>
#include <vector>

#include "undoAppSTOutput.hpp"

using undoApp::ST::OutSeverity;
using undoApp::ST::OutputFilter;
using undoApp::ST::OutputLine;
using undoApp::ST::classifyCompilerLine;
using undoApp::ST::searchOutput;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

static std::vector<OutputLine> sample() {
   return {
      {OutSeverity::Error, "  line 12: undeclared identifier 'counter'"},
      {OutSeverity::Error, "  line 30: type mismatch"},
      {OutSeverity::Warning, "  unused variable 'tmp'"},
      {OutSeverity::Success, "Parsing successful: 3 POU(s) found"},
      {OutSeverity::Info, "// Generated ST file:"},
      {OutSeverity::Info, "//   PROGRAM Main"},
      {OutSeverity::Info, "// --- END ---"},
   };
}

int main() {
   const std::vector<OutputLine> lines = sample();

   // --- nothing is hidden to begin with ---
   OutputFilter all;
   check(all.visible(lines).size() == lines.size(),
         "every line is shown to begin with, got " + std::to_string(all.visible(lines).size()));

   // --- hiding the generated dump leaves the diagnostics ---
   //
   // This is the case that matters: the dump is the bulk, and the two errors are
   // the reason the panel is open.
   all.showInfo = false;
   {
      const std::vector<OutputLine> shown = all.visible(lines);
      size_t errors = 0;
      for (const OutputLine& line : shown) {
         if (line.severity == OutSeverity::Error) ++errors;
      }
      check(errors == 2, "hiding the dump leaves both errors, got " + std::to_string(errors));
      check(shown.size() == 4, "and leaves only the four lines that are not the dump, got " +
             std::to_string(shown.size()));
      bool dumpGone = true;
      for (const OutputLine& line : shown) {
         if (line.text.find("Generated ST") != std::string::npos) {
            dumpGone = false;
         }
      }
      check(dumpGone, "and the dump itself is gone");
   }

   // --- the counts tell the truth about what is behind each switch ---
   //
   // Counted from the whole list, not from what is already showing. This is the
   // bug that makes the filter useless while looking correct.
   check(OutputFilter::count(lines, OutSeverity::Error) == 2, "two errors in the list, got " +
         std::to_string(OutputFilter::count(lines, OutSeverity::Error)));
   check(OutputFilter::count(lines, OutSeverity::Warning) == 1, "one warning, got " +
         std::to_string(OutputFilter::count(lines, OutSeverity::Warning)));
   check(OutputFilter::count(lines, OutSeverity::Success) == 1, "one success, got " +
         std::to_string(OutputFilter::count(lines, OutSeverity::Success)));
   check(OutputFilter::count(lines, OutSeverity::Info) == 3, "three info lines, got " +
         std::to_string(OutputFilter::count(lines, OutSeverity::Info)));
   {
      // With info already hidden, its switch must still say three.
      OutputFilter hidingInfo;
      hidingInfo.showInfo = false;
      check(hidingInfo.visible(lines).size() == 4 &&
               OutputFilter::count(lines, OutSeverity::Info) == 3,
            "a hidden line is still counted beside its own switch");
   }

   // --- errors only, which is what one wants after a failed build ---
   OutputFilter errorsOnly;
   errorsOnly.showWarnings = false;
   errorsOnly.showSuccess = false;
   errorsOnly.showInfo = false;
   {
      const std::vector<OutputLine> shown = errorsOnly.visible(lines);
      check(shown.size() == 2, "errors and nothing else, got " + std::to_string(shown.size()));
      check(shown.size() == 2 && shown[0].severity == OutSeverity::Error,
            "and what is left are errors");
   }

   // --- everything off leaves nothing, rather than everything ---
   OutputFilter none;
   none.showErrors = none.showWarnings = none.showSuccess = none.showInfo = false;
   check(none.visible(lines).empty(), "turning everything off leaves nothing, got " +
         std::to_string(none.visible(lines).size()));
   check(none.passes(OutSeverity::Error) == false,
         "and a hidden severity does not pass, even the blocking one");

   // --- the order is the order they were written in ---
   //
   // Checked against the input with the hidden lines taken out by hand, rather
   // than by comparing severity values: the list is written diagnostics first and
   // detail last, so the severities descend and any comparison of them would be
   // checking the shape of the enum instead of what filtering did.
   {
      const std::vector<OutputLine> shown = all.visible(lines);
      std::vector<OutputLine> expected;
      for (const OutputLine& line : lines) {
         if (line.severity != OutSeverity::Info) {
            expected.push_back(line);
         }
      }
      check(shown.size() == expected.size(), "the same number of lines as the input minus the hidden ones");
      bool sameOrder = shown.size() == expected.size();
      for (size_t i = 0; sameOrder && i < shown.size(); ++i) {
         if (shown[i].text != expected[i].text) {
            sameOrder = false;
         }
      }
      check(sameOrder, "filtering keeps the order of what is left");
   }

   // --- an empty list is not a crash and is not "everything" ---
   check(all.visible({}).empty(), "an empty list stays empty");
   check(OutputFilter::count({}, OutSeverity::Error) == 0, "and counts nothing");

   // --- search ---
   {
      const std::vector<OutputLine> found = searchOutput(lines, "identifier");
      check(found.size() == 1, "searching finds the one line it names, got " +
            std::to_string(found.size()));
      check(searchOutput(lines, "").size() == lines.size(),
            "an empty search returns everything rather than nothing");
      check(searchOutput(lines, "//").size() == 3, "searching the dump finds its three lines, got " +
            std::to_string(searchOutput(lines, "//").size()));
      check(searchOutput(lines, "NOT THERE").empty(), "a search that matches nothing returns nothing");
      // The case that matters most: finding an error without reading past the dump.
      const std::vector<OutputLine> errs = searchOutput(lines, "counter");
      check(errs.size() == 1 && errs[0].severity == OutSeverity::Error,
            "searching reaches an error buried under the dump");
   }

   // --- how a line the transpiler printed is coloured ---
   //
   // The formats here are the ones st2cpp actually writes, taken from
   // Diagnostics::print and printSummary rather than invented, because a test
   // against a guessed format would pass while the real one stayed broken.
   //
   // The tally is the case that was wrong: st2cpp ends its diagnostics with a
   // count of what it just said, the word "error" is in that line, and taking it
   // at its word marked a run with no errors at all as an error.
   struct CompilerCase
   {
      const char* line;
      OutSeverity expected;
      const char* what;
   };
   const CompilerCase cases[] = {
      // The tallies, in the two shapes st2cpp writes them.
      {"3 error(s), 2 warning(s) generated.", OutSeverity::Info,
       "a tally of errors and warnings is not itself an error"},
      {"Semantic analysis: 0 errors, 3 warnings", OutSeverity::Info,
       "a tally naming zero errors is not an error"},
      {"Semantic analysis: 2 errors, 0 warnings", OutSeverity::Info,
       "a tally naming two errors is not an error either, they are listed above it"},
      {"1 error generated.", OutSeverity::Info, "a tally of one error is not an error"},
      {"5 warnings generated.", OutSeverity::Info, "a tally of warnings is not a warning"},
      {"0 warnings generated.", OutSeverity::Info, "a tally of nothing is not anything"},

      // The diagnostics themselves, in st2cpp's header format.
      {"main.st:12:5: error: undeclared identifier 'counter' [E001]", OutSeverity::Error,
       "a diagnostic header is an error"},
      {"main.st:3:1: warning: unused variable 'tmp' [W004]", OutSeverity::Warning,
       "a diagnostic header is a warning"},
      {"main.st:9:2: note: consider renaming [N002]", OutSeverity::Info,
       "a note is neither"},

      // The source snippet under a diagnostic is context, not a finding.
      {"  12 |   counter := 1;", OutSeverity::Info, "a source snippet is neither"},
      {"     |     ^", OutSeverity::Info, "a caret is neither"},

      // A number next to a position must not be read as a tally.
      {"main.st:42:1: error: something is wrong [E009]", OutSeverity::Error,
       "a line number in a diagnostic header is not a count"},

      // What the compiler narrates.
      {"st2cpp: /usr/local/bin/st2cpp", OutSeverity::Info, "the command it found is neither"},
      {"Generating project.st.c...", OutSeverity::Info, "progress is neither"},
   };

   for (const CompilerCase& c : cases) {
      check(classifyCompilerLine(c.line) == c.expected, c.what);
   }

   // A file called errors.st, mentioned in a line about something else, is not a
   // failure: this is what matching the bare word anywhere would have got wrong.
   check(classifyCompilerLine("Reading errors.st ...") == OutSeverity::Info,
         "a file whose name contains 'error' is not an error");
   check(classifyCompilerLine("") == OutSeverity::Info, "an empty line is neither");

   // And the tally rule does not swallow a real error that carries a number in
   // its message, because a tally needs the number immediately before the word.
   check(classifyCompilerLine("main.st:5:1: error: 3 variables are undeclared [E010]") == OutSeverity::Error,
         "a number inside a message does not make it a tally");

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}