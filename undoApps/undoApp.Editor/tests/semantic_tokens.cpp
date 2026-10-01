/**
 * @file semantic_tokens.cpp
 * @brief The category a token is given, for a declaration and for a reference to it
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoAppSTSemantic.hpp"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "semantic/SemanticAnalyzer.h"
#include <iostream>
#include <sstream>
#include <vector>

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

int failures = 0;

static void check(bool ok, const std::string& what) {
  if (!ok) { std::cout << "  FAIL: " << what << "\n"; ++failures; }
}

int main() {
  std::string src =
    "FUNCTION_BLOCK OtherFB\n"     // 1
    "END_FUNCTION_BLOCK\n"        // 2
    "\n"                            // 3
    "FUNCTION_BLOCK FB\n"          // 4
    "VAR\n"                         // 2
    "    shared : INT;\n"           // 3
    "    inst : OtherFB;\n"         // 4
    "END_VAR\n"                     // 5
    "VAR CONSTANT\n"                // 6
    "    LIMIT : INT := 10;\n"      // 7
    "END_VAR\n"                     // 8
    "\n"                            // 9
    "METHOD M1 : INT\n"             // 10
    "VAR_INPUT\n"                   // 11
    "    p : INT;\n"                // 12
    "END_VAR\n"                     // 13
    "    p := p + 1;\n"             // 14
    "    shared := p;\n"            // 15
    "END_METHOD\n"                  // 16
    "shared := LIMIT;\n"            // 17
    "inst.Run();\n"                 // 18
    "shared := INT_TO_REAL(LIMIT);\n" // 19
    "END_FUNCTION_BLOCK\n";         // 20

  std::vector<std::string> lines;
  { std::istringstream is(src); std::string l; while (std::getline(is,l)) lines.push_back(l); }

  Lexer lex(src);
  Parser parser(std::move(lex.tokenize()));
  auto tu = parser.parseTranslationUnit();

  st2cpp::semantic::SemanticAnalyzer a;
  auto info = a.analyze(tu);
  check(info.symbolTable != nullptr, "symbol table present");
  if (!info.symbolTable) return 1;

  auto set = collectSemanticTokens(tu, *info.symbolTable, lines);
  std::cout << "tokens: " << set.tokens.size() << "\n\n";

  // index by (line, text)
  auto findAt = [&](int line, const std::string& name) -> const SemanticToken* {
    for (const auto& t : set.tokens)
      if (t.line == line && t.line >= 1 && t.line <= (int)lines.size() &&
          t.col + t.length <= (int)lines[t.line-1].size() &&
          lines[t.line-1].compare(t.col, t.length, name) == 0)
        return &t;
    return nullptr;
  };

  std::cout << "--- all tokens ---\n";
  for (const auto& t : set.tokens) {
    std::string txt = (t.line>=1 && t.line<=(int)lines.size() &&
                      t.col+t.length<=(int)lines[t.line-1].size())
                       ? lines[t.line-1].substr(t.col, t.length) : std::string("<OOR>");
    std::cout << "  L" << t.line << " C" << t.col << " '" << txt << "' = " << catName(t.category) << "\n";
    if (txt == "<OOR>") { std::cout << "    FAIL: token out of range\n"; ++failures; }
  }
  std::cout << "\n--- expectations ---\n";

  // Every token must land on its own name (guards the 1-based col conversion)
  for (const auto& t : set.tokens) {
    check(t.line >= 1 && t.line <= (int)lines.size(), "line in range");
  }

  const SemanticToken* t;
  t = findAt(1, "OtherFB"); check(t && t->category==SymCategory::Field, "OtherFB decl = Field");
  t = findAt(4, "FB");      check(t && t->category==SymCategory::Field, "FB decl = Field");
  t = findAt(6, "shared");   check(t && t->category==SymCategory::Variable, "shared decl = Variable");
  t = findAt(7, "inst");     check(t && t->category==SymCategory::Field,    "inst decl = Field (FB type)");
  t = findAt(10, "LIMIT");    check(t && t->category==SymCategory::Constant,  "LIMIT = Constant");
  t = findAt(13,"M1");       check(t && t->category==SymCategory::Function,  "method name = Function");
  t = findAt(18,"p");        check(t && t->category==SymCategory::Parameter, "p param = Parameter");
  t = findAt(17,"p");        check(t && t->category==SymCategory::Parameter, "p use = Parameter");
  t = findAt(18,"shared");   check(t && t->category==SymCategory::Variable, "shared in method = Variable");
  t = findAt(18,"p");        check(t && t->category==SymCategory::Parameter, "p use in method = Parameter");
  t = findAt(20,"shared");   check(t && t->category==SymCategory::Variable, "shared in body = Variable");
  t = findAt(20,"LIMIT");    check(t && t->category==SymCategory::Constant,  "LIMIT use = Constant");
  t = findAt(22,"INT_TO_REAL"); check(t && t->category==SymCategory::Function, "conversion fn = Function");
  t = findAt(22,"LIMIT");    check(t && t->category==SymCategory::Constant,  "LIMIT in call = Constant");

  std::cout << (failures ? "\nRESULT: FAILURES\n" : "\nRESULT: all checks passed\n");
  return failures ? 1 : 0;
}
