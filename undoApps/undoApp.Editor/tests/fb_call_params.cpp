/**
 * @file fb_call_params.cpp
 * @brief What a call to a function block instance offers as its parameters
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// Calling an instance, `mot(a)`, binds positional arguments against the
// block's interface: VAR_INPUT, VAR_OUTPUT and VAR_IN_OUT. Plain VAR,
// VAR_TEMP and VAR CONSTANT are the block's own state and are not
// arguments, so they must not turn up in the list.
//
// UI-free: this drives callSiteAt() and resolveCallSignature(), which is
// what the inline hint and the Tab parameter list are both built on.
#include "undoAppSTSemantic.hpp"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "semantic/SemanticAnalyzer.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

using namespace undoApp::ST;

int failures = 0;

static void check(bool ok, const std::string& what) {
  std::cout << (ok ? "  ok   " : "  FAIL ") << what << "\n";
  if (!ok) ++failures;
}

static st2cpp::semantic::SymbolTable analyze(const std::string& src) {
  Lexer lexer(src);
  Parser parser(lexer.tokenize());
  TranslationUnit tu = parser.parseTranslationUnit();
  st2cpp::semantic::SemanticAnalyzer analyzer;
  st2cpp::semantic::SemanticInfo info = analyzer.analyze(tu);
  (void)info;
  return *info.symbolTable;
}

static const std::vector<std::string> linesOf(const std::string& src) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : src) {
    if (c == '\n') { out.push_back(cur); cur.clear(); } else cur += c;
  }
  out.push_back(cur);
  return out;
}

// Position in the flattened source of where the cursor sits: right after
// the opening parenthesis of the call under test, i.e. inside its argument
// list, which is what callSiteAt needs to mark the call active.
static std::pair<int,int> caretOf(const std::string& src, const std::string& where) {
  const size_t at = src.find(where);
  if (at == std::string::npos) return {-1, -1};
  // Just past the last character of the marker, i.e. sitting on the
  // character that follows it. That is where the cursor is.
  size_t at2 = at + where.size();
  int line = 0, col = 0;
  for (size_t i = 0; i < at2; ++i) {
    if (src[i] == '\n') { ++line; col = 0; } else { ++col; }
  }
  return {line, col};
}

static bool resolveAt(const st2cpp::semantic::SymbolTable& st, const std::string& src,
                        const std::string& pou, int line, int col,
                        CallSignature& out, CallSite& callOut) {
  const CallSite call = callSiteAt(linesOf(src), line, col);
  callOut = call;
  if (!call.active) return false;
  const st2cpp::semantic::ScopeId scope = findMemberScopeFor(st, pou, false);
  if (scope == 0) return false;
  return resolveCallSignature(st, scope, call, out);
}

static const SignatureParam* findParam(const CallSignature& sig, const std::string& name) {
  for (const SignatureParam& p : sig.params) {
    if (p.name == name) return &p;
  }
  return nullptr;
}

static std::vector<std::string> paramNames(const CallSignature& sig) {
  std::vector<std::string> names;
  for (const SignatureParam& p : sig.params) names.push_back(p.name);
  return names;
}

static std::string join(const std::vector<std::string>& v) {
  std::string out;
  for (size_t i = 0; i < v.size(); ++i) {
    if (i != 0) out += ", ";
    out += v[i];
  }
  return out;
}

static const char* kSource =
    "FUNCTION_BLOCK Motore\n"
    "VAR_INPUT\n"
    "    abilita : BOOL;\n"
    "    velocita : INT := 0;\n"
    "END_VAR\n"
    "VAR_OUTPUT\n"
    "    allarme : BOOL;\n"
    "END_VAR\n"
    "VAR_IN_OUT\n"
    "    contatore : INT;\n"
    "END_VAR\n"
    "VAR\n"
    "    giriInterni : INT;\n"
    "END_VAR\n"
    "VAR CONSTANT\n"
    "    limite : INT := 10;\n"
    "END_VAR\n"
    "VAR_TEMP\n"
    "    appo : INT;\n"
    "END_VAR\n"
    "END_FUNCTION_BLOCK\n"
    "\n"
    "FUNCTION_BLOCK Derivato EXTENDS Motore\n"
    "VAR_INPUT\n"
    "    guadagno : REAL;\n"
    "END_VAR\n"
    "VAR\n"
    "    accumulo : REAL;\n"
    "END_VAR\n"
    "END_FUNCTION_BLOCK\n"
    "\n"
    "PROGRAM Principale\n"
    "VAR\n"
    "    mot : Motore;\n"
    "    der : Derivato;\n    x : INT;\n"
    "END_VAR\n"
    "mot(x);\n"
    "END_PROGRAM\n";

int main() {
  const st2cpp::semantic::SymbolTable st = analyze(kSource);
  std::cout << (failures == 0 ? "RESULT: all checks passed\n" : "RESULT: checks failed\n");
  return failures == 0 ? 0 : 1;
}
