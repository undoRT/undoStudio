/**
 * @file project_registry.cpp
 * @brief A diagnostic dump of the project registry and of the descriptor built directly
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

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().DisplaySize = ImVec2(1280, 800);
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  // The fixture is written by the test; this used to read the real
  // ~/Desktop/progettiUndoStudio project.
  const std::string dir = undoApp::tests::writeBlockFixture("undoStudio-project-registry").string();
  STApp app;
  app.setupEditors();
  app.requestOpenFile(dir + "/undoPRG.st");
  app.ensureProjectRegistry();

  std::printf("registry: %p\n", (void*)app.m_projectRegistry.get());
  if (app.m_projectRegistry) {
    std::printf("libraries: %zu\n", app.m_projectRegistry->all().size());
    for (const auto* d : app.m_projectRegistry->allOrdered()) {
      std::printf("  id='%s' name='%s' fbs=%zu types=%zu enums=%zu funcs=%zu\n",
                  d->id.c_str(), d->name.c_str(), d->functionBlocks.size(),
                  d->types.size(), d->enums.size(), d->functions.size());
      for (const auto& fb : d->functionBlocks) std::printf("     FB '%s'\n", fb.name.c_str());
    }
  }
  std::printf("workspace decl names: %zu\n", app.m_workspaceDecls.size());

  // What does the builder produce for the FB file on its own?
  {
    std::ifstream f(dir + "/undoFB.st");
    std::stringstream b; b << f.rdbuf();
    Lexer lex(b.str());
    Parser p(std::move(lex.tokenize()));
    auto tu = p.parseTranslationUnit();
    st2cpp::semantic::SemanticAnalyzer a;
    auto info = a.analyze(tu);
    st2cpp::semantic::LibraryExportOptions o;
    o.id = "x"; o.name = "x"; o.version = "0.0.0";
    auto r = st2cpp::semantic::LibraryDescriptorBuilder::build(tu, info, o);
    std::printf("direct build: ok=%d descriptor=%d errors=%zu\n", (int)r.ok(), (int)r.descriptor.has_value(), r.errors.size());
    for (const auto& e : r.errors) std::printf("   export error: %s\n", e.toString().c_str());
    if (r.descriptor) std::printf("   fbs=%zu types=%zu enums=%zu\n",
        r.descriptor->functionBlocks.size(), r.descriptor->types.size(), r.descriptor->enums.size());
  }
  ImGui::DestroyContext();
  return 0;
}
