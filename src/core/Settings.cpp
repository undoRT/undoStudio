/**
 * @file Settings.cpp
 * @brief Reading and writing the IDE's own state files
 * @ingroup core
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoStudio/core/Settings.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <vector>

namespace undoStudio {
namespace core {
namespace settings {

const char* const kCoreFile = "undoStudio.ini";

namespace {

/// @brief One line of a state file
struct Line
{
   std::string section;
   std::string key;
   std::string value;
   std::string raw;            ///< The line as it was, for a comment or a blank
   bool hasSection = false;    ///< False for a key that belongs to the section above
   bool isComment = false;     ///< Blank or starting with '#'
};

/// @brief Trim the blanks off both ends of a string
std::string trimmed(const std::string& text)
{
   size_t first = 0;
   while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) {
      ++first;
   }
   size_t last = text.size();
   while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1]))) {
      --last;
   }
   return text.substr(first, last - first);
}

/// @brief Split a file into lines, remembering which section each belongs to
std::vector<Line> readLines(const std::string& file)
{
   std::vector<Line> lines;
   std::ifstream in(file);
   if (!in.good()) {
      return lines;
   }
   std::string section;
   std::string raw;
   while (std::getline(in, raw)) {
      if (!raw.empty() && raw.back() == '\r') {
         raw.pop_back();
      }
      const std::string text = trimmed(raw);
      if (text.empty() || text[0] == '#') {
         // Kept as it was. These files are the IDE's, but a user who has opened one
         // to see what a value means will not thank it for losing the note.
         lines.push_back({"", "", "", raw, false, true});
         continue;
      }
      if (text.front() == '[' && text.back() == ']') {
         section = text.substr(1, text.size() - 2);
         lines.push_back({section, "", "", text, true, false});
         continue;
      }
      const size_t equals = text.find('=');
      if (equals == std::string::npos) {
         lines.push_back({"", "", "", raw, false, true});
         continue;
      }
      lines.push_back({section, trimmed(text.substr(0, equals)), text.substr(equals + 1), text, false, false});
   }
   return lines;
}

/// @brief Write the lines back out, a missing file being an empty list
void writeLines(const std::string& file, const std::vector<Line>& lines)
{
   std::ofstream out(file, std::ios::trunc);
   if (!out.good()) {
      return;
   }
   for (const Line& line : lines) {
      if (line.isComment) {
         out << line.raw << "\n";
      } else if (line.hasSection) {
         out << "[" << line.section << "]\n";
      } else {
         out << line.key << "=" << line.value << "\n";
      }
   }
}

} // namespace

std::string getString(const std::string& file, const std::string& section, const std::string& key,
                      const std::string& def)
{
   for (const Line& line : readLines(file)) {
      if (!line.hasSection && line.section == section && line.key == key) {
         return line.value;
      }
   }
   return def;
}

int getInt(const std::string& file, const std::string& section, const std::string& key, int def)
{
   const std::string raw = getString(file, section, key);
   if (raw.empty()) {
      return def;
   }
   try {
      size_t read = 0;
      const int value = std::stoi(raw, &read);
      // A value with something after the number is not a number. std::stoi stops
      // at the first character it cannot use and says how far it got, which is
      // the only way to tell "16" from "16px".
      return (read == raw.size()) ? value : def;
   } catch (const std::exception&) {
      return def;
   }
}

std::vector<std::string> getList(const std::string& file, const std::string& section, const std::string& key)
{
   std::vector<std::string> values;
   for (const Line& line : readLines(file)) {
      if (!line.hasSection && line.section == section && line.key == key) {
         values.push_back(line.value);
      }
   }
   return values;
}

void setString(const std::string& file, const std::string& section, const std::string& key, const std::string& value)
{
   std::vector<Line> lines = readLines(file);

   bool sectionSeen = false;
   bool written = false;
   size_t sectionStart = lines.size();
   size_t sectionEnd = lines.size();
   for (size_t i = 0; i < lines.size(); ++i) {
      if (lines[i].hasSection) {
         if (lines[i].section == section && !sectionSeen) {
            sectionSeen = true;
            sectionStart = i;
         }
         // The section runs up to the next header, so a new key can be put at the
         // end of it and leave what is already in there, comments included, where
         // the user put it.
         if (sectionSeen && !lines[i].section.empty() && i > sectionStart) {
            sectionEnd = i;
         }
         continue;
      }
      if (sectionSeen && lines[i].key == key) {
         // First occurrence wins: a key written twice would otherwise be read
         // from the first, so the second would look like it had not saved.
         lines[i].value = value;
         written = true;
         break;
      }
   }
   if (!written) {
      if (!sectionSeen) {
         if (!lines.empty() && !lines.back().isComment) {
            lines.push_back({"", "", "", "", false, true}); // a blank line to separate
         }
         sectionStart = lines.size();
         sectionEnd = lines.size();
         lines.push_back({section, "", "", "[" + section + "]", true, false});
         ++sectionEnd;
      }
      lines.insert(lines.begin() + static_cast<long>(sectionEnd), Line{section, key, value, key + "=" + value, false, false});
   }
   writeLines(file, lines);
}

void setInt(const std::string& file, const std::string& section, const std::string& key, int value)
{
   setString(file, section, key, std::to_string(value));
}

void setList(const std::string& file, const std::string& section, const std::string& key,
             const std::vector<std::string>& values)
{
   const std::vector<Line> lines = readLines(file);

   // Everything with this key goes, wherever it is in the section, and the new
   // values go in at the end of the section. Removing and reinserting the list in
   // one pass rather than rewriting the whole section, so that a comment the user
   // wrote in their own state file is still there afterwards.
   bool sectionSeen = false;
   size_t sectionEnd = lines.size();
   size_t insertAt = lines.size();
   std::vector<Line> kept;
   for (size_t i = 0; i < lines.size(); ++i) {
      const Line& line = lines[i];
      if (line.hasSection) {
         if (line.section == section && !sectionSeen) {
            sectionSeen = true;
         }
         if (sectionSeen && !line.section.empty() && i > 0 && !kept.empty() &&
             kept.back().section != line.section) {
            sectionEnd = kept.size();
         }
         kept.push_back(line);
         continue;
      }
      if (sectionSeen && line.key == key) {
         if (insertAt == lines.size()) {
            insertAt = kept.size();
            sectionEnd = kept.size();
         }
         continue; // dropped: the whole list is written back below
      }
      kept.push_back(line);
   }
   if (!sectionSeen) {
      if (!kept.empty() && !kept.back().isComment) {
         kept.push_back({"", "", "", "", false, true});
      }
      kept.push_back({section, "", "", "[" + section + "]", true, false});
      insertAt = kept.size();
      sectionEnd = kept.size();
   }
   for (size_t i = values.size(); i > 0; --i) {
      kept.insert(kept.begin() + static_cast<long>(sectionEnd),
                  Line{section, key, values[i - 1], key + "=" + values[i - 1], false, false});
   }
   (void)insertAt;
   writeLines(file, kept);
}

} // namespace settings
} // namespace core
} // namespace undoStudio
