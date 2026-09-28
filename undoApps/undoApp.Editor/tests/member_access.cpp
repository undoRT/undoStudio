// Exercises the UI-free member-access layer: what the '.' operator can reach
// on a function block instance, and how a line is read at the cursor.
//
// Deliberately does not create an ImGui context, so it runs without a display.
#include "undoAppSTSemantic.hpp"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "semantic/SemanticAnalyzer.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace undoApp::ST;

int failures = 0;

static void check(bool ok, const std::string& what) {
  if (!ok) {
    std::cout << "  FAIL: " << what << "\n";
    ++failures;
  }
}

static const MemberAccess* findMember(const MemberList& list, const std::string& name) {
  const std::string want = st2cpp::semantic::SymbolTable::normalizeKey(name);
  for (const auto& m : list) {
    if (st2cpp::semantic::SymbolTable::normalizeKey(m.name) == want) return &m;
  }
  return nullptr;
}

static int countKind(const MemberList& list, MemberAccess::Kind kind) {
  int n = 0;
  for (const auto& m : list) {
    if (m.kind == kind) ++n;
  }
  return n;
}

// Parse + analyze a snippet, then hand back the symbol table.
static st2cpp::semantic::SymbolTable analyze(const std::string& src) {
  Lexer lexer(src);
  Parser parser(lexer.tokenize());
  TranslationUnit tu = parser.parseTranslationUnit();
  st2cpp::semantic::SemanticAnalyzer analyzer;
  st2cpp::semantic::SemanticInfo info = analyzer.analyze(tu);
  return *info.symbolTable;
}

static const char* kFb =
    "FUNCTION_BLOCK Base\n"
    "VAR_INPUT\n"
    "    abilita : BOOL;\n"
    "END_VAR\n"
    "VAR_OUTPUT\n"
    "    allarme : BOOL;\n"
    "END_VAR\n"
    "VAR\n"
    "    ticks : INT;\n"
    "END_VAR\n"
    "METHOD PUBLIC Reset : BOOL\n"
    "END_METHOD\n"
    "END_FUNCTION_BLOCK\n"
    "\n"
    "FUNCTION_BLOCK Derivato EXTENDS Base\n"
    "VAR_INPUT\n"
    "    gain : REAL;\n"
    "END_VAR\n"
    "VAR\n"
    "    acc : REAL;\n"
    "END_VAR\n"
    "VAR CONSTANT\n"
    "    limite : INT := 10;\n"
    "END_VAR\n"
    "METHOD PROTECTED Step : BOOL\n"
    "END_METHOD\n"
    "END_FUNCTION_BLOCK\n";

int main() {
  // ------------------------------------------------------------------
  // 1. A derived block exposes its own and its inherited interface
  // ------------------------------------------------------------------
  {
    auto st = analyze(kFb);
    st2cpp::semantic::SymbolId derived = st.lookupGlobal("Derivato");
    check(derived != 0, "Derivato resolves");
    MemberList members = collectBlockMembers(st, derived);

    const MemberAccess* gain = findMember(members, "gain");
    check(gain != nullptr, "own input 'gain' is offered");
    if (gain) {
      check(gain->kind == MemberAccess::Kind::Parameter, "gain is a parameter");
      check(!gain->inherited, "gain is not marked inherited");
      check(gain->typeText == "REAL", "gain is REAL, got " + gain->typeText);
    }

    const MemberAccess* abilita = findMember(members, "abilita");
    check(abilita != nullptr, "inherited input 'abilita' is offered");
    if (abilita) {
      check(abilita->inherited, "abilita is marked inherited");
      check(abilita->declaredIn == "Base", "abilita is declared in Base, got " + abilita->declaredIn);
    }

    const MemberAccess* allarme = findMember(members, "allarme");
    check(allarme != nullptr, "inherited output 'allarme' is offered");
    if (allarme) {
      check(allarme->inherited && allarme->declaredIn == "Base", "allarme comes from Base");
    }

    const MemberAccess* acc = findMember(members, "acc");
    check(acc != nullptr, "own state 'acc' is offered");
    if (acc) {
      check(acc->kind == MemberAccess::Kind::State, "acc is state, not a parameter");
      check(!acc->inherited, "acc is not inherited");
    }

    const MemberAccess* limite = findMember(members, "limite");
    check(limite != nullptr, "VAR CONSTANT 'limite' is offered");
    if (limite) {
      check(limite->isConstant, "limite is flagged constant");
    }

    const MemberAccess* step = findMember(members, "Step");
    check(step != nullptr, "own method 'Step' is offered");
    if (step) {
      check(step->kind == MemberAccess::Kind::Method, "Step is a method");
      check(!step->inherited, "Step is not inherited");
      check(step->typeText == "BOOL", "Step returns BOOL, got " + step->typeText);
    }

    const MemberAccess* reset = findMember(members, "Reset");
    check(reset != nullptr, "inherited method 'Reset' is offered");
    if (reset) {
      check(reset->inherited, "Reset is marked inherited");
      check(reset->declaredIn == "Base", "Reset is declared in Base");
    }

    // 2 params + 2 params inherited + acc + limite + 2 methods
    check(countKind(members, MemberAccess::Kind::Parameter) == 3,
          "3 parameters (2 inherited), got " +
             std::to_string(countKind(members, MemberAccess::Kind::Parameter)));
    check(countKind(members, MemberAccess::Kind::State) == 3,
          "3 state entries (ticks, acc, limite), got " +
             std::to_string(countKind(members, MemberAccess::Kind::State)));
    check(countKind(members, MemberAccess::Kind::Method) == 2, "2 methods");

    // A base block does not see what the derived one adds.
    MemberList baseMembers = collectBlockMembers(st, st.lookupGlobal("Base"));
    check(findMember(baseMembers, "gain") == nullptr, "Base does not offer the derived 'gain'");
    check(findMember(baseMembers, "acc") == nullptr, "Base does not offer the derived 'acc'");
    check(findMember(baseMembers, "ticks") != nullptr, "Base offers its own 'ticks'");

    // A non-block symbol yields nothing rather than a partial listing.
    check(collectBlockMembers(st, 0).empty(), "symbol 0 yields no members");
    check(collectBlockMembers(st, st.lookupGlobal("Base")) .empty() == false,
          "Base yields members");
  }

  // ------------------------------------------------------------------
  // 2. A redeclared parameter is listed once, as the derived declaration
  // ------------------------------------------------------------------
  {
    std::string src =
        "FUNCTION_BLOCK A\n"
        "VAR_INPUT\n"
        "    comune : INT;\n"
        "END_VAR\n"
        "END_FUNCTION_BLOCK\n"
        "FUNCTION_BLOCK B EXTENDS A\n"
        "VAR_INPUT\n"
        "    comune : DINT;\n"
        "END_VAR\n"
        "END_FUNCTION_BLOCK\n";
    auto st = analyze(src);
    MemberList members = collectBlockMembers(st, st.lookupGlobal("B"));
    int count = 0;
    const MemberAccess* found = nullptr;
    for (const auto& m : members) {
      if (st2cpp::semantic::SymbolTable::normalizeKey(m.name) == "COMUNE") {
         ++count;
         found = &m;
      }
    }
    check(count == 1, "redeclared parameter appears once, got " + std::to_string(count));
    if (found) {
      check(found->typeText == "DINT", "the derived type wins, got " + found->typeText);
      check(!found->inherited, "a redeclaration is not marked inherited");
    }
  }

  // ------------------------------------------------------------------
  // 3. findInstanceBlock resolves the block behind a variable
  // ------------------------------------------------------------------
  {
    std::string src =
        "FUNCTION_BLOCK Motor\n"
        "VAR_INPUT\n"
        "    rpm : INT;\n"
        "END_VAR\n"
        "END_FUNCTION_BLOCK\n"
        "\n"
        "PROGRAM Main\n"
        "VAR\n"
        "    motore : Motor;\n"
        "    velocita : INT;\n"
        "END_VAR\n"
        "END_PROGRAM\n";
    auto st = analyze(src);
    // The PROGRAM's scope is the one the body sees.
    st2cpp::semantic::ScopeId progScope = 0;
    for (const auto& scope : st.getScopes()) {
      if (scope.name == "PROG_Main") progScope = scope.id;
    }
    check(progScope != 0, "the PROGRAM scope exists");

    st2cpp::semantic::SymbolId fb = findInstanceBlock(st, progScope, "motore");
    check(fb != 0, "'motore' resolves to a function block instance");
    if (fb != 0) {
      const st2cpp::semantic::Symbol* sym = st.get(fb);
      check(sym != nullptr && sym->name == "Motor", "the instance resolves to Motor");
    }
    check(findInstanceBlock(st, progScope, "velocita") == 0, "a plain INT is not an instance");
    check(findInstanceBlock(st, progScope, "inesistente") == 0, "an unknown name is not an instance");
    check(findInstanceBlock(st, progScope, "") == 0, "an empty name is not an instance");
  }

  // ------------------------------------------------------------------
  // 4. memberAccessPointAt reads the cursor position
  // ------------------------------------------------------------------
  {
    // Columns are 0-based and the cursor sits *after* the character it follows.
    struct Case { const char* line; int col; bool active; const char* object; const char* prefix; };
    const Case cases[] = {
        {"    motore.",    11, true,  "motore", ""},
        {"    motore.re",  13, true,  "motore", "re"},
        {"    motore.Reset()", 11, true, "motore", ""},
        {"motore.gain := 1.0;", 7, true, "motore", ""},
        // A plain INT is still a syntactically valid access point: whether the
        // object is a block is decided later by findInstanceBlock, not here.
        {"    velocita.",  13, true,  "velocita", ""},
        {"    42.",         7, false, "", ""},    // a literal is not an object
        {"    f(x).",      9, false, "", ""},    // a call result is not either
        {"    a.b.c",      9, true,  "b", "c"}, // resolves on b, not on a
        {"    a.b.",       8, true,  "b", ""},   // a chain completes on b too
        {"    .",          5, false, "", ""},    // nothing before the dot
        {"    motore",     11, false, "", ""},   // no dot typed yet
    };
    for (const auto& c : cases) {
      MemberAccessPoint p = memberAccessPointAt(c.line, c.col);
      check(p.active == c.active,
            std::string("'") + c.line + "' at " + std::to_string(c.col) +
               " active should be " + (c.active ? "true" : "false"));
      if (p.active && c.active) {
         check(p.objectName == c.object,
               std::string("object name of '") + c.line + "' should be " + c.object + ", got " + p.objectName);
         check(p.prefix == c.prefix,
               std::string("prefix of '") + c.line + "' should be '" + c.prefix + "', got '" + p.prefix + "'");
      }
    }

    // Prefix filtering is case-insensitive, as IEC identifiers are.
    MemberAccess fbMembers = {};
    fbMembers.name = "Reset";
    check(fbMembers.matches(""), "an empty prefix matches everything");
    check(fbMembers.matches("re"), "a lowercase prefix matches");
    check(fbMembers.matches("RESET"), "an uppercase prefix matches");
    check(!fbMembers.matches("xyz"), "an unrelated prefix does not match");
    check(!fbMembers.matches("resets"), "a longer prefix does not match");
  }

  // ------------------------------------------------------------------
  // 5. describeMember renders the completion label
  // ------------------------------------------------------------------
  {
    MemberAccess method;
    method.name = "Reset";
    method.kind = MemberAccess::Kind::Method;
    method.typeText = "BOOL";
    method.parameterTypes = {"INT", "BOOL"};
    check(describeMember(method) == "Reset(INT, BOOL) : BOOL",
          "method label, got " + describeMember(method));

    MemberAccess state;
    state.name = "ticks";
    state.kind = MemberAccess::Kind::State;
    state.typeText = "INT";
    check(describeMember(state) == "ticks : INT", "state label, got " + describeMember(state));

    MemberAccess noType;
    noType.name = "Reset";
    noType.kind = MemberAccess::Kind::Method;
    check(describeMember(noType) == "Reset()", "a method with no return type, got " + describeMember(noType));
  }

  if (failures == 0) {
    std::cout << "RESULT: all checks passed\n";
    return 0;
  }
  std::cout << failures << " check(s) failed\n";
  return 1;
}
