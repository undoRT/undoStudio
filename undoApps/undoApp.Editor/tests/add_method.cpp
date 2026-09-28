// Does creating a method actually add it?
//
// Reported from two places: the "+" on the editor tab bar, and "Add Method" in
// the tree's context menu. The two do not go through the same code at all: the
// tab opens a dialog, the tree used to call addMethod() directly with a fixed
// name and no dialog. This drives the direct call, to tell a failure in the data
// path apart from a failure in a window, a popup or a menu.
#include <imgui.h>

#include <algorithm>
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

static int countMethods(const STApp& app) {
  return static_cast<int>(app.m_methods.size());
}

static bool hasMethod(const STApp& app, const std::string& name) {
  for (const MethodData& m : app.m_methods) {
    if (m.name == name) {
      return true;
    }
  }
  return false;
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();
  ImGui::GetIO().ConfigInputTrickleEventQueue = false;

  const auto dir = std::filesystem::temp_directory_path() / "undoStudio-add-method";
  std::filesystem::create_directories(dir);
  const auto fbPath = dir / "addFB.st";
  {
    std::ofstream out(fbPath);
    out << "FUNCTION_BLOCK addFB\nVAR\n    n : INT;\nEND_VAR\nn := 1;\nEND_FUNCTION_BLOCK\n";
  }
  const auto pouPath = dir / "addPOU.st";
  {
    std::ofstream out(pouPath);
    out << "PROGRAM addPOU\nVAR\n    n : INT;\nEND_VAR\nn := 1;\nEND_PROGRAM\n";
  }

  // What the tree's context menu does, on a function block.
  {
    STApp app;
    app.setupEditors();
    app.openFile(fbPath.string());
    check(app.m_pouName == "addFB", "the function block opened, got '" + app.m_pouName + "'");
    const int before = countMethods(app);
    app.addMethod("NewMethod", "INT");
    check(countMethods(app) == before + 1, "addMethod adds one, got " + std::to_string(countMethods(app)) +
                                              " from " + std::to_string(before));
    check(hasMethod(app, "NewMethod"), "and the method is the one asked for");
    check(app.m_pendingTabSelection == "NewMethod", "and its tab is asked for, got '" +
                                                        app.m_pendingTabSelection + "'");
    check(app.m_methodEditors.count("NewMethod") == 1, "and it gets its editors, got " +
                                                          std::to_string(app.m_methodEditors.size()));
  }

  // The same call a second time, which is what a second click on the tree does.
  {
    STApp app;
    app.setupEditors();
    app.openFile(fbPath.string());
    app.addMethod("NewMethod", "INT");
    const int after = countMethods(app);
    app.addMethod("NewMethod", "INT");
    check(countMethods(app) == after, "a duplicate name adds nothing, got " +
                                         std::to_string(countMethods(app)) + " from " +
                                         std::to_string(after));
    app.addMethod("", "INT");
    check(countMethods(app) == after, "an empty name adds nothing, got " + std::to_string(countMethods(app)));
  }

  // A PROGRAM, which is what most .st files in the tree are.
  {
    STApp app;
    app.setupEditors();
    app.openFile(pouPath.string());
    const int before = countMethods(app);
    app.addMethod("NewMethod", "INT");
    check(countMethods(app) == before + 1, "addMethod works on a PROGRAM too, got " +
                                              std::to_string(countMethods(app)) + " from " +
                                              std::to_string(before));
  }

  // What the tree's context menu does now, and what the "+" tab does: ask for the
  // dialog, and let the ST Editor panel be the one that opens it.
  {
    STApp app;
    app.setupEditors();
    app.openFile(fbPath.string());
    app.m_newMethodName = "leftover";
    app.requestAddMethodDialog();
    check(app.m_addMethodDialogPending, "the request from the tree is pending");
    check(app.consumeAddMethodDialogRequest(), "the ST Editor panel takes it");
    check(!app.consumeAddMethodDialogRequest(), "and only once, so the popup cannot be re-armed");
    check(!app.m_addMethodDialogPending, "the request is cleared once taken");
    check(app.m_newMethodName.empty() && app.m_newMethodReturnType.empty(),
          "the dialog starts from empty fields, got '" + app.m_newMethodName + "'");
    check(countMethods(app) == 0, "and nothing was added yet, got " + std::to_string(countMethods(app)));
  }

  // The "+" tab is in the ST Editor window itself, so it can still arm the popup
  // directly, and the flag has to be cleared or the popup comes straight back.
  {
    STApp app;
    app.setupEditors();
    app.m_showAddMethodPopup = true;
    check(!app.consumeAddMethodDialogRequest(), "the tab's own request is not the tree's request");
    check(app.m_showAddMethodPopup, "the tab's flag is a separate one and is left alone");
    app.m_showAddMethodPopup = false;
    check(!app.m_showAddMethodPopup, "and clearing it is what stops the popup reopening");
  }

  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::printf("%s\n", failures == 0 ? "RESULT: all checks passed" : "RESULT: checks failed");
  return failures == 0 ? 0 : 1;
}
