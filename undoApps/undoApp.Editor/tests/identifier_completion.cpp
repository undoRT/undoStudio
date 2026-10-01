/**
 * @file identifier_completion.cpp
 * @brief Completion for a half-typed plain name: the list narrows to it, and Tab finishes it
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// Everything here drives whole frames rather than calling the update functions,
// because what is being tested is a decision taken during a frame: which
// suggestions exist for the word ending at the caret, and what happens to the
// buffer when one is accepted. A test that called updateMemberCompletion()
// directly would not see the same thing the user does.
//
// The workspace is written here rather than shared with member_completion.cpp,
// since the assertions are about the names the editor collected from the scopes
// and each test wants its own.
//
// Uses an ImGui context without a window, so it runs headless.
#include <imgui.h>

#include <algorithm>
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

/// @brief Whether the list holds a candidate with this exact name
static bool offered(STApp& app, const std::string& name) {
  for (const MemberAccess& candidate : app.m_completionCandidates) {
    if (candidate.name == name) {
      return true;
    }
  }
  return false;
}

/// @brief The kind of the candidate with this name, if it is there
static bool kindIs(STApp& app, const std::string& name, MemberAccess::Kind kind) {
  for (const MemberAccess& candidate : app.m_completionCandidates) {
    if (candidate.name == name) {
      return candidate.kind == kind;
    }
  }
  return false;
}

/// @brief The names in the list, for a failure message
static std::string names(STApp& app) {
  std::string out;
  for (const MemberAccess& candidate : app.m_completionCandidates) {
    if (!out.empty()) {
      out += ", ";
    }
    out += candidate.name;
  }
  return out.empty() ? "none" : out;
}

/// One frame, with the body editor holding the keyboard and no key pressed
static void runFrame(STApp& app) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(600.0f, 700.0f);
  ImGui::NewFrame();
  io.WantTextInput = true;
  ImGui::SetNextWindowSize(ImVec2(1200, 800));
  stEditorFrame(app);
  ImGui::EndFrame();
}

/// One frame with the body editor holding the keyboard and no key pressed
static void frameFocused(STApp& app) { runFrame(app); }

/// One frame with a key pressed, and a second so the release is seen
///
/// A key released and pressed again inside one frame is not a press to ImGui: it
/// never sees the key go up, so the time it has been down for does not restart.
static void frameWithKey(STApp& app, ImGuiKey key) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(key, true);
  runFrame(app);
  io.AddKeyEvent(key, false);
  runFrame(app);
}

/// @brief Put `bodyText` in the body editor, park the caret at `column` of line 1
static void place(STApp& app, const std::string& bodyText, int column) {
  app.m_bodyEditor->SetText(bodyText);
  TextEditor::Coordinates at;
  at.mLine = 1;
  at.mColumn = column;
  app.m_bodyEditor->SetCursorPosition(at);
  frameFocused(app);
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();
  // A press and the release of the next key can land in one frame, and ImGui
  // defers one of them to the frame after by default. That is fine for a person
  // typing and useless for a test that presses a key and asserts on the same
  // frame, so the queue is emptied in one go here.
  ImGui::GetIO().ConfigInputTrickleEventQueue = false;

  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "undoStudio-identifier-completion";
  std::filesystem::create_directories(dir);

  // A function block with a constant and a state variable of its own, so the
  // test can tell 'a name in this scope' from 'a member of the instance': both
  // are reachable from the body, only one of them without a dot.
  const auto fbPath = dir / "idFB.st";
  {
    std::ofstream out(fbPath);
    out << "FUNCTION_BLOCK idFB\n"
           "VAR_INPUT\n"
           "    abilita : BOOL;\n"
           "END_VAR\n"
           "VAR\n"
           "    ticks : INT;\n"
           "END_VAR\n"
           "VAR CONSTANT\n"
           "    limite : INT := 10;\n"
           "END_VAR\n"
           "METHOD PUBLIC Reset : BOOL\n"
           "    Reset := TRUE;\n"
           "END_METHOD\n"
           "END_FUNCTION_BLOCK\n";
  }

  // The POU under edit: a variable of the block above, so that a name in scope and
  // a member of an instance can be told apart. It deliberately has no method of
  // its own: the generated file writes a PROGRAM's methods next to its body, and
  // the front end reads that as an expression it cannot parse, which would leave
  // this program without a semantic model at all.
  const auto pouPath = dir / "idPOU.st";
  {
    std::ofstream out(pouPath);
    out << "PROGRAM idPOU\n"
           "VAR\n"
           "    motore : idFB;\n"
           "END_VAR\n"
           "END_PROGRAM\n";
  }

  STApp app;
  app.setupEditors();
  app.requestOpenFile(pouPath.string());
  {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(600.0f, 700.0f);
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(1200, 800));
    stEditorFrame(app);
    ImGui::EndFrame();
  }
  frameFocused(app);
  check(app.m_pouName == "idPOU", "the POU opened, got '" + app.m_pouName + "'");

  // --- a half-typed variable ---
  place(app, "\nmot\n", 3);
  {
    std::ofstream dbg("/tmp/undoStudio-identifier-debug2.txt");
    dbg << "--- after place: ast=" << (app.m_ast ? 1 : 0) << " semantic=" << (app.m_semantic ? 1 : 0)
        << " scope=" << app.scopeIdForLine(1) << "\n";
    for (const auto& line : app.m_outputLines) dbg << "| " << line.text << "\n";
  }
  check(app.m_completionEditor == app.m_bodyEditor.get(), "typing 'mot' opens the list");
  check(offered(app, "motore"), "'mot' offers 'motore', got " + names(app));
  check(app.m_completionCandidates.size() == 1, "'mot' narrows to one candidate, got " +
                                                      std::to_string(app.m_completionCandidates.size()));
  check(kindIs(app, "motore", MemberAccess::Kind::State), "and it is the variable");

  // --- Tab completes the name it was narrowed to ---
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_bodyEditor->GetText() == "\nmotore\n\n", "Tab completes the name, got '" +
                                                          app.m_bodyEditor->GetText() + "'");
  check(app.m_completionEditor == nullptr, "and the list closes once the name is whole");
  check(app.m_bodyEditor->GetCursorPosition().mColumn == 6, "with the caret after the name, got " +
                                                                std::to_string(app.m_bodyEditor->GetCursorPosition().mColumn));

  // --- the same name as a type, and the block instance's own members ---
  place(app, "\nid\n", 2);
  check(offered(app, "idFB"), "the function block type of the workspace is completable, got " + names(app));

  // A member of the instance is not a name in the POU's scope. Offering 'limite'
  // here would suggest a variable that does not exist at this point, even though
  // the fuzzy matcher will happily match a library name that holds the same
  // letters: what matters is that this one is absent.
  place(app, "\nlim\n", 3);
  check(!offered(app, "limite"), "a member of the instance is not offered without its dot, got " +
                                    names(app));

  // --- a line that does not parse yet still completes ---
  //
  // 'mot' on a line of its own is not a statement, so the front end throws and the
  // program has no model. The declarations in scope have not changed while the
  // word was typed, so the last model that worked is still the right answer, and
  // throwing it away would leave the list dead exactly while it is being used.
  place(app, "\nmot\n", 3);
  check(offered(app, "motore"), "a half-typed line that does not parse yet still completes, got " +
                                    names(app));

  // --- the word after an assignment, which is where most names get typed ---
  place(app, "\nx := mot\n", 9);
  check(offered(app, "motore"), "a name after ':=' is completed too, got " + names(app));
  place(app, "\nx = mot\n", 7);
  check(offered(app, "motore"), "and after a plain '=', got " + names(app));
  place(app, "\nEnable(mot\n", 11);
  check(offered(app, "motore"), "and inside an argument list, got " + names(app));

  // --- nowhere a name can go ---
  //
  // In a string or a comment the text is not code, and a name written there
  // cannot be completed. A list that appears anyway reads as the editor having
  // lost track of where the caret is.
  place(app, "\nmotore := 'mot\n", 14);
  check(app.m_completionEditor == nullptr, "nothing is offered inside a string literal");
  place(app, "\n// mot\n", 7);
  check(app.m_completionEditor == nullptr, "and nothing inside a comment");
  place(app, "\n10\n", 2);
  check(app.m_completionEditor == nullptr, "a line that is only a literal offers nothing");
  place(app, "\n\n", 0);
  check(app.m_completionEditor == nullptr, "an empty line offers no list until there is a word");

  // --- a word that matches nothing closes rather than showing an empty list ---
  place(app, "\nzzzzz\n", 5);
  check(app.m_completionEditor == nullptr, "a name that matches nothing shows no list");

  // --- the list follows the word as it is typed ---
  place(app, "\nmo\n", 2);
  check(offered(app, "motore"), "'mo' offers 'motore'");
  place(app, "\nmot\n", 3);
  check(app.m_completionCandidates.size() == 1, "and 'mot' has narrowed it to one, got " +
                                                      std::to_string(app.m_completionCandidates.size()));
  check(app.m_completionSelected == 0, "with that one selected");

  // --- Escape still closes a plain-name list, and stays closed ---
  place(app, "\nmo\n", 2);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the list is open for Escape");
  frameWithKey(app, ImGuiKey_Escape);
  check(app.m_completionEditor == nullptr, "Escape closes it");
  frameFocused(app);
  check(app.m_completionEditor == nullptr, "and it stays closed while the caret does not move");

  // --- the same completion inside a function block, where its own method and its
  // own constants are names in scope ---
  STApp fbApp;
  fbApp.setupEditors();
  fbApp.requestOpenFile(fbPath.string());
  {
    ImGuiIO& io = ImGui::GetIO();
    io.AddMousePosEvent(600.0f, 700.0f);
    ImGui::NewFrame();
    ImGui::SetNextWindowSize(ImVec2(1200, 800));
    stEditorFrame(fbApp);
    ImGui::EndFrame();
  }
  frameFocused(fbApp);
  check(fbApp.m_pouName == "idFB", "the block opened as the POU under edit, got '" + fbApp.m_pouName + "'");

  place(fbApp, "\ntic\n", 3);
  check(offered(fbApp, "ticks"), "the block's own variable is a name in scope, got " + names(fbApp));
  place(fbApp, "\nlim\n", 3);
  check(offered(fbApp, "limite"), "and so is its constant, got " + names(fbApp));
  check(kindIs(fbApp, "limite", MemberAccess::Kind::Constant), "which is offered as a constant");
  place(fbApp, "\nabi\n", 3);
  check(offered(fbApp, "abilita"), "and its input parameter, got " + names(fbApp));
  check(kindIs(fbApp, "abilita", MemberAccess::Kind::Parameter), "which is offered as a parameter");

  // A method is completed ready to be called, with the caret between the
  // brackets so the arguments can be typed straight in.
  place(fbApp, "\nRes\n", 3);
  check(offered(fbApp, "Reset"), "the block's own method is completable, got " + names(fbApp));
  check(kindIs(fbApp, "Reset", MemberAccess::Kind::Method), "and it is a method");
  frameWithKey(fbApp, ImGuiKey_Tab);
  check(fbApp.m_bodyEditor->GetText() == "\nReset()\n\n",
        "Tab completes a method with its brackets, got '" + fbApp.m_bodyEditor->GetText() + "'");
  check(fbApp.m_bodyEditor->GetCursorPosition().mColumn == 7,
        "the caret lands between the brackets, got " +
           std::to_string(fbApp.m_bodyEditor->GetCursorPosition().mColumn));

  std::printf("%s\n", failures == 0 ? "RESULT: all checks passed" : "RESULT: checks failed");
  return failures == 0 ? 0 : 1;
}
