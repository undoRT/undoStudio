#include "undoAppSTSemantic.hpp"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "semantic/SemanticAnalyzer.h"
#include <iostream>
#include <sstream>

using namespace undoApp::ST;
int failures = 0;
static void check(bool ok, const std::string& what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
  if (!ok) ++failures;
}

int main() {
  std::string src =
    "TYPE\n  Pt : STRUCT\n    x : INT;\n    y : INT;\n  END_STRUCT\nEND_TYPE\n"  // 1-5
    "FUNCTION_BLOCK FB1\n"                                                            // 6
    "VAR\n    speed : INT := 0;\n    name : STRING[32];\n    buf : ARRAY[0..9] OF INT;\n"
    "    p : Pt;\nEND_VAR\n"                                                            // 7-11
    "VAR CONSTANT\n    MAXV : INT := 9;\nEND_VAR\n"                                    // 12-14
    "METHOD Reset\n"                                                                  // 15
    "VAR_INPUT\n    hard : BOOL;\nEND_VAR\n"                                          // 16-18
    "    hard := FALSE;\nEND_METHOD\n"                                               // 19-20
    "speed := 1;\nEND_FUNCTION_BLOCK\n";                                              // 21-22

  std::vector<std::string> lines;
  { std::istringstream is(src); std::string l; while (std::getline(is,l)) lines.push_back(l); }

  Lexer lex(src);
  Parser parser(std::move(lex.tokenize()));
  auto tu = parser.parseTranslationUnit();
  st2cpp::semantic::SemanticAnalyzer a;
  auto info = a.analyze(tu);
  check(info.symbolTable != nullptr, "symbol table present");

  auto idx = collectDeclarations(tu, *info.symbolTable);
  std::cout << "declaration names: " << idx.size() << "\n";

  auto get = [&](const char* n) -> const std::vector<Declaration>* {
    auto it = idx.find(st2cpp::semantic::SymbolTable::asciiUpper(n));
    return it == idx.end() ? nullptr : &it->second;
  };

  // Each declaration must point at its own name on its own line.
  auto pointsAt = [&](const Declaration& d, const std::string& name) {
    if (d.line < 1 || d.line > (int)lines.size()) return false;
    int c = d.col;
    if (c < 0) { c = (int)lines[d.line-1].find(name); if (c < 0) return false; }
    return lines[d.line-1].compare(c, name.size(), name) == 0;
  };

  const auto* fb = get("FB1");
  check(fb && !fb->empty(), "FB1 declared");
  if (fb) check(pointsAt(fb->front(), "FB1"), "FB1 points at its name");
  if (fb) check(fb->front().kindText == "FUNCTION_BLOCK", "FB1 kindText");

  const auto* sp = get("SPEED");
  check(sp && !sp->empty(), "speed declared");
  if (sp) { check(pointsAt(sp->front(), "speed"), "speed points at its name");
            check(sp->front().typeText == "INT", "speed type INT, got '" + sp->front().typeText + "'");
            check(sp->front().kindText == "VAR", "speed kindText VAR"); }

  const auto* nm = get("NAME");
  if (nm) check(nm->front().typeText == "STRING[32]", "name type STRING[32], got '" + nm->front().typeText + "'");
  else check(false, "name declared");

  const auto* bf = get("BUF");
  if (bf) check(bf->front().typeText == "ARRAY[0..9] OF INT", "buf type, got '" + bf->front().typeText + "'");
  else check(false, "buf declared");

  const auto* mx = get("MAXV");
  if (mx) check(mx->front().category == SymCategory::Constant, "MAXV is Constant");
  else check(false, "MAXV declared");

  const auto* rs = get("RESET");
  check(rs && !rs->empty(), "Reset declared");
  if (rs) { check(rs->front().kindText == "METHOD", "Reset kindText METHOD");
            check(rs->front().scope == "Reset", "Reset scope"); }

  const auto* hd = get("HARD");
  check(hd && !hd->empty(), "hard declared (parameter)");
  if (hd) { check(hd->front().kindText == "PARAMETER", "hard kindText PARAMETER");
            check(hd->front().category == SymCategory::Parameter, "hard category Parameter");
            check(hd->front().scope == "Reset", "hard scope Reset"); }

  const auto* pt = get("PT");
  if (pt) check(pt->front().kindText == "STRUCT", "Pt kindText STRUCT");
  else check(false, "Pt declared");

  // Struct member must be reachable by its own name
  const auto* x = get("X");
  check(x && !x->empty(), "struct member x declared");

  std::cout << (failures ? "\nRESULT: FAILURES\n" : "\nRESULT: all checks passed\n");
  return failures ? 1 : 0;
}
