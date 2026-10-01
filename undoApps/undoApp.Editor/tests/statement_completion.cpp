/**
 * @file statement_completion.cpp
 * @brief End-to-end check of the statement completion: typing the start of a statement
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The last third is where the list is allowed to open. Both of those cases were
// wrong before they were tested: a fuzzy match took the list over the member
// completion after "motore.re", and it hid an identifier that begins like a
// keyword.
//
// Uses an ImGui context without a window, so it runs headless.
#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define private public
#include <TextEditor.h>
#include "undoAppST.hpp"
#include "st_editor_frame.hpp"
#undef private

using namespace undoApp::ST;

static int failures = 0;
static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

static void frame(STApp& app) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(600.0f, 700.0f);
  ImGui::NewFrame();
  ImGui::SetNextWindowSize(ImVec2(1200, 800));
  stEditorFrame(app);
  ImGui::EndFrame();
}

/// One frame with the body editor holding the keyboard.
///
/// TextEditor claims the keyboard from inside its own Render by setting
/// io.WantTextInput, and a headless frame cannot reproduce the click sequence
/// that gets it there. Injecting the flag between NewFrame and the render is the
/// same signal at the same point in the frame.
static void frameWithBodyEditorFocused(STApp& app) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(600.0f, 700.0f);
  ImGui::NewFrame();
  io.WantTextInput = true;
  ImGui::SetNextWindowSize(ImVec2(1200, 800));
  stEditorFrame(app);
  ImGui::EndFrame();
}

/// Park the caret at the end of a body line and let one frame run.
static void typeOnLine(STApp& app, const std::string& bodyText, int line, int column) {
  app.m_bodyEditor->SetText(bodyText);
  TextEditor::Coordinates at;
  at.mLine = line;
  at.mColumn = column;
  app.m_bodyEditor->SetCursorPosition(at);
  frameWithBodyEditorFocused(app);
}

static const StatementSnippet* labelled(const std::vector<StatementSnippet>& list, const std::string& label) {
  for (const auto& s : list) {
    if (s.label == label) return &s;
  }
  return nullptr;
}

static bool offers(const std::vector<MemberAccess>& list, const std::string& name) {
  for (const auto& m : list) {
    if (m.name == name) return true;
  }
  return false;
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "undoStudio-statement-completion";
  std::filesystem::create_directories(dir);

  const auto fbPath = dir / "undoFB.st";
  {
    std::ofstream out(fbPath);
    out << "FUNCTION_BLOCK undoFB\n"
           "VAR\n"
           "    ticks : INT;\n"
           "END_VAR\n"
           "METHOD PUBLIC Reset : BOOL\n"
           "    Reset := TRUE;\n"
           "END_METHOD\n"
           "END_FUNCTION_BLOCK\n";
  }
  const auto pouPath = dir / "undoPOU.st";
  {
    std::ofstream out(pouPath);
    out << "PROGRAM undoPOU\n"
           "VAR\n"
           "    motore : undoFB;\n"
           "    reSetFlag : BOOL;\n"
           "END_VAR\n"
           "END_PROGRAM\n";
  }

  STApp app;
  app.setupEditors();
  app.requestOpenFile(pouPath.string());
  frame(app);
  frameWithBodyEditorFocused(app);
  check(app.m_pouName == "undoPOU", "the POU opened, got '" + app.m_pouName + "'");

  // --- a keyword prefix opens the statement list ---
  typeOnLine(app, "\nIF\n", 1, 2);
  check(app.statementCompletionOpen(), "'IF' opens the statement list");
  check(labelled(app.m_snippetCandidates, "IF ... END_IF") != nullptr,
        "the IF skeleton is offered, got " + std::to_string(app.m_snippetCandidates.size()) + " candidates");
  check(app.m_snippetCandidates[0].label == "IF ... END_IF",
        "the whole statement is the best match, got '" + app.m_snippetCandidates[0].label + "'");

  // --- the closing keywords come first once 'end' has been typed ---
  typeOnLine(app, "\nEND\n", 1, 3);
  check(app.statementCompletionOpen(), "'END' opens the statement list");
  check(!app.m_snippetCandidates.empty() &&
            app.m_snippetCandidates[0].label.compare(0, 3, "END") == 0,
        "a closing keyword outranks a statement that merely contains it, got '" +
            (app.m_snippetCandidates.empty() ? std::string("none") : app.m_snippetCandidates[0].label) + "'");

  // --- accepting writes the skeleton and leaves the caret on the condition ---
  typeOnLine(app, "\nIF\n", 1, 2);
  app.acceptStatementCompletion();
  const std::string afterIf = app.m_bodyEditor->GetText();
  check(afterIf.find("IF  THEN") != std::string::npos,
        "the skeleton brings its THEN, got '" + afterIf + "'");
  check(afterIf.find("END_IF") != std::string::npos, "the skeleton brings its END_IF");
  const TextEditor::Coordinates ifCaret = app.m_bodyEditor->GetCursorPosition();
  check(ifCaret.mLine == 1 && ifCaret.mColumn == 3,
        "the caret lands on the condition, got line " + std::to_string(ifCaret.mLine) + " column " +
            std::to_string(ifCaret.mColumn));

  // --- the typed keyword is replaced, not left next to the one written ---
  check(afterIf.find("IFIF") == std::string::npos, "the typed 'IF' is replaced rather than doubled");

  // --- the skeleton is written under the indentation already in force ---
  typeOnLine(app, "\n    IF\n", 1, 6);
  app.acceptStatementCompletion();
  const std::string nested = app.m_bodyEditor->GetText();
  check(nested.find("    IF  THEN") != std::string::npos, "the keyword keeps the caret line's indent");
  check(nested.find("\n        \n") != std::string::npos || nested.find("\n        ") != std::string::npos,
        "the body of the skeleton is indented one step further, got '" + nested + "'");
  check(nested.find("    END_IF") != std::string::npos, "the closing keyword is indented too");

  // --- a word reached through a '.' belongs to the member list ---
  //
  // "re" is the case that matters: REPEAT is a subsequence match for it, so a
  // list opening here would switch the member completion off underneath the word
  // being typed.
  typeOnLine(app, "\nmotore.re\n", 1, 9);
  check(!app.statementCompletionOpen(), "a member access keeps the statement list closed");
  check(app.m_completionEditor == app.m_bodyEditor.get(),
        "the member list has the word instead, got '" + app.m_completionPrefix + "'");
  check(offers(app.m_completionCandidates, "Reset"),
        "the block's method is still offered, got " + std::to_string(app.m_completionCandidates.size()) +
            " candidates");

  // --- an identifier that begins like a keyword keeps the identifier list ---
  typeOnLine(app, "\nreSetFlag\n", 1, 3);
  check(!app.statementCompletionOpen(),
        "'reS' is not the start of a keyword, so the statement list stays closed");
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the identifier list answers 'reS'");
  check(offers(app.m_completionCandidates, "reSetFlag"), "the variable the user is typing is offered");

  // --- a word that is neither a keyword nor an identifier offers nothing ---
  typeOnLine(app, "\nzzq\n", 1, 3);
  check(!app.statementCompletionOpen(), "an unrelated word opens no list");

  // --- Escape closes the list and keeps it closed at that spot ---
  typeOnLine(app, "\nIF\n", 1, 2);
  check(app.statementCompletionOpen(), "the list reopens for a fresh 'IF'");
  app.dismissStatementCompletion();
  typeOnLine(app, "\nIF\n", 1, 2);
  check(!app.statementCompletionOpen(), "Escape holds the list closed at the same spot");

  ImGui::DestroyContext();

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}
