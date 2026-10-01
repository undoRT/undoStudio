/**
 * @file fb_shadowing.cpp
 * @brief A diagnostic dump of findDeclaration: a method call, and a name a method shadows
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <imgui.h>
#include <cstdio>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <memory>
#include <functional>
#include <unordered_map>
#include <filesystem>
#include <algorithm>

#define private public
#include <TextEditor.h>
#include "undoAppST.hpp"
#include "workspace_fixture.hpp"
#undef private

using namespace undoApp::ST;
static void dump(STApp& app) {
  std::printf("\n--- declarations for this file ---\n");
  for (const auto& [key, list] : app.m_declarations) {
    for (const auto& d : list)
      std::printf("  %-12s line=%3d col=%3d scope='%s' kind='%s' name='%s' cat=%d\n",
                  key.c_str(), d.line, d.col, d.scope.c_str(), d.kindText.c_str(),
                  d.name.c_str(), (int)d.category);
  }
  std::printf("\n--- source map ---\n");
  for (const auto& s : app.m_sourceMap)
    std::printf("  lines %2d..%-2d %-11s method='%s'\n", s.fullStart, s.fullEnd,
                s.isVariables ? "Vars" : "Body", s.methodName.c_str());
  std::printf("\n--- methods known to STApp ---\n");
  for (const auto& m : app.m_methods)
    std::printf("  '%s' : '%s'\n", m.name.c_str(), m.returnType.c_str());
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  STApp app;
  app.setupEditors();
  // The fixture is written by the test; this used to read the real
  // ~/Desktop/progettiUndoStudio project, so it inspected nothing off that machine.
  const std::string root = undoApp::tests::writeBlockFixture("undoStudio-fb-shadowing").string();
  app.requestOpenFile(root + "/undoFB.st");
  std::printf("opened: %s\n", app.m_currentFilePath.c_str());
  std::printf("errors: %d\n", (int)app.m_errors.size());
  for (const auto& e : app.m_errors) std::printf("  L%d %s\n", e.line, e.message.c_str());
  dump(app);

  // Clicking the method call in the POU body must switch to its tab.
  const Declaration* meth = app.findDeclaration("undoMeth", "");
  std::printf("\nfindDeclaration('undoMeth', '') -> %s\n", meth ? "found" : "NOT FOUND");
  if (meth) {
    std::printf("   kind='%s' scope='%s' name='%s' line=%d\n", meth->kindText.c_str(),
                meth->scope.c_str(), meth->name.c_str(), meth->line);
    const bool ok = app.revealMethodTab(meth->scope);
    std::printf("   revealMethodTab('%s') -> %s, activeTab now '%s'\n",
                meth->scope.c_str(), ok ? "true" : "false", app.m_activeTab.c_str());
    std::printf("   revealDeclaration -> %s\n", app.revealDeclaration(*meth) ? "true" : "false");
  }

  // Shadowing: from the POU body, undoVar must resolve to the POU declaration (line 10).
  const Declaration* v = app.findDeclaration("undoVar", "");
  std::printf("\nfindDeclaration('undoVar', '') -> line %d scope='%s'\n", v ? v->line : -1, v ? v->scope.c_str() : "");
  const Declaration* vm = app.findDeclaration("undoVar", "undoMeth");
  std::printf("findDeclaration('undoVar', 'undoMeth') -> line %d scope='%s'\n", vm ? vm->line : -1, vm ? vm->scope.c_str() : "");

  ImGui::DestroyContext();
  return 0;
}
