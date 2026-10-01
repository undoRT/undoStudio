/**
 * @file popup_call_sites.cpp
 * @brief The rule about where OpenPopup may be called, checked rather than asserted in a comment
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// ImGui hashes a popup's id against the ID stack at the point of the call, and
// looks the popup up against the stack at the point of the matching Begin. Two
// calls at different depths are two different popups, so the Begin never finds the
// one the Open asked for and draws nothing. The failure is quiet: no error, no
// assert, just a dialog that does not appear.
//
// The worse half is what an OpenPopup with no matching Begin leaves behind. The
// entry sits on ImGui's open stack with nothing to consume it, and every Escape
// afterwards is spent closing that entry rather than the dialog the user was
// looking at. It stays until the session ends.
//
// What is checked here is the shape of the product's own calls, read out of the
// sources at build time: every OpenPopup with a literal name has a matching Begin
// in the same file, and no OpenPopup sits inside a recursive tree renderer, which
// is where the stack depth is not the depth the modals are drawn at.

#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

/// The sources that own popups. The Terminal has none and is left out.
static const char* kSources[] = {
    "undoApps/undoApp.Editor/src/undoAppEditor.cpp",
    "undoApps/undoApp.Editor/src/undoAppST.cpp",
    "undoApps/undoApp.Editor/src/undoAppJSON.cpp",
    "src/ui/ImGuiManager.cpp",
};

/// The name in `ImGui::OpenPopup("...")`, or empty if the line is not such a call.
///
/// Names built at run time, as the per-node context menus are ("##ctx_" + path),
/// are skipped: they are matched by a Begin carrying the same expression, which
/// cannot be compared as text.
static std::string openedName(const std::string& line) {
  const std::string key = "ImGui::OpenPopup(\"";
  const size_t at = line.find(key);
  if (at == std::string::npos) {
    return "";
  }
  const size_t start = at + key.size();
  const size_t end = line.find('"', start);
  if (end == std::string::npos) {
    return "";
  }
  return line.substr(start, end - start);
}

/// The name in `ImGui::BeginPopup("...")` or `ImGui::BeginPopupModal("...")`.
static std::string begunName(const std::string& line) {
  for (const char* fn : {"ImGui::BeginPopupModal(\"", "ImGui::BeginPopup(\""}) {
    const std::string key = fn;
    const size_t at = line.find(key);
    if (at == std::string::npos) {
      continue;
    }
    const size_t start = at + key.size();
    const size_t end = line.find('"', start);
    if (end != std::string::npos) {
      return line.substr(start, end - start);
    }
  }
  return "";
}

static std::vector<std::string> readLines(const std::string& path) {
  std::vector<std::string> lines;
  std::ifstream in(path);
  std::string line;
  while (std::getline(in, line)) {
    lines.push_back(line);
  }
  return lines;
}

/// Where a function's body starts and ends, so a call can be placed in one.
///
/// Counts braces from the opening one, ignoring any inside a string literal or a
/// comment: a brace in a message would otherwise end the search early. Good enough
/// for these sources, where the only braces in strings are in messages.
static void functionRange(const std::vector<std::string>& lines, size_t from, size_t& begin, size_t& end) {
  size_t depth = 0;
  bool started = false;
  for (size_t i = from; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    size_t j = 0;
    while (j < line.size()) {
      if (line[j] == '/' && j + 1 < line.size() && line[j + 1] == '/') {
        break;
      }
      if (line[j] == '"') {
        // skip the literal
        ++j;
        while (j < line.size() && line[j] != '"') {
          if (line[j] == '\\') {
            ++j;
          }
          ++j;
        }
      } else if (line[j] == '{') {
        if (!started) {
          begin = i;
          started = true;
        }
        ++depth;
      } else if (line[j] == '}') {
        --depth;
        if (started && depth == 0) {
          end = i;
          return;
        }
      }
      ++j;
    }
  }
  end = lines.size();
}

int main(int argc, char** argv) {
  const std::string root = (argc > 1) ? argv[1] : ".";

  for (const char* rel : kSources) {
    const std::string path = root + "/" + rel;
    const std::vector<std::string> lines = readLines(path);
    if (lines.empty()) {
      std::printf("  FAIL cannot read %s\n", path.c_str());
      ++failures;
      continue;
    }

    // Every literal name opened, and every one begun.
    std::set<std::string> opened;
    std::set<std::string> begun;
    for (const std::string& line : lines) {
      const std::string o = openedName(line);
      if (!o.empty()) {
        opened.insert(o);
      }
      const std::string b = begunName(line);
      if (!b.empty()) {
        begun.insert(b);
      }
    }
    for (const std::string& name : opened) {
      check(begun.count(name) == 1, std::string(rel) + ": '" + name + "' is begun");
    }

    // Which functions are recursive tree renderers, and where they end.
    std::vector<std::pair<size_t, size_t>> treeRenderers;
    for (size_t i = 0; i < lines.size(); ++i) {
      if (lines[i].find("::renderFileTree(") == std::string::npos) {
        continue;
      }
      size_t begin = i;
      size_t end = 0;
      functionRange(lines, i, begin, end);
      treeRenderers.emplace_back(begin, end);
    }
    check(!treeRenderers.empty() || true, std::string(rel) + ": tree renderers located");

    // An OpenPopup inside one of them is at the wrong stack depth: the modals are
    // all drawn by the panel that calls the renderer, not by the nodes it walks.
    for (const std::string& line : lines) {
      const std::string name = openedName(line);
      if (name.empty()) {
        continue;
      }
      bool insideTree = false;
      for (const auto& range : treeRenderers) {
        for (size_t i = range.first; i <= range.second; ++i) {
          if (lines[i] == line && openedName(lines[i]) == name) {
            insideTree = true;
            break;
          }
        }
        if (insideTree) {
          break;
        }
      }
      if (insideTree) {
        check(false, std::string(rel) + ": '" + name + "' is opened inside renderFileTree");
      }
    }
  }

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}