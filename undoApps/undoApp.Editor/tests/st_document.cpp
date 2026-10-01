/**
 * @file st_document.cpp
 * @brief An open ST file, taken out and put back
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// Several files can be open at once, and each backend holds one, so switching
// tabs means taking the file out whole and putting it back when it is shown
// again. What that has to get right is the unsaved half: the only copy of what
// has been typed since the file was opened is in the editors, and reading the file
// off disk instead would hand back the saved version and lose the rest.
//
// So the test does the round trip with the file on disk deliberately different
// from the editors. Anything that came back from the file instead of the memory
// shows up as the disk's text, which is the failure this exists to catch.

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#define private public
#include <TextEditor.h>
#include "undoAppST.hpp"
#undef private

using namespace undoApp::ST;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

static const char* kOriginal =
   "FUNCTION_BLOCK Counter\n"
   "VAR_INPUT\n"
   "    step : INT;\n"
   "END_VAR\n"
   "VAR\n"
   "    total : INT;\n"
   "END_VAR\n"
   "METHOD PUBLIC Bump : INT\n"
   "    total := total + step;\n"
   "END_METHOD\n"
   "METHOD PUBLIC Reset\n"
   "    total := 0;\n"
   "END_METHOD\n"
   "total := total + step;\n"
   "END_FUNCTION_BLOCK\n";

/// What the file on disk says, which is not what the editors will say.
static const char* kOnDisk =
   "FUNCTION_BLOCK Counter\n"
   "VAR_INPUT\n"
   "    step : INT;\n"
   "END_VAR\n"
   "total := 0;\n"
   "END_FUNCTION_BLOCK\n";

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "undoStudio-st-document";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  const fs::path file = dir / "Counter.st";
  {
    std::ofstream out(file);
    out << kOriginal;
  }

  STApp app;
  app.setupEditors();
  app.openFile(file.string());

  check(app.m_pouName == "Counter", "the file opened as the POU it is, got '" + app.m_pouName + "'");
  check(app.m_methods.size() == 2, "with both of its methods, got " + std::to_string(app.m_methods.size()));

  // --- edit it, the way typing does ---
  //
  // The changes are made through the editors, which is where a user's changes
  // live; nothing here goes near m_methods, because in the real editor that is
  // only written back on save.
  //
  // The new variable goes inside the last VAR block rather than after its END_VAR:
  // a declaration after END_VAR is not ST, and the file is re-parsed on the way
  // back in, so putting it there would fail the parse this is not testing.
  std::string vars = app.m_variablesEditor->GetText();
  const size_t lastEndVar = vars.rfind("END_VAR");
  check(lastEndVar != std::string::npos, "the POU's variables end with END_VAR, as expected");
  vars.insert(lastEndVar, "    extra : BOOL;\n");
  app.m_variablesEditor->SetText(vars);
  // A comment on its own line: a comment at the end of a statement is the
  // shape checkMissingSemicolons reads as a missing semicolon, and this test is
  // about the switch, not about that.
  app.m_bodyEditor->SetText(app.m_bodyEditor->GetText() + "// marker\n");

  // A method's editors exist only once its tab has been shown, so showing it is
  // part of getting one open.
  app.getOrCreateMethodEditors(app.m_methods[0]);
  const auto editors = app.m_methodEditors.find(app.m_methods[0].name);
  check(editors != app.m_methodEditors.end(), "the method's editors exist once its tab has been shown");
  std::string pouVarsBefore = app.m_variablesEditor->GetText();
  std::string pouBodyBefore = app.m_bodyEditor->GetText();

  // --- take it out ---
  const STDocument taken = app.takeDocument();

  check(taken.path == file.string(), "the document knows its path");
  check(taken.pouName == "Counter", "and its POU, got '" + taken.pouName + "'");
  check(taken.methods.size() == 2, "and its methods, got " + std::to_string(taken.methods.size()));
  check(taken.pouVariablesText.find("extra : BOOL") != std::string::npos,
        "and the edit to the POU's variables");
  check(taken.pouBodyText.find("marker") != std::string::npos,
        "and the edit to the POU's body");
  check(pouVarsBefore == taken.pouVariablesText && pouBodyBefore == taken.pouBodyText,
        "what came out is what was on screen");

  if (editors != app.m_methodEditors.end()) {
     std::string methodText = editors->second.body->GetText();
     // Type into the method the way a user would.
     methodText += "    // method touched\n";
     editors->second.body->SetText(methodText);
     const STDocument afterEdit = app.takeDocument();
     bool foundTouched = false;
     for (const MethodData& m : afterEdit.methods) {
        if (m.name == "Bump" && m.bodyText.find("method touched") != std::string::npos) {
           foundTouched = true;
        }
     }
     check(foundTouched, "and the edit inside a method, which is only ever in its editor");
  }

  // --- the file on disk is still the old one ---
  {
     std::ifstream in(file);
     std::stringstream buf;
     buf << in.rdbuf();
     check(buf.str().find("extra : BOOL") == std::string::npos,
           "nothing was written to disk on the way out: taking a document is not saving it");
  }

  // --- put a different file in its place ---
  const fs::path other = dir / "Other.st";
  {
     std::ofstream out(other);
     out << "PROGRAM Other\n"
            "VAR\n"
            "    n : INT;\n"
            "END_VAR\n"
            "n := 1;\n"
            "END_PROGRAM\n";
  }
  app.openFile(other.string());
  check(app.m_pouName == "Other", "another file is open, got '" + app.m_pouName + "'");

  // --- put the first one back ---
  app.setDocument(taken);
  check(app.m_currentFilePath == file.string(),
        "the first file is current again, got '" + app.m_currentFilePath + "'");
  check(app.m_pouName == "Counter", "as its own POU, not the one that was in between");
  check(app.m_methods.size() == 2, "with both its methods, got " + std::to_string(app.m_methods.size()));

  // The check that matters: the text came back from memory, not from disk.
  check(app.m_variablesEditor->GetText().find("extra : BOOL") != std::string::npos,
        "the unsaved edit to the variables survived the switch");
check(app.m_bodyEditor->GetText().find("marker") != std::string::npos,
       "and the edit to the body survived it");

  // --- the file on disk was never touched by any of this ---
  {
     std::ifstream in(file);
     std::stringstream buf;
     buf << in.rdbuf();
     check(buf.str().find("extra : BOOL") == std::string::npos,
           "and the file on disk still has none of it: a switch is not a save");
     check(buf.str().find("Bump") != std::string::npos, "and still has its methods");
  }

  // --- the restored document parses ---
  check(app.m_ast != nullptr, "the restored file is parsed again");
  check(app.m_errors.empty(),
        "and parses without errors, got " + std::to_string(app.m_errors.size()) + ": " +
           (app.m_errors.empty() ? std::string() : app.m_errors.front().what()));

  // --- a second round trip loses nothing ---
  {
     const STDocument again = app.takeDocument();
     app.openFile(other.string());
     app.setDocument(again);
     check(app.m_variablesEditor->GetText().find("extra : BOOL") != std::string::npos,
           "and a second round trip is as lossless as the first");
     check(app.m_pouName == "Counter", "still the right file after two switches");
  }

  fs::remove_all(dir, ec);
  ImGui::DestroyContext();

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}