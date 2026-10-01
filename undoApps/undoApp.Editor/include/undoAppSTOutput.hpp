/**
 * @file undoAppSTOutput.hpp
 * @brief Deciding which lines of the ST Output panel are shown.
 * @ingroup undoAppEditor
 *
 * @author undoStudio
 * @date 2026
 * @copyright SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The Output panel holds three kinds of thing in one list: diagnostics, the
 * confirmations that a step ran, and the generated ST dumped in full for
 * debugging. The third is hundreds of lines for a file of any size, and it is
 * what the panel was mostly showing, so the two errors that were actually worth
 * reading arrived somewhere under it.
 *
 * The filtering is here, apart from the panel that draws it, because this is the
 * part with the decisions in it and the part worth checking: what is counted is
 * what is hidden, a count taken after filtering counts the wrong lines, and a
 * panel that says "0 errors" while hiding them is worse than one that says
 * nothing.
 */

#ifndef UNDOSTUDIO_UNDOAPP_STOUTPUTFILTER_HPP
#define UNDOSTUDIO_UNDOAPP_STOUTPUTFILTER_HPP

#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace undoApp {
namespace ST {

namespace detail {

/// @brief A lower-case copy, for matching words without caring about their case
inline std::string toLowerText(const std::string& text)
{
   std::string lower = text;
   for (char& c : lower) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
   }
   return lower;
}

/// @brief Whether a string begins with a prefix
inline bool startsWith(const std::string& text, const std::string& prefix)
{
   return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

} // namespace detail

/**
 * @brief Severity of a line in the Output panel
 */
enum class OutSeverity
{
   Info,    ///< Debug/detail output, and the generated ST dump
   Success, ///< Confirmation that a step succeeded
   Warning, ///< Reported by the analyzer but not blocking
   Error    ///< Blocks code generation
};

/**
 * @brief One tagged line in the Output panel
 *
 * Severity is carried as data rather than recovered from the text: matching on
 * substrings silently missed lowercase variants such as "errors found".
 */
struct OutputLine
{
   OutSeverity severity = OutSeverity::Info;
   std::string text;
};

/**
 * @brief Which severities the Output panel shows
 *
 * All are on to begin with, so the panel shows everything it used to. Turning the
 * generated dump off is a separate switch on the panel rather than this one,
 * because it is the one thing a user wants hidden by default and the one they
 * want back when something looks wrong.
 */
struct OutputFilter
{
   bool showErrors = true;
   bool showWarnings = true;
   bool showSuccess = true;
   bool showInfo = true;

   /// @brief Whether a line of this severity is shown
   bool passes(OutSeverity severity) const
   {
      switch (severity) {
      case OutSeverity::Error: return showErrors;
      case OutSeverity::Warning: return showWarnings;
      case OutSeverity::Success: return showSuccess;
      case OutSeverity::Info: return showInfo;
      }
      return true;
   }

   /**
    * @brief The lines to draw, in the order they were added
    *
    * A copy, because the panel draws from it across frames while the list it came
    * from keeps growing, and holding a reference to that would mean re-checking
    * every iterator the moment a validation adds a line.
    */
   std::vector<OutputLine> visible(const std::vector<OutputLine>& lines) const
   {
      std::vector<OutputLine> kept;
      kept.reserve(lines.size());
      for (const OutputLine& line : lines) {
         if (passes(line.severity)) {
            kept.push_back(line);
         }
      }
      return kept;
   }

   /// @brief How many lines of a severity the list holds, filtered or not
   ///
   /// Counted from the whole list, never from what survived the filter: the number
   /// beside a switch is there to say what turning it on would show, so counting
   /// the already-hidden lines would leave every switch reading zero.
   static size_t count(const std::vector<OutputLine>& lines, OutSeverity severity)
   {
      size_t total = 0;
      for (const OutputLine& line : lines) {
         if (line.severity == severity) {
            ++total;
         }
      }
      return total;
   }
};

/**
 * @brief The lines matching a substring, for the panel's search box
 *
 * Empty or whitespace-only text matches everything, so clearing the box restores
 * the list without a second way to say "no search".
 */
inline std::vector<OutputLine> searchOutput(const std::vector<OutputLine>& lines, const std::string& needle)
{
   if (needle.empty()) {
      return lines;
   }
   std::vector<OutputLine> found;
   for (const OutputLine& line : lines) {
      if (line.text.find(needle) != std::string::npos) {
         found.push_back(line);
      }
   }
   return found;
}

/**
 * @brief Whether a line from the transpiler is a tally rather than a diagnostic
 *
 * st2cpp closes its diagnostics with a count of what it just reported:
 *   3 error(s), 2 warning(s) generated.
 *   Semantic analysis: 0 errors, 3 warnings
 * The word "error" is in those lines, and taking it at its word marks a summary
 * of zero errors as an error. What tells them apart from a real diagnostic is
 * that a number sits immediately in front of the word: a diagnostic names its
 * position instead, as "file.st:12:5: error:", where a colon comes between.
 */
inline bool isCountSummary(const std::string& line)
{
   for (size_t i = 0; i < line.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(line[i]))) {
         continue;
      }
      size_t after = i;
      while (after < line.size() && std::isdigit(static_cast<unsigned char>(line[after]))) {
         ++after;
      }
      size_t word = after;
      while (word < line.size() && line[word] == ' ') {
         ++word;
      }
      const std::string tail = detail::toLowerText(line.substr(word, 12));
      if (detail::startsWith(tail, "error") || detail::startsWith(tail, "warning")) {
         return true;
      }
      i = after - 1;
   }
   return false;
}

/**
 * @brief How a line the transpiler printed should be coloured in the panel
 *
 * The transpiler writes to standard error in a fixed shape, which is what this
 * reads. Matching the bare word "error" anywhere in the line is what marked a
 * count of zero errors as an error, and what would mark a file called errors.st
 * as a failure.
 *
 * A tally is Info: it reports what the lines above it already said, each of which
 * is coloured on its own, so colouring the summary as well puts one more red line
 * in among the red ones and hides the ones that matter.
 */
inline OutSeverity classifyCompilerLine(const std::string& line)
{
   if (isCountSummary(line)) {
      return OutSeverity::Info;
   }

   const std::string lower = detail::toLowerText(line);

   // "file.st:12:5: error: message", or a line that opens with the word.
   for (const char* marker : {": error", ": warning", "error:", "warning:"}) {
      if (lower.find(marker) != std::string::npos) {
         return (marker[0] == 'e' || marker == std::string(": error")) ? OutSeverity::Error
                                                                      : OutSeverity::Warning;
      }
   }

   return OutSeverity::Info;
}

} // namespace ST
} // namespace undoApp

#endif // UNDOSTUDIO_UNDOAPP_STOUTPUTFILTER_HPP
