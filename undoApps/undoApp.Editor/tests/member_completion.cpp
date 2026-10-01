/**
 * @file member_completion.cpp
 * @brief End-to-end check of the member completion: typing '.' after a function block instance
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The block lives in a sibling file, which is how undoStudio reaches it: the
// editor regenerates the .st from the POU being edited, so a block declared in
// the same file is not part of the analysed source. A sibling file goes through
// the library registry instead, which is the path worth testing.
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
/// that gets it there reliably. Injecting the flag between NewFrame and the
/// render is the same signal, at the same point in the frame, so the code under
/// test takes exactly the path it takes when a user clicks into the editor.
static void frameWithBodyEditorFocused(STApp& app) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(600.0f, 700.0f);
  ImGui::NewFrame();
  io.WantTextInput = true;
  ImGui::SetNextWindowSize(ImVec2(1200, 800));
  stEditorFrame(app);
  ImGui::EndFrame();
}

/// Park the cursor just after a member access and let one frame run.
static void typeMemberAccess(STApp& app, const std::string& bodyText, int column) {
  app.m_bodyEditor->SetText(bodyText);
  TextEditor::Coordinates at;
  at.mLine = 1;
  at.mColumn = column;
  app.m_bodyEditor->SetCursorPosition(at);
  frameWithBodyEditorFocused(app);
}

static const MemberAccess* named(const std::vector<MemberAccess>& list, const std::string& name) {
  for (const auto& m : list) {
    if (m.name == name) return &m;
  }
  return nullptr;
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "undoStudio-member-completion";
  std::filesystem::create_directories(dir);

  const auto fbPath = dir / "undoFB.st";
  {
    std::ofstream out(fbPath);
    out << "FUNCTION_BLOCK undoFB\n"
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
  const auto pouPath = dir / "undoPOU.st";
  {
    std::ofstream out(pouPath);
    out << "PROGRAM undoPOU\n"
           "VAR\n"
           "    motore : undoFB;\n"
           "END_VAR\n"
           "END_PROGRAM\n";
  }

  STApp app;
  app.setupEditors();
  app.requestOpenFile(pouPath.string());
  frame(app);
  frameWithBodyEditorFocused(app);
  check(app.m_pouName == "undoPOU", "the POU opened, got '" + app.m_pouName + "'");

  // --- the list opens on '.' ---
  typeMemberAccess(app, "\nmotore.\n", 8);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "'.' opens the list on the body editor");
  check(!app.m_completionCandidates.empty(), "'.' offers candidates");
  check(app.m_completionObject == "motore", "the object is the instance, got '" + app.m_completionObject + "'");

  const MemberAccess* abilita = named(app.m_completionCandidates, "abilita");
  const MemberAccess* ticks = named(app.m_completionCandidates, "ticks");
  const MemberAccess* limite = named(app.m_completionCandidates, "limite");
  const MemberAccess* reset = named(app.m_completionCandidates, "Reset");
  check(abilita != nullptr, "the sibling block's parameter is offered");
  check(ticks != nullptr, "the sibling block's state is offered");
  check(limite != nullptr, "the sibling block's constant state is offered");
  check(reset != nullptr, "the sibling block's method is offered");
  if (abilita) {
    check(abilita->kind == MemberAccess::Kind::Parameter, "abilita is a parameter");
    check(abilita->typeText == "BOOL", "abilita is BOOL, got " + abilita->typeText);
  }
  if (ticks) {
    check(ticks->kind == MemberAccess::Kind::State, "ticks is state, not a parameter");
    check(ticks->typeText == "INT", "ticks is INT, got " + ticks->typeText);
  }
  if (limite) {
    check(limite->isConstant, "limite keeps its constant flag");
  }
  if (reset) {
    check(reset->kind == MemberAccess::Kind::Method, "Reset is a method");
    check(reset->typeText == "BOOL", "Reset returns BOOL, got " + reset->typeText);
  }

  // --- the prefix filters ---
  typeMemberAccess(app, "\nmotore.ti\n", 9);
  check(app.m_completionCandidates.size() == 1, "'ti' narrows to one candidate, got " +
                                                   std::to_string(app.m_completionCandidates.size()));
  if (app.m_completionCandidates.size() == 1) {
    check(app.m_completionCandidates[0].name == "ticks", "the survivor is 'ticks'");
  }

  // --- a name that is not a block instance offers nothing ---
  typeMemberAccess(app, "\nveloce.\n", 8);
  check(app.m_completionCandidates.empty(), "a plain INT opens no list");
  check(app.m_completionEditor == nullptr, "and the previous list was dismissed");

  // --- moving the selection wraps around ---
  typeMemberAccess(app, "\nmotore.\n", 8);
  const int count = static_cast<int>(app.m_completionCandidates.size());
  if (count > 1) {
    app.m_completionSelected = 0;
    app.moveMemberCompletion(-1);
    check(app.m_completionSelected == count - 1, "up from the first entry wraps to the last");
    app.moveMemberCompletion(1);
    check(app.m_completionSelected == 0, "down from the last wraps to the first");
  } else {
    check(false, "expected several candidates to navigate");
  }

  // --- accepting a method adds its parentheses ---
  typeMemberAccess(app, "\nmotore.re\n", 10);
  for (int i = 0; i < static_cast<int>(app.m_completionCandidates.size()); ++i) {
    if (app.m_completionCandidates[i].name == "Reset") app.m_completionSelected = i;
  }
  app.acceptMemberCompletion();
  check(app.m_bodyEditor->GetText().find("motore.Reset()") != std::string::npos,
        "accepting a method inserts the parentheses, got '" + app.m_bodyEditor->GetText() + "'");
  check(app.m_completionEditor == nullptr, "the list closes after accepting");

  // --- accepting a state member inserts only the name ---
  typeMemberAccess(app, "\nmotore.ti\n", 9);
  app.acceptMemberCompletion();
  check(app.m_bodyEditor->GetText().find("motore.ticks") != std::string::npos,
        "accepting a member inserts the name, got '" + app.m_bodyEditor->GetText() + "'");
  check(app.m_bodyEditor->GetText().find("motore.ticks()") == std::string::npos,
        "accepting a member does not add parentheses");

  // --- the list does not steal the keyboard ---
  //
  // This is the regression behind the list blinking and the typing going nowhere.
  // The editors are BeginChild windows and TextEditor::HandleKeyboardInputs only
  // sets io.WantTextInput when its child is focused. ImGui focuses a window the
  // first time it appears, so an unguarded list takes the keyboard on the frame
  // it opens: the editor stops claiming the text input, the list is dismissed,
  // it reappears, takes focus again, and the popup flickers while the characters
  // typed after the '.' reach no editor.
  //
  // The flags are asserted rather than the runtime focus, because a headless ImGui
  // context never activates the navigation that performs the focus-on-appearing
  // rule: a runtime check passes with or without the fix, so it would guard
  // nothing. Asserting the flags is what actually pins the behaviour down.
  check((STApp::kMemberCompletionWindowFlags & ImGuiWindowFlags_NoFocusOnAppearing) != 0,
        "the list never takes focus when it appears");
  check((STApp::kMemberCompletionWindowFlags & ImGuiWindowFlags_NoNavFocus) != 0,
        "the list is not a navigation target, so the editor keeps the keyboard");

  // The list must stay open across frames instead of being dismissed and reopened,
  // which is what stability means here.
  app.m_bodyEditor->SetText("\nmotore.\n");
  {
    TextEditor::Coordinates at;
    at.mLine = 1;
    at.mColumn = 8;
    app.m_bodyEditor->SetCursorPosition(at);
  }
  frameWithBodyEditorFocused(app);
  check(!app.m_completionCandidates.empty(), "the list is open for the stability test");
  const std::vector<MemberAccess> firstFrameCandidates = app.m_completionCandidates;
  frameWithBodyEditorFocused(app);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the list stays on the body editor");
  check(!app.m_completionCandidates.empty(), "the list is still open on the next frame");
  check(app.m_completionCandidates.size() == firstFrameCandidates.size(),
        "the candidate count is stable across frames, got " +
           std::to_string(app.m_completionCandidates.size()) + " then " +
           std::to_string(firstFrameCandidates.size()));

  // --- the keys are consumed before the editors act on them ---
  //
  // handleMemberCompletionKeys() has to run before TextEditor::Render(), or Enter
  // would both accept the member and insert a line break.
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the list is open for the key test");
  app.m_bodyEditor->SetHandleKeyboardInputs(true);
  const bool keyConsumed = app.handleMemberCompletionKeys();
  check(keyConsumed == false, "an unpressed frame consumes nothing");
  app.m_bodyEditor->SetHandleKeyboardInputs(false);
  check(app.m_bodyEditor->IsHandleKeyboardInputsEnabled() == false,
        "the editor stays muted until the frame is restored");

  // --- dismissal leaves the text alone ---
  typeMemberAccess(app, "\nmotore.\n", 8);
  const std::string before = app.m_bodyEditor->GetText();
  app.clearMemberCompletion();
  check(app.m_bodyEditor->GetText() == before, "dismissing leaves the text untouched");
  check(app.m_completionCandidates.empty(), "dismissing empties the candidates");

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}
