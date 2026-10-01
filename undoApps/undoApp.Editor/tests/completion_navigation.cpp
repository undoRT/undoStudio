// Keyboard handling and signature help for the ST editor's member completion.
//
// The arrow keys are the regression that made this file necessary. The list is
// an overlay window, and the editor that owns it is muted for the frame in which
// a navigation key is handled. TextEditor only raises io.WantTextInput from
// HandleKeyboardInputs, so muting it lowered the very signal the list used to
// decide whether the editor still held the keyboard: the list was dismissed on
// the same frame the key arrived, and Down appeared to do nothing at all. The
// fix reads the keyboard ownership straight after each editor's Render, while
// the editor is still unmuted, and keeps the previous frame's answer for the
// frame it is not.
//
// So the tests below drive whole frames with real key events rather than calling
// the handlers directly: the bug lived in the interaction between the two, and
// calling handleMemberCompletionKeys() by hand cannot reach it.
//
// The block lives in a sibling file, as in member_completion.cpp, because the
// editor analyses a regenerated .st of the POU being edited.
//
// Uses an ImGui context without a window, so it runs headless.
#include <imgui.h>
// The arrow keys also reach ImGui's keyboard navigation, which moves the focus to
// another window and scrolls it into view. Reading NavMoveDir is the only way to
// see that happen from a test, and it lives in the internal header.
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

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

/// One frame, with the body editor holding the keyboard and no key pressed.
///
/// io.WantTextInput is injected between NewFrame and the render because
/// TextEditor raises it from inside Render() when its child is focused, and a
/// headless frame cannot reproduce the click that gets it there. That is the same
/// signal at the same point in the frame, so the code under test takes the path it
/// takes when a user clicks into the editor.
static void runFrame(STApp& app) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(600.0f, 700.0f);
  ImGui::NewFrame();
  io.WantTextInput = true;
  ImGui::SetNextWindowSize(ImVec2(1200, 800));
  app.renderEditorPanel();
  ImGui::EndFrame();
}

/// @brief A navigation request as a name, for a failure message
///
/// An int is unreadable here in a way it is not elsewhere: ImGuiDir_None is -1, so
/// a failure prints -1 and reads as a request going up rather than as none at all.
static const char* dirName(ImGuiDir dir) {
  switch (dir) {
    case ImGuiDir_None: return "None";
    case ImGuiDir_Left: return "Left";
    case ImGuiDir_Right: return "Right";
    case ImGuiDir_Up: return "Up";
    case ImGuiDir_Down: return "Down";
    default: return "?";
  }
}

/// One frame with a key pressed, and the navigation request that press produced
///
/// Read at the end of the press frame, because that is when the request exists:
/// NewFrame is where it is built, and the frame after it drains the release, and a
/// key on its way up asks for nothing.
static ImGuiDir frameWithKeyAndNav(STApp& app, ImGuiKey key) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(key, true);
  runFrame(app);
  // ImGuiDir_None is -1, not 0: read it as the enum it is, or a list that asked
  // for nothing reads as a request going up.
  const ImGuiDir dir = ImGui::GetCurrentContext()->NavMoveDir;
  io.AddKeyEvent(key, false);
  runFrame(app);
  return dir;
}

/// One frame with the body editor holding the keyboard, and a key pressed
///
/// Two frames, because releasing a key and pressing it again inside a single one
/// is not a press as far as ImGui is concerned: the key is never seen up, so the
/// time it has been down for does not restart and IsKeyPressed() stays false. A
/// real keyboard is not that fast, and neither is a test that presses the same
/// key twice in a row. The frame after the release is the cheapest way to let
/// ImGui see the key go up, and it changes nothing: no key is pressed in it.
static void frameWithKey(STApp& app, ImGuiKey key) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(key, true);
  runFrame(app);
  io.AddKeyEvent(key, false);
  runFrame(app);
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

/// One frame with the body editor holding the keyboard and no key pressed
static void frameFocused(STApp& app) { runFrame(app); }


/// Where the signature help ended up this frame, and whether it drew at all
static ImRect signatureRect(STApp& app) {
  return ImRect(app.m_signatureRectMin, app.m_signatureRectMax);
}

/// Where the completion list ended up this frame
static ImRect listRect(STApp& app) {
  return ImRect(app.m_completionRectMin, app.m_completionRectMax);
}

/// Whether two rectangles share any area
static bool overlaps(const ImRect& a, const ImRect& b) {
  return a.Min.x < b.Max.x && b.Min.x < a.Max.x && a.Min.y < b.Max.y && b.Min.y < a.Max.y;
}

/// A rectangle as y=[top..bottom], for a failure message
static std::string span(const ImRect& rect) {
  return "y=[" + std::to_string(static_cast<int>(rect.Min.y)) + ".." +
         std::to_string(static_cast<int>(rect.Max.y)) + "]";
}

/// Park the cursor at `column` of `line` and run one frame
static void placeAtLine(STApp& app, const std::string& bodyText, int line, int column) {
  app.m_bodyEditor->SetText(bodyText);
  TextEditor::Coordinates at;
  at.mLine = line;
  at.mColumn = column;
  app.m_bodyEditor->SetCursorPosition(at);
  frameFocused(app);
}

/// Park the cursor at `column` of the first line and run one frame
static void place(STApp& app, const std::string& bodyText, int column) {
  placeAtLine(app, bodyText, 1, column);
}

static int indexOf(const std::vector<MemberAccess>& list, const std::string& name) {
  for (std::size_t i = 0; i < list.size(); ++i) {
    if (list[i].name == name) return static_cast<int>(i);
  }
  return -1;
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
  // As src/ui/ImGuiManager.cpp does, and before the first frame: the editor reads
  // this once to learn what the host wants, and hands it back unchanged when no
  // overlay is open.
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // --- the fuzzy matcher, on its own ---
  //
  // Prefix matching was the old behaviour and is not enough: a user who half
  // remembers a name types its letters in the order they remember them, not in
  // the order they are declared.
  {
    const NameMatch scattered = matchName("Reset", "rst");
    check(scattered.matched, "'rst' matches 'Reset'");
    check(scattered.positions.size() == 3, "'rst' consumes three characters, got " +
                                                std::to_string(scattered.positions.size()));
    check(matchName("ticks", "tks").matched, "'tks' matches 'ticks'");
    check(matchName("limite", "lt").matched, "'lt' matches 'limite'");
    check(!matchName("ticks", "zzz").matched, "'zzz' matches nothing");
    check(!matchName("ticks", "skt").matched, "'skt' does not match 'ticks', order matters");
    check(matchName("Reset", "reset").matched, "a full name matches regardless of case");
    // A prefix is what a user means when they type one, so it has to outrank a
    // subsequence that happens to contain the same letters.
    check(matchName("ticks", "tic").score > matchName("ticks", "tck").score,
          "the prefix scores above the scattered match");
    // An empty pattern matches everything, or the list would never open.
    check(matchName("ticks", "").matched, "an empty pattern matches");
  }

  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / "undoStudio-completion-navigation";
  std::filesystem::create_directories(dir);

  const auto fbPath = dir / "navFB.st";
  {
    std::ofstream out(fbPath);
    out << "FUNCTION_BLOCK navFB\n"
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
           "METHOD PUBLIC SetSpeed : BOOL\n"
           "VAR_INPUT\n"
           "    speed : INT;\n"
           "END_VAR\n"
           "VAR_IN_OUT\n"
           "    ramp : BOOL;\n"
           "END_VAR\n"
           "    SetSpeed := TRUE;\n"
           "END_METHOD\n"
           "END_FUNCTION_BLOCK\n";
  }
  const auto pouPath = dir / "navPOU.st";
  {
    std::ofstream out(pouPath);
    out << "PROGRAM navPOU\n"
           "VAR\n"
           "    motore : navFB;\n"
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
    app.renderEditorPanel();
    ImGui::EndFrame();
  }
  frameFocused(app);
  check(app.m_pouName == "navPOU", "the POU opened, got '" + app.m_pouName + "'");

  // --- a scattered prefix reaches the member through the UI ---
  place(app, "\nmotore.rst\n", 11);
  check(app.m_completionCandidates.size() == 1, "'rst' narrows to one candidate, got " +
                                                    std::to_string(app.m_completionCandidates.size()));
  if (app.m_completionCandidates.size() == 1) {
    check(app.m_completionCandidates[0].name == "Reset", "and it is 'Reset'");
  }

  // --- Down moves the selection and keeps the list open ---
  //
  // This is the reported bug. The frame below mutes the editor, so the list has
  // to survive on the keyboard ownership cached from the frame before.
  place(app, "\nmotore.\n", 7);
  const int count = static_cast<int>(app.m_completionCandidates.size());
  check(count > 1, "the list has something to navigate, got " + std::to_string(count));
  if (count > 1) {
    app.m_completionSelected = 0;
    const std::string beforeDown = app.m_bodyEditor->GetText();
    // The navigation request is read rather than the scrollbar: what the user saw
    // was the body moving, and the request is what moves it, whether or not this
    // layout happens to have a window for the focus to land on and scroll.
    const ImGuiDir downDir = frameWithKeyAndNav(app, ImGuiKey_DownArrow);
    check(downDir == ImGuiDir_None,
          std::string("Down asks ImGui's navigation for nothing while the list is open, got ") +
             dirName(downDir));
    check(app.m_completionEditor == app.m_bodyEditor.get(), "Down keeps the list open");
    check(app.m_completionSelected == 1, "Down moves the selection, got " +
                                            std::to_string(app.m_completionSelected));
    check(app.m_completionCandidates.size() == static_cast<std::size_t>(count),
          "Down does not disturb the candidate list");
    check(app.m_bodyEditor->GetText() == beforeDown,
          "Down leaves the text alone, got '" + app.m_bodyEditor->GetText() + "'");
    check(app.m_bodyEditor->GetCursorPosition().mColumn == 7,
          "Down leaves the cursor after the '.', got " +
             std::to_string(app.m_bodyEditor->GetCursorPosition().mColumn));
    check(app.m_bodyEditor->GetCursorPosition().mLine == 1,
          "Down does not walk the caret onto the next line, got line " +
             std::to_string(app.m_bodyEditor->GetCursorPosition().mLine));

    const ImGuiDir upDir = frameWithKeyAndNav(app, ImGuiKey_UpArrow);
    check(upDir == ImGuiDir_None,
          std::string("Up asks ImGui's navigation for nothing either, got ") + dirName(upDir));
    check(app.m_completionEditor == app.m_bodyEditor.get(), "Up keeps the list open");
    check(app.m_completionSelected == 0, "Up moves the selection back, got " +
                                            std::to_string(app.m_completionSelected));

    // PageDown is the only other navigation a list of this length needs. Home
    // and End are left to the editor on purpose: they are how a line is
    // navigated, and a list that took them would have to be dismissed first.
    const ImGuiDir pageDir = frameWithKeyAndNav(app, ImGuiKey_PageDown);
    check(pageDir == ImGuiDir_None,
          std::string("and neither does PageDown, got ") + dirName(pageDir));
    check(app.m_completionSelected > 0 && app.m_completionSelected < count,
          "PageDown jumps within the list, got " + std::to_string(app.m_completionSelected));
    check(app.m_completionEditor == app.m_bodyEditor.get(), "PageDown keeps the list open");
    frameWithKey(app, ImGuiKey_PageUp);
    check(app.m_completionSelected == 0, "PageUp comes back, got " +
                                              std::to_string(app.m_completionSelected));
  }

  // --- the arrow keys move the row, and the caret stays where it was ---
  //
  // Two different consumers of one press. The editor the list belongs to has the
  // key blocked, so the text is not touched; the navigation keys are claimed, so
  // ImGui's keyboard navigation is not touched either. Blocking alone was not
  // enough, and neither was claiming alone: what the user reported was the body
  // moving under the list, and that was the navigation half.
  const int lineBeforeArrow = app.m_bodyEditor->GetCursorPosition().mLine;
  frameWithKey(app, ImGuiKey_DownArrow);
  check(app.m_completionSelected == 1, "an arrow moves the row while the list is open, got row " +
                                           std::to_string(app.m_completionSelected));
  check(app.m_bodyEditor->GetCursorPosition().mLine == lineBeforeArrow,
        "and the caret does not follow it, got line " +
           std::to_string(app.m_bodyEditor->GetCursorPosition().mLine));
  frameWithKey(app, ImGuiKey_Escape);
  check(app.m_completionEditor == nullptr, "Escape closes the list");

  // The claim is the list's, and only while the list is up: the flag is not stood
  // down, so ImGui's navigation goes back to seeing arrows as soon as there is no
  // overlay to take them.
  const ImGuiDir freeDir = frameWithKeyAndNav(app, ImGuiKey_DownArrow);
  check(freeDir != ImGuiDir_None,
        std::string("with no list open the arrow is ImGui's again, got ") + dirName(freeDir));
  // What the editor does with an arrow when no list is open is ImGui's business
  // again, and the host's keyboard navigation flag is never touched to arrange it.
  // What stays checked here is the list: the row moves, the caret does not.

  // --- Enter accepts through the key path, and inserts no line break ---
  place(app, "\nmotore.re\n", 9);
  {
    const int i = indexOf(app.m_completionCandidates, "Reset");
    check(i >= 0, "'re' offers 'Reset'");
    if (i >= 0) app.m_completionSelected = i;
  }
  const std::string beforeEnter = app.m_bodyEditor->GetText();
  const int linesBefore = static_cast<int>(std::count(beforeEnter.begin(), beforeEnter.end(), '\n'));
  frameWithKey(app, ImGuiKey_Enter);
  check(app.m_bodyEditor->GetText().find("motore.Reset()") != std::string::npos,
        "Enter accepts the method, got '" + app.m_bodyEditor->GetText() + "'");
  const std::string afterEnter = app.m_bodyEditor->GetText();
  check(static_cast<int>(std::count(afterEnter.begin(), afterEnter.end(), '\n')) == linesBefore,
        "Enter reached the list and not the editor, which would have added a line break: got '" +
           afterEnter + "'");
  check(app.m_completionEditor == nullptr, "the list closes after accepting");

  // --- Escape dismisses, and does not spring back ---
  place(app, "\nmotore.\n", 7);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the list is open for Escape");
  frameWithKey(app, ImGuiKey_Escape);
  check(app.m_completionEditor == nullptr, "Escape closes the list");
  // The signature help takes the keyboard as well, so the arrows stop navigating
  // while it is up: the same press must not drag the panel along with it.
  place(app, "\nmotore.SetSpeed(1, \n", 19);
  check(app.m_signatureEditor == app.m_bodyEditor.get(), "the signature is open");
  // The cursor has not moved, so the dismissed spot is the same one. Putting the
  // list back there would make Escape useless: it would vanish and reappear in
  // the same frame, and the key would read as doing nothing.
  frameFocused(app);
  check(app.m_completionEditor == nullptr, "Escape keeps it closed while the cursor stays put");
  // Typing more is not putting it back the same spot: the prefix grew.
  app.m_bodyEditor->SetText("\nmotore.t\n");
  {
    TextEditor::Coordinates at;
    at.mLine = 1;
    at.mColumn = 8;
    app.m_bodyEditor->SetCursorPosition(at);
  }
  frameFocused(app);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the list returns for a new prefix");

  // --- the signature of a method call ---
  place(app, "\nmotore.SetSpeed(\n", 16);
  check(app.m_signatureEditor == app.m_bodyEditor.get(),
        std::string("the signature opens for a method call, got ") +
           (app.m_signatureEditor ? "the body editor" : "nothing"));
  if (app.m_signatureEditor == app.m_bodyEditor.get()) {
    check(app.m_signature.name == "SetSpeed", "it names the method, got '" + app.m_signature.name + "'");
    check(app.m_signature.params.size() == 2, "it lists both parameters, got " +
                                                  std::to_string(app.m_signature.params.size()));
    if (app.m_signature.params.size() == 2) {
      check(app.m_signature.params[0].name == "speed" && app.m_signature.params[0].direction == "IN",
            "the first parameter is the input 'speed', got '" + app.m_signature.params[0].name +
               "' '" + app.m_signature.params[0].direction + "'");
      check(app.m_signature.params[1].name == "ramp" && app.m_signature.params[1].direction == "IN_OUT",
            "the second is the in-out 'ramp', got '" + app.m_signature.params[1].name +
               "' '" + app.m_signature.params[1].direction + "'");
    }
    check(app.m_signatureArgument == 0, "the cursor is in the first argument, got " +
                                            std::to_string(app.m_signatureArgument));
  }

  // --- the signature follows the cursor between arguments ---
  place(app, "\nmotore.SetSpeed(1, \n", 19);
  if (app.m_signatureEditor == app.m_bodyEditor.get()) {
    check(app.m_signatureArgument == 1, "the cursor is in the second argument, got " +
                                            std::to_string(app.m_signatureArgument));
  } else {
    check(false, "the signature stays open across the first argument");
  }

  // --- the signature is gone once the call is ---
  place(app, "\nn := 1;\n", 8);
  check(app.m_signatureEditor == nullptr, "no signature outside a call");
  check(app.m_signature.params.empty(), "and the parameters are dropped with it");

  // --- a plain INT has no signature to show ---
  place(app, "\nabilita.Reset(\n", 14);
  check(app.m_signatureEditor == nullptr, "a scalar offers no method signature");

  // --- typing still reaches the editor while a key is blocked ---
  //
  // Blocking a key is not muting the editor: only the arrow, Enter, Tab and Escape
  // presses the overlays took are held back. A character typed in the same frame
  // has to arrive, or a list open at the time would swallow typing.
  place(app, "\nmotore.\n", 7);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the list is open for the typing check");
  {
    const std::string before = app.m_bodyEditor->GetText();
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_DownArrow, true); // taken by the list
    io.AddInputCharacter('z');                // and a character in the same frame
    runFrame(app);
    io.AddKeyEvent(ImGuiKey_DownArrow, false);
    runFrame(app);
    // The character lands at the caret, in the middle of the buffer, so the
    // expectation is the whole text rather than a suffix of it.
    check(app.m_bodyEditor->GetText() == "\nmotore.z\n\n",
          "a character typed in a frame where a key was blocked still arrives, got '" +
             app.m_bodyEditor->GetText() + "' instead of '" + before + "' with 'z' at the caret");
  }
  check(app.m_completionCandidates.empty(), "and the list closes once the prefix no longer matches");

  // --- the parameter hint follows a comma ---
  //
  // The popup is the only place the full signature lives, so the inline hint
  // after a comma is what makes an argument writable without a second look.
  place(app, "\nmotore.SetSpeed(1, \n", 19);
  check(app.m_signatureArgumentEmpty, "an empty argument is reported as empty, so the hint is drawn");

  // --- Tab picks the parameters of the call being written ---
  //
  // Nothing typed at the caret spells out the parameters, so the list has to be
  // asked for: the same widget, filled with the names the callee was declared with.
  place(app, "\nmotore.SetSpeed(\n", 16);
  check(app.m_signatureEditor == app.m_bodyEditor.get(), "the call is being written");
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "Tab opens a list of the parameters");
  check(app.m_completionIsParameterList, "and it is the parameter list, not a member list");
  check(app.m_completionCandidates.size() == 2, "which holds both parameters, got " +
                                                   std::to_string(app.m_completionCandidates.size()));
  // Guarded: a check that fails says so, while indexing past the end of an empty
  // list takes the whole test down and hides which of the checks before it failed.
  if (app.m_completionCandidates.size() == 2) {
    check(app.m_completionCandidates[0].name == "speed" && app.m_completionCandidates[1].name == "ramp",
          "in the order they were declared, got " + names(app));
    check(app.m_completionCandidates[0].parameterDirs.size() == 1 &&
              app.m_completionCandidates[0].parameterDirs.front() == "IN",
          "and the first carries its direction");
  }
  check(app.m_completionSelected == 0, "with the first one selected");

  // The keys are the ones the member list already answers to: nothing to learn twice.
  frameWithKey(app, ImGuiKey_DownArrow);
  check(app.m_completionSelected == 1, "Down walks the parameters, got " +
                                          std::to_string(app.m_completionSelected));
  frameWithKey(app, ImGuiKey_Tab);
  // The closing parenthesis is not in the buffer yet, and Tab does not write one:
  // the argument being named is what was asked for.
  check(app.m_bodyEditor->GetText() == "\nmotore.SetSpeed(ramp\n\n",
        "and Tab writes the chosen parameter, got '" + app.m_bodyEditor->GetText() + "'");
  check(app.m_completionEditor == nullptr, "the list closes once it has been used");

  // The next argument is offered the ones this call still owes, with the one just
  // given pushed down instead of gone: a value already written can be replaced by
  // name, which is the whole point of being able to name an argument at all.
  place(app, "\nmotore.SetSpeed(ramp, \n", 22);
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_completionCandidates.size() == 2, "Tab in the next argument lists them again");
  if (app.m_completionCandidates.size() == 2) {
    check(app.m_completionCandidates[0].name == "ramp", "the one still owed comes first, got " +
                                                            names(app));
    check(app.m_completionCandidates[1].name == "speed", "and the one already given follows, got " +
                                                              names(app));
  }
  if (app.m_completionCandidates.size() == 2) {
    check(app.m_completionCandidates[1].note == "given", "marked as given, got '" +
                                                            app.m_completionCandidates[1].note + "'");
    check(app.m_completionCandidates[1].kind == MemberAccess::Kind::Parameter,
          "and it is still a parameter, not a name from the scope");
  }

  frameWithKey(app, ImGuiKey_Escape);
  check(app.m_completionEditor == nullptr, "the list is closed before the next case");

  // A half-typed name narrows the parameters the way it narrows any other list.
  place(app, "\nmotore.SetSpeed(s\n", 17);
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_completionCandidates.size() == 1, "'s' narrows the parameters to one, got " +
                                                   std::to_string(app.m_completionCandidates.size()));
  if (!app.m_completionCandidates.empty()) {
    check(app.m_completionCandidates[0].name == "speed", "and it is the one starting with it, got " +
                                                              names(app));
  }
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_bodyEditor->GetText() == "\nmotore.SetSpeed(speed\n\n",
        "which replaces the half-typed name rather than doubling it, got '" +
           app.m_bodyEditor->GetText() + "'");

  // Nothing to choose from, so nothing to show: Tab stays a Tab.
  place(app, "\nmotore.ticks + \n", 15);
  check(app.m_signatureEditor == nullptr, "a scalar has no call to write arguments to");
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_completionEditor == nullptr, "and no list is offered where there are no parameters");
  check(app.m_bodyEditor->GetText() == "\nmotore.ticks + \t\n\n",
        "so Tab is still a tab character, got '" + app.m_bodyEditor->GetText() + "'");

  // Escape closes it, and it does not spring back on the next frame.
  place(app, "\nmotore.SetSpeed(\n", 16);
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the list is open for Escape");
  frameWithKey(app, ImGuiKey_Escape);
  check(app.m_completionEditor == nullptr, "Escape closes the parameter list");
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_completionEditor == app.m_bodyEditor.get(),
        "and Tab asks for it again, rather than it being stuck on");
  frameWithKey(app, ImGuiKey_Escape);

  // Leaving the call closes it, so the list cannot outlive what it describes.
  place(app, "\nmotore.SetSpeed(\n", 16);
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the list is open again");
  app.m_bodyEditor->SetCursorPosition(TextEditor::Coordinates{0, 0});
  frameFocused(app);
  check(app.m_completionEditor == nullptr, "and closes when the caret leaves the call");

  // --- Tab hands the screen over to the parameter list ---
  //
  // The signature and the parameter list are the same names, and two overlays
  // anchored to one cursor answer one question between them. Pressing Tab asks for
  // the parameters, so the signature goes away and the list has the screen to
  // itself; nothing behind it is forgotten, so it comes straight back.
  placeAtLine(app, "\nmotore.SetSpeed(\n", 1, 16);
  check(app.m_signatureEditor == app.m_bodyEditor.get(), "the signature is up before Tab");
  check(app.m_signatureRectMax.y > 0.0f, "and it has been drawn, " + span(ImRect(app.m_signatureRectMin,
                                                                                app.m_signatureRectMax)));
  frameWithKey(app, ImGuiKey_Tab);
  check(app.m_completionIsParameterList, "Tab asks for the parameters");
  check(app.m_signatureRectMax.y == 0.0f, "and the signature is not drawn any more");
  check(app.m_signatureEditor == app.m_bodyEditor.get(),
        "though it is still resolved, so it can come back");
  {
    // The list has the screen to itself now: below the cursor, and no longer
    // stacking under a signature that is not there.
    const ImRect list = listRect(app);
    const float caret = app.m_bodyEditor->GetCursorScreenPos().y;
    check(list.Min.y > caret, "so the list sits below the cursor on its own, list " + span(list) +
                                   " caret y=" + std::to_string(caret));
  }
  frameWithKey(app, ImGuiKey_Escape);
  check(app.m_signatureRectMax.y > 0.0f,
        "and the signature is back as soon as the list closes, it drew " +
           span(ImRect(app.m_signatureRectMin, app.m_signatureRectMax)));

  // --- the two overlays do not sit on top of each other ---
  //
  // Both are anchored to the same cursor and both want the same place. A cursor on
  // the first line of the editor has room below and none above, which is where the
  // signature help lands when it cannot sit above the line, so the parameter list
  // arriving after it used to be drawn underneath it. The rectangles are read back
  // from the windows, since where they land is what the user sees.
  // A list of names from the scope does still share the screen with the signature,
  // since it is the signature that is being answered: the list steps around it
  // rather than covering it.
  placeAtLine(app, "\nmotore.SetSpeed(mo\n", 1, 18);
  check(app.m_completionEditor == app.m_bodyEditor.get(), "the scope list is open inside the call, got " +
                                                             names(app));
  check(!app.m_completionIsParameterList, "and it is not the parameter list");
  {
    const ImRect signature = signatureRect(app);
    const ImRect list = listRect(app);
    check(signature.Max.y > signature.Min.y && list.Max.y > list.Min.y,
          "both overlays are on screen, signature " + span(signature) + " list " + span(list));
    check(!overlaps(signature, list),
          "and they do not cover each other, signature " + span(signature) + " list " + span(list));
  }
  frameWithKey(app, ImGuiKey_Escape);

  // With room above the cursor the signature keeps it, and the list takes the room
  // below, which is where the two were meant to be all along.
  placeAtLine(app, "\n\n\n\n\n\n\n\nmotore.SetSpeed(\n", 8, 16);
  frameWithKey(app, ImGuiKey_Tab);
  {
    const ImRect signature = signatureRect(app);
    const ImRect list = listRect(app);
    // Which one is on top is what matters here. The signature's box may reach a few
    // pixels into the caret's line, since ImGui hands a window the height its
    // content asks for rather than the height that was asked for, and the estimate
    // it is placed against is a little short.
    const float caret = app.m_bodyEditor->GetCursorScreenPos().y;
    check(signature.Min.y < caret && list.Min.y > caret,
          "the signature is the upper of the two and the list the lower, signature " + span(signature) +
             " list " + span(list) + " caret y=" + std::to_string(caret));
    check(!overlaps(signature, list), "so they are clear of each other too, signature " +
                                          span(signature) + " list " + span(list));
  }
  frameWithKey(app, ImGuiKey_Escape);

  // The list steps around the signature, never the other way round: the signature is
  // where it would have been on its own. An overlay cannot be looked up by absence,
  // since ImGui keeps a window around for a few frames after it stops being drawn,
  // so this compares the same window before and after instead.
  placeAtLine(app, "\nmotore.SetSpeed(\n", 1, 16);
  const ImRect alone = signatureRect(app);
  check(alone.Max.y > alone.Min.y, "the signature is drawn with no list beside it, " + span(alone));
  // A name from the scope brings a list that coexists with the signature, and that
  // list is the one that gives way: the signature stays exactly where it was.
  placeAtLine(app, "\nmotore.SetSpeed(mo\n", 1, 18);
  const ImRect withList = signatureRect(app);
  check(alone.Min.y == withList.Min.y && alone.Max.y == withList.Max.y,
        "and it does not move for a list that shares the screen with it, " + span(alone) + " then " +
           span(withList));

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);

  if (failures == 0) {
    std::printf("RESULT: all checks passed\n");
    return 0;
  }
  std::printf("RESULT: %d check(s) failed\n", failures);
  return 1;
}
