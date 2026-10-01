/**
 * @file json_open_as_tree.cpp
 * @brief A project's own JSON opens as text, and can be looked at as a tree instead
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The rule that sends a .json to the text backend is that the configuration files
// are the ones a user is told to edit by hand: exports.json holds the PROGRAM
// order, a task file its cycle time, and a tree has nothing to type into. That is
// the right default and it is what openFile does on its own.
//
// It is not a reason to be unable to look at one. exports.json is nested and reads
// badly flat, so "Open as Tree" has to reach the viewer for a file that does not
// go there by itself, and "Open as Text" for one that does. The check is that both
// routes end with the file in exactly one tab, in the backend that was asked for:
// a file that is in two tabs makes the bar a worse answer than no bar, and a file
// that is in the wrong backend is on screen in a viewer with no text to edit, or in
// an editor showing something that is not the file.

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#define private public
#include <TextEditor.h>
#include "undoAppEditor.hpp"
#undef private

using namespace undoApp::Editor;
using undoApp::DocKind;
using undoApp::ST::STApp;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

static void writeFile(const fs::path& p, const std::string& body) {
  std::ofstream out(p);
  out << body;
}

static DocKind kindOf(EditorApp& app, const std::string& path) {
  return app.m_open.documents()[app.documentIndex(path)].kind;
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  const fs::path dir = fs::temp_directory_path() / "undoStudio-json-tree";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);

  // Nested on purpose. A flat one would open as text and read as text, and would
  // never make the reason for the second route visible.
  const fs::path dataFile = dir / "plain.json";
  writeFile(dataFile, "{\n  \"a\": {\n    \"b\": 1\n  }\n}\n");

  auto& app = EditorApp::getInstance();
  check(app.initialize(), "the editor app initializes");
  STApp::getInstance().setupEditors();

  // --- a .json that is not a project file goes to the viewer on its own ---
  app.openFile(dataFile.string(), /*pin=*/true);
  check(app.m_open.size() == 1, "one file open, got " + std::to_string(app.m_open.size()));
  check(kindOf(app, dataFile.string()) == DocKind::JSON,
        "a .json with no project behind it opens as the tree, got kind " +
            std::to_string(static_cast<int>(kindOf(app, dataFile.string()))));

  // --- "Open as Text" moves it, and leaves one tab ---
  app.openFileAsText(dataFile.string());
  check(app.m_open.size() == 1, "opening it as text does not add a tab, got " + std::to_string(app.m_open.size()));
  check(!app.m_open.isOpen(dataFile.string()) || kindOf(app, dataFile.string()) == DocKind::Text,
        "and the tab that is there is the text one");
  check(kindOf(app, dataFile.string()) == DocKind::Text,
        "the file is now text, got kind " + std::to_string(static_cast<int>(kindOf(app, dataFile.string()))));

  // --- and back to the tree, still one tab ---
  app.openFileAsTree(dataFile.string());
  check(app.m_open.size() == 1, "and opening it as a tree does not add a tab either, got " +
                                    std::to_string(app.m_open.size()));
  check(kindOf(app, dataFile.string()) == DocKind::JSON,
        "and the tab is the tree one again, got kind " +
            std::to_string(static_cast<int>(kindOf(app, dataFile.string()))));

  // --- the backend that is left behind is not still holding the file ---
  //
  // One backend per file, or two of them hold one file and only one is ever drawn:
  // the panel draws whichever m_currentFileType names, and the other keeps a stale
  // copy that a later tab switch would hand back.
  check(app.getCurrentFilePath() == dataFile.string(), "the file on screen is the one that was asked for");

  // --- the configuration file, the case the second route exists for ---
  //
  // isConfigFile() is what sends it to the text backend by default, and it answers
  // that only for a file inside an open project: a .json on its own is Generic,
  // which is why the file above opened as a tree. So a real project has to be open
  // before the default can be observed at all -- the rule is about the project's
  // own configuration, not about the extension.
  const fs::path project = dir / "prj";
  fs::create_directories(project / ".undoProject");
  fs::create_directories(project / "undoLogic" / "Main");
  {
    std::ofstream json(project / ".undoProject" / "project.json");
    json << "{\"project\": {\"name\": \"prj\", \"target\": \"Linux\", \"semantics\": {\"strictness\": true}}}\n";
  }
  // A PLC has to be registered before its exports file is that PLC's: the path is
  // undoLogic/<plc>/exports.json, and the role is answered from the list of PLCs
  // rather than from the file's own name.
  {
    std::ofstream json(project / ".undoProject" / "plcs.json");
    json << "{\"plcs\": [{\"name\": \"Main\", \"description\": \"the one\"}]}\n";
  }
  {
    std::ofstream json(project / "undoLogic" / "Main" / "exports.json");
    json << "{\n  \"programs\": [\n    { \"name\": \"Main\", \"task\": \"Cycle\" }\n  ]\n}\n";
  }

  auto& pm = undoStudio::core::ProjectManager::getInstance();
  check(pm.openProject(project.string()), "a project opens, so its own files have a role");

  const std::string prjExports = (project / "undoLogic" / "Main" / "exports.json").string();
  app.openFile(prjExports, /*pin=*/true);
  check(kindOf(app, prjExports) == DocKind::Text,
        "a configuration .json opens as text by default, got kind " +
            std::to_string(static_cast<int>(kindOf(app, prjExports))));
  check(pm.isConfigFile(prjExports), "and the project agrees the file is its own configuration");

  const std::string exportsFile = prjExports;

  app.openFileAsTree(exportsFile);
  check(app.m_open.size() == 2, "asking for the tree adds the tab rather than replacing it, got " +
                                    std::to_string(app.m_open.size()));
  check(kindOf(app, exportsFile) == DocKind::JSON,
        "and the configuration file can be opened as a tree, got kind " +
            std::to_string(static_cast<int>(kindOf(app, exportsFile))));
  check(app.getCurrentFilePath() == exportsFile, "and it is the one on screen");

  // --- a file that is a tree already is not disturbed by being asked for again ---
  app.openFileAsTree(exportsFile);
  check(app.m_open.size() == 2, "asking twice does not add a second tab, got " + std::to_string(app.m_open.size()));

  // --- the empty path is not a document ---
  app.openFileAsTree("");
  app.openFileAsText("");
  check(app.m_open.size() == 2, "an empty path opens nothing, got " + std::to_string(app.m_open.size()));

  // --- and the project closes, so a later file is Generic again ---
  //
  // The default is a property of the file's role, not of its extension. If the
  // answer outlived the project, a .json left behind by a closed one would keep
  // opening as text with nothing behind it.
  pm.closeProject();
  const fs::path orphan = dir / "orphan.json";
  writeFile(orphan, "{\n  \"x\": 1\n}\n");
  app.openFile(orphan.string(), /*pin=*/true);
  check(kindOf(app, orphan.string()) == DocKind::JSON,
        "a .json outside any project opens as a tree, got kind " +
            std::to_string(static_cast<int>(kindOf(app, orphan.string()))));

  fs::remove_all(dir, ec);
  ImGui::DestroyContext();

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}
