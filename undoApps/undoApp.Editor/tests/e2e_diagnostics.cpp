/**
 * @file e2e_diagnostics.cpp
 * @brief A diagnostic dump of the real STApp layout and its semantic token mapping
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include <imgui.h>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#define private public
#include <TextEditor.h>
#include "undoAppST.hpp"
#include "undoAppSTSemantic.hpp"
#undef private

using namespace undoApp::ST;

static const char* catName(SymCategory c) {
  switch (c) {
    case SymCategory::Variable: return "Variable";
    case SymCategory::Constant: return "Constant";
    case SymCategory::Parameter: return "Parameter";
    case SymCategory::Function: return "Function";
    case SymCategory::Type: return "Type";
    case SymCategory::Field: return "Field";
    case SymCategory::Enumerator: return "Enumerator";
    default: return "Unresolved";
  }
}

// A small POU with a method, so the dump shows variables, a METHOD tab and the
// semantic tokens landing on the editors.
static std::string writeDemoFixture() {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "undoStudio-e2e-diagnostics";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);
  { std::ofstream out(dir / "Demo.st");
    out << "PROGRAM Demo\n"
           "VAR\n"
           "    counter : INT;\n"
           "    limit : INT := 10;\n"
           "END_VAR\n"
           "counter := counter + 1;\n"
           "IF counter > limit THEN\n"
           "    counter := 0;\n"
           "END_IF;\n"
           "END_PROGRAM\n"; }
  return dir.string();
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  // Written by the test. It used to default to /tmp/opencode/ws/Demo.st, a path
  // that only existed on the machine that wrote it, so the parse reported
  // "L1: Expected POU name" against an empty file.
  const std::string inPath = writeDemoFixture() + "/Demo.st";
  std::string src;
  { std::ifstream f(inPath); std::stringstream ss; ss << f.rdbuf(); src = ss.str(); }

  STApp app;
  app.setupEditors();
  app.openFile(inPath);

  // Force a parse pass like the editor does.
  app.validateAndParse();

  // Opening a METHOD tab must paint it too.
  if (!app.m_methods.empty()) app.getOrCreateMethodEditors(app.m_methods[0]);

  { std::istringstream gs(app.generateSTFile()); std::string gl; int n=1;
    std::printf("=== generated file ===\n");
    while (std::getline(gs, gl)) std::printf("  L%02d| %s\n", n++, gl.c_str()); }

  // Idempotence: regenerating must not shift lines, or every keystroke would
  // move the error markers and semantic spans.
  { std::string a = app.generateSTFile();
    app.validateAndParse();
    std::string b = app.generateSTFile();
    std::printf("=== idempotence: %s (%zu vs %zu bytes) ===\n",
                (a == b) ? "STABLE" : "UNSTABLE", a.size(), b.size());
    if (a != b) { std::printf("--- first ---\n%s--- second ---\n%s", a.c_str(), b.c_str()); } }

  std::printf("=== segments ===\n");
  for (const auto& s : app.m_sourceMap)
    std::printf("  lines %2d..%-2d %-11s method='%s'\n", s.fullStart, s.fullEnd,
                s.isVariables ? "Variables" : "Body", s.methodName.c_str());

  std::printf("\n=== tokens landed on editors ===\n");
  int total = 0;
  auto dump = [&](const char* label, TextEditor* ed) {
    if (!ed) return;
    for (const auto& [line, spans] : ed->mSemanticTokens) {
      for (const auto& sp : spans) {
        const char* nm = "?";
        switch (sp.mColor) {
          case TextEditor::PaletteIndex::SemVariable: nm="Variable"; break;
          case TextEditor::PaletteIndex::SemConstant: nm="Constant"; break;
          case TextEditor::PaletteIndex::SemParameter: nm="Parameter"; break;
          case TextEditor::PaletteIndex::SemFunction: nm="Function"; break;
          case TextEditor::PaletteIndex::SemType: nm="Type"; break;
          case TextEditor::PaletteIndex::SemField: nm="Field"; break;
          case TextEditor::PaletteIndex::SemEnumerator: nm="Enumerator"; break;
          default: break;
        }
        std::printf("  %-10s line %2d cols %2d..%-2d -> %s\n", label, line, sp.mColStart, sp.mColEnd, nm);
        ++total;
      }
    }
  };
  dump("POU-var", app.m_variablesEditor.get());
  dump("POU-body", app.m_bodyEditor.get());
  for (auto& [n, e] : app.m_methodEditors) { dump((n+"-var").c_str(), e.variables.get()); dump((n+"-body").c_str(), e.body.get()); }

  std::printf("\ntotal tokens: %d\n", total);
  std::printf("errors: %d\n", (int)app.m_errors.size());
  if (app.m_semantic) {
    std::printf("st2cpp diagnostics: %d (errors=%d)\n",
                (int)app.m_semantic->diagnostics.totalCount(),
                (int)app.m_semantic->diagnostics.errorCount());
    for (const auto& dg : app.m_semantic->diagnostics.all())
      std::printf("   raw: line=%u code=%s msg=%s\n", dg.location.line,
                  diagnosticCodeToString(dg.code).c_str(), dg.message.c_str());
  } else std::printf("st2cpp diagnostics: NO SEMANTIC OBJECT\n");
  for (const auto& e : app.m_errors) std::printf("  L%d: %s\n", e.line, e.message.c_str());
  std::printf("--- output panel (errors/warnings) ---\n");
  for (const auto& l : app.m_outputLines) if (l.severity != OutSeverity::Info) std::printf("  [sev=%d] %s\n", (int)l.severity, l.text.c_str());

  ImGui::DestroyContext();
  return total > 0 ? 0 : 1;
}
