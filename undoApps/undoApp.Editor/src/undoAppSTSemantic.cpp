/**
 * @file undoAppSTSemantic.cpp
 * @brief Resolved-symbol extraction for ST syntax highlighting
 * @ingroup undoapps
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoAppSTSemantic.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace undoApp {
namespace ST {

namespace {

using st2cpp::semantic::Scope;
using st2cpp::semantic::ScopeId;
using st2cpp::semantic::Symbol;
using st2cpp::semantic::SymbolId;
using st2cpp::semantic::SymbolKind;
using st2cpp::semantic::SymbolTable;
using st2cpp::semantic::TypeId;
using st2cpp::semantic::TypeInfo;
using st2cpp::semantic::TypeKind;

/**
 * @brief Occurrence reported by the AST walkers
 */
struct Occurrence
{
   int line = 0;     ///< 1-based
   int col = 0;      ///< 0-based
   std::string name; ///< Identifier text, also the token width
};

using OccurrenceSink = std::function<void(const Occurrence&)>;

/**
 * @brief Generated source, 1-based, for nodes without a position
 */
struct Source
{
   const std::vector<std::string>* lines = nullptr;

   std::string lineAt(int line) const
   {
      if (!lines || line < 1 || line > (int)lines->size()) {
         return std::string();
      }
      return (*lines)[line - 1];
   }
};

/**
 * @brief Map a symbol to the category used for coloring
 * @param sym    Symbol to classify
 * @param symTab Table owning the symbol, used to inspect its type
 *
 * A variable whose type is a FUNCTION_BLOCK is reported as a Field: in an IEC
 * editor an instance behaves differently from a plain variable, and telling
 * them apart at a glance is the point of semantic highlighting.
 */
SymCategory categoryOf(const Symbol& sym, const SymbolTable& symTab)
{
   switch (sym.kind) {
   case SymbolKind::Function:
   case SymbolKind::Method:
      return SymCategory::Function;
   case SymbolKind::Type:
   case SymbolKind::Interface:
      return SymCategory::Type;
   case SymbolKind::FunctionBlock:
   case SymbolKind::Program:
   case SymbolKind::StructMember:
      return SymCategory::Field;
   case SymbolKind::Enumerator:
      return SymCategory::Enumerator;
   case SymbolKind::Parameter:
      return SymCategory::Parameter;
   case SymbolKind::Variable: {
      if (const auto* type = symTab.getType(sym.typeId)) {
         if (type->kind == st2cpp::semantic::TypeKind::FunctionBlock) {
            return SymCategory::Field;
         }
      }
      return sym.isConstant ? SymCategory::Constant : SymCategory::Variable;
   }
   }
   return SymCategory::Variable;
}

/**
 * @brief Walk an expression tree, reporting every identifier occurrence
 *
 * st2cpp columns are 1-based, so they are converted to 0-based here.
 */
void walkExpr(const std::shared_ptr<Expr>& expr, const Source& src, const OccurrenceSink& sink)
{
   if (!expr) {
      return;
   }

   const int line = (int)expr->line;
   const int col = (int)expr->col - 1;

   std::visit(
      [&](const auto& node) {
         using T = std::decay_t<decltype(node)>;

         if constexpr (std::is_same_v<T, IdentExpr>) {
            sink({line, col, node.name});

         } else if constexpr (std::is_same_v<T, MemberExpr>) {
            walkExpr(node.object, src, sink);
            // A member access has no position of its own: the member follows the
            // last '.' on the line, which is where ST member access sits.
            const std::string text = src.lineAt(line);
            const size_t dot = text.rfind('.');
            const size_t at = text.find(node.member, (dot == std::string::npos) ? 0 : dot + 1);
            if (at != std::string::npos) {
               sink({line, (int)at, node.member});
            }

         } else if constexpr (std::is_same_v<T, CallExpr>) {
            walkExpr(node.callee, src, sink);
            for (const auto& arg : node.args) {
               walkExpr(arg.value, src, sink);
            }

         } else if constexpr (std::is_same_v<T, SuperCallExpr>) {
            const size_t at = src.lineAt(line).rfind(node.methodName);
            if (at != std::string::npos) {
               sink({line, (int)at, node.methodName});
            }

         } else if constexpr (std::is_same_v<T, CastExpr>) {
            walkExpr(node.operand, src, sink);

         } else if constexpr (std::is_same_v<T, SizeofExpr>) {
            walkExpr(node.expr, src, sink);

         } else if constexpr (std::is_same_v<T, IndexExpr>) {
            walkExpr(node.array, src, sink);
            for (const auto& idx : node.indices) {
               walkExpr(idx, src, sink);
            }

         } else if constexpr (std::is_same_v<T, UnaryExpr>) {
            walkExpr(node.operand, src, sink);

         } else if constexpr (std::is_same_v<T, AdrExpr>) {
            walkExpr(node.operand, src, sink);

         } else if constexpr (std::is_same_v<T, DerefExpr>) {
            walkExpr(node.pointer, src, sink);

         } else if constexpr (std::is_same_v<T, BinaryExpr>) {
            walkExpr(node.left, src, sink);
            walkExpr(node.right, src, sink);

         } else if constexpr (std::is_same_v<T, StructInitExpr>) {
            for (const auto& m : node.members) {
               walkExpr(m.value, src, sink);
            }

         } else if constexpr (std::is_same_v<T, ArrayInitExpr>) {
            for (const auto& el : node.elements) {
               walkExpr(el, src, sink);
            }
         }
      },
      expr->node);
}

/**
 * @brief Walk a statement list, reporting every identifier occurrence
 */
void walkStmts(const std::vector<std::shared_ptr<Stmt>>& body, const Source& src, const OccurrenceSink& sink)
{
   for (const auto& stmt : body) {
      if (!stmt) {
         continue;
      }

      std::visit(
         [&](const auto& node) {
            using T = std::decay_t<decltype(node)>;

            if constexpr (std::is_same_v<T, AssignStmt>) {
               walkExpr(node.lhs, src, sink);
               walkExpr(node.rhs, src, sink);
               for (const auto& extra : node.additionalTargets) {
                  walkExpr(extra, src, sink);
               }

            } else if constexpr (std::is_same_v<T, ExprStmt>) {
               walkExpr(node.expr, src, sink);

            } else if constexpr (std::is_same_v<T, ReturnStmt>) {
               walkExpr(node.expr, src, sink);

            } else if constexpr (std::is_same_v<T, IfStmt>) {
               for (const auto& branch : node.branches) {
                  walkExpr(branch.condition, src, sink);
                  walkStmts(branch.body, src, sink);
               }

            } else if constexpr (std::is_same_v<T, ForStmt>) {
               walkExpr(node.from, src, sink);
               walkExpr(node.to, src, sink);
               walkExpr(node.by, src, sink);
               walkStmts(node.body, src, sink);

            } else if constexpr (std::is_same_v<T, WhileStmt>) {
               walkExpr(node.condition, src, sink);
               walkStmts(node.body, src, sink);

            } else if constexpr (std::is_same_v<T, RepeatStmt>) {
               walkStmts(node.body, src, sink);
               walkExpr(node.condition, src, sink);

            } else if constexpr (std::is_same_v<T, CaseStmt>) {
               walkExpr(node.selector, src, sink);
               for (const auto& branch : node.branches) {
                  for (const auto& value : branch.values) {
                     walkExpr(value.low, src, sink);
                     walkExpr(value.high, src, sink);
                  }
                  walkStmts(branch.body, src, sink);
               }
            }
         },
         stmt->node);
   }
}

/**
 * @brief Collect a scope and every scope nested inside it
 */
void collectScopeSubtree(const SymbolTable& st, ScopeId root, std::vector<ScopeId>& out)
{
   out.clear();
   if (root == 0) {
      return;
   }
   out.push_back(root);
   // Scopes are few, so a parent scan beats maintaining a child index.
   for (size_t i = 0; i < out.size(); ++i) {
      for (const auto& scope : st.getScopes()) {
         if (scope.parentId == out[i]) {
            out.push_back(scope.id);
         }
      }
   }
}

/**
 * @brief Find the scope holding the members of a POU or METHOD
 *
 * st2cpp names member scopes after their owner ("FB_Motor", "METHOD_Reset").
 * That name is the only handle available: the POU symbol itself is declared in
 * the enclosing scope, so its scopeId does not point at its variables.
 */
ScopeId findMemberScope(const SymbolTable& st, const std::string& name, bool method)
{
   if (name.empty()) {
      return 0;
   }
   const std::string suffix = "_" + name;
   for (const auto& scope : st.getScopes()) {
      if (method) {
         if (scope.name == "METHOD" + suffix) {
            return scope.id;
         }
      } else if (scope.name.size() > suffix.size() && scope.name.compare(0, 7, "METHOD_") != 0 &&
                 scope.name.compare(scope.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
         return scope.id;
      }
   }
   return 0;
}

/**
 * @brief Name -> symbol index restricted to a scope subtree
 */
using SymbolIndex = std::unordered_map<std::string, SymbolId>;

SymbolIndex buildIndex(const SymbolTable& st, ScopeId root)
{
   SymbolIndex index;
   std::vector<ScopeId> scopes;
   collectScopeSubtree(st, root, scopes);
   if (scopes.empty()) {
      return index;
   }
   st.forEachSymbol([&](const Symbol& sym) {
      if (sym.id == 0 || sym.isExternal || sym.name.empty()) {
         return;
      }
      if (std::find(scopes.begin(), scopes.end(), sym.scopeId) != scopes.end()) {
         // Later declarations are the more local ones and win, so a METHOD
         // parameter correctly shadows a POU variable of the same name.
         index[st.normalizeKey(sym.name)] = sym.id;
      }
   });
   return index;
}

} // namespace

SemanticTokenSet collectSemanticTokens(const TranslationUnit& tu,
                                       const SymbolTable& symTab,
                                       const std::vector<std::string>& srcLines)
{
   SemanticTokenSet result;

   Source src;
   src.lines = &srcLines;

   // Global scope holds the built-in types and IEC conversion functions.
   const SymbolIndex globalIndex = buildIndex(symTab, symTab.globalScopeId());

   auto categoryIn = [&](const SymbolIndex& index, const std::string& name) {
      const std::string key = symTab.normalizeKey(name);
      auto it = index.find(key);
      if (it == index.end()) {
         it = globalIndex.find(key);
      }
      if (it == index.end()) {
         return SymCategory::Unresolved;
      }
      const Symbol* sym = symTab.get(it->second);
      return sym ? categoryOf(*sym, symTab) : SymCategory::Unresolved;
   };

   auto emit = [&](int line, int col, const std::string& name, SymCategory cat) {
      if (name.empty() || cat == SymCategory::Unresolved || line < 1 || col < 0) {
         return;
      }
      SemanticToken t;
      t.line = line;
      t.col = col;
      t.length = (int)name.size();
      t.category = cat;
      result.tokens.push_back(t);
   };

   // A POU or METHOD name has no position: st2cpp reports the keyword, so the
   // identifier itself is located in the line.
   auto emitFound = [&](int line, const std::string& name, const SymbolIndex& index) {
      const size_t at = src.lineAt(line).find(name);
      if (at != std::string::npos) {
         emit(line, (int)at, name, categoryIn(index, name));
      }
   };

   for (const auto& pou : tu.pous) {
      const SymbolIndex pouIndex = buildIndex(symTab, findMemberScope(symTab, pou.name, false));

      emitFound((int)pou.line, pou.name, globalIndex);

      for (const auto& sec : pou.varSections) {
         for (const auto& decl : sec.decls) {
            emit((int)decl.line, (int)decl.col - 1, decl.name, categoryIn(pouIndex, decl.name));
         }
      }

      walkStmts(pou.body, src, [&](const Occurrence& occ) {
         emit(occ.line, occ.col, occ.name, categoryIn(pouIndex, occ.name));
      });

      for (const auto& method : pou.methods) {
         const SymbolIndex methodIndex = buildIndex(symTab, findMemberScope(symTab, method.name, true));

         emitFound((int)method.line, method.name, methodIndex);

         for (const auto& decl : method.localVars) {
            emit((int)decl.line, (int)decl.col - 1, decl.name, categoryIn(methodIndex, decl.name));
         }

         // Method parameters carry no position in the AST, so they are matched
         // by name against the declaration lines below the METHOD header.
         for (const auto& param : method.parameters) {
            for (int l = (int)method.line; l < (int)method.line + 40; ++l) {
               const size_t at = src.lineAt(l).find(param.name);
               if (at == std::string::npos) {
                  continue;
               }
               emit(l, (int)at, param.name, categoryIn(methodIndex, param.name));
               break;
            }
         }

         walkStmts(method.body, src, [&](const Occurrence& occ) {
            emit(occ.line, occ.col, occ.name, categoryIn(methodIndex, occ.name));
         });
      }
   }

   for (const auto& st : tu.structs) {
      emit((int)st.line, (int)st.col - 1, st.name, categoryIn(globalIndex, st.name));
      for (const auto& member : st.members) {
         emit((int)member.line, (int)member.col - 1, member.name, SymCategory::Field);
      }
   }

   for (const auto& en : tu.enums) {
      emit((int)en.line, (int)en.col - 1, en.name, categoryIn(globalIndex, en.name));
      for (const auto& value : en.enumerators) {
         emit((int)value.line, (int)value.col - 1, value.name, SymCategory::Enumerator);
      }
   }

   return result;
}

/**
 * @brief Render an array bound when it is a plain literal
 */
static std::string describeBound(const std::shared_ptr<Expr>& bound)
{
   if (!bound) {
      return "?";
   }
   if (const auto* lit = std::get_if<LiteralExpr>(&bound->node)) {
      return lit->value;
   }
   return "?";
}

std::string describeType(const TypeRef& type)
{
   // A named alias keeps its own spelling, so prefer it over the base name.
   std::string name = type.name;

   if (name.empty()) {
      switch (type.base) {
      case BaseType::BOOL: name = "BOOL"; break;
      case BaseType::SINT: name = "SINT"; break;
      case BaseType::INT: name = "INT"; break;
      case BaseType::DINT: name = "DINT"; break;
      case BaseType::LINT: name = "LINT"; break;
      case BaseType::USINT: name = "USINT"; break;
      case BaseType::UINT: name = "UINT"; break;
      case BaseType::UDINT: name = "UDINT"; break;
      case BaseType::ULINT: name = "ULINT"; break;
      case BaseType::REAL: name = "REAL"; break;
      case BaseType::LREAL: name = "LREAL"; break;
      case BaseType::BYTE: name = "BYTE"; break;
      case BaseType::WORD: name = "WORD"; break;
      case BaseType::DWORD: name = "DWORD"; break;
      case BaseType::LWORD: name = "LWORD"; break;
      case BaseType::STRING: name = "STRING"; break;
      case BaseType::WSTRING: name = "WSTRING"; break;
      case BaseType::TIME: name = "TIME"; break;
      case BaseType::DATE: name = "DATE"; break;
      case BaseType::DT: name = "DT"; break;
      case BaseType::TOD: name = "TOD"; break;
      case BaseType::VOID: name = "VOID"; break;
      case BaseType::NAMED: break;
      }
   }

   if (!type.arrayDims.empty()) {
      // The parser collects comma-separated dimensions into a single vector,
      // and overwrites them when nesting, so they render as one N-dimensional
      // array rather than as nested ARRAY keywords.
      std::string dims = "ARRAY[";
      for (size_t i = 0; i < type.arrayDims.size(); ++i) {
         if (i != 0) {
            dims += ", ";
         }
         dims += describeBound(type.arrayDims[i].low) + ".." + describeBound(type.arrayDims[i].high);
      }
      dims += "] OF ";
      name = dims + (name.empty() ? std::string("?") : name);
   } else if (type.stringLen.has_value()) {
      name += "[" + std::to_string(*type.stringLen) + "]";
   }

   if (type.isPointer) {
      name = "POINTER TO " + name;
   } else if (type.isRefTo) {
      name = "REF_TO " + name;
   }
   return name;
}

namespace {

/**
 * @brief Display name of a variable section
 */
const char* varKindText(VarKind kind)
{
   switch (kind) {
   case VarKind::VAR: return "VAR";
   case VarKind::INPUT: return "VAR_INPUT";
   case VarKind::OUTPUT: return "VAR_OUTPUT";
   case VarKind::IN_OUT: return "VAR_IN_OUT";
   case VarKind::EXTERNAL: return "VAR_EXTERNAL";
   case VarKind::GLOBAL: return "VAR_GLOBAL";
   case VarKind::TEMP: return "VAR_TEMP";
   }
   return "VAR";
}

} // namespace

DeclarationIndex collectDeclarations(const TranslationUnit& tu, const SymbolTable& symTab)
{
   DeclarationIndex index;

   const SymbolIndex globalIndex = buildIndex(symTab, symTab.globalScopeId());

   auto categoryFor = [&](const SymbolIndex& scopeIndex, const std::string& name) {
      const std::string key = symTab.normalizeKey(name);
      auto it = scopeIndex.find(key);
      if (it == scopeIndex.end()) {
         it = globalIndex.find(key);
      }
      if (it == scopeIndex.end()) {
         return SymCategory::Unresolved;
      }
      const Symbol* sym = symTab.get(it->second);
      return sym ? categoryOf(*sym, symTab) : SymCategory::Unresolved;
   };

   // col < 0 means "not reported by the parser": the caller resolves the exact
   // column by looking the identifier up in the generated line. line < 0 means
   // the parser gave no line at all, which happens for METHOD parameters; the
   // caller then searches the method body for the declaration.
   auto add = [&](const std::string& name, int line, int col, const std::string& scope,
                  const SymbolIndex& scopeIndex, const std::string& typeText, const std::string& kindText) {
      if (name.empty() || line == 0) {
         return;
      }
      Declaration decl;
      decl.line = line;
      decl.col = col;
      decl.name = name;
      decl.scope = scope;
      decl.category = categoryFor(scopeIndex, name);
      decl.typeText = typeText;
      decl.kindText = kindText;
      index[symTab.normalizeKey(name)].push_back(std::move(decl));
   };

   for (const auto& pou : tu.pous) {
      const char* pouKind = (pou.kind == POUKind::PROGRAM) ? "PROGRAM"
                            : (pou.kind == POUKind::FUNCTION_BLOCK) ? "FUNCTION_BLOCK"
                                                                      : "FUNCTION";

      // The POU name is reported at the keyword, so the column stays unresolved.
      add(pou.name, (int)pou.line, -1, std::string(), globalIndex, describeType(pou.returnType), pouKind);

      const SymbolIndex pouIndex = buildIndex(symTab, findMemberScope(symTab, pou.name, false));

      for (const auto& sec : pou.varSections) {
         for (const auto& d : sec.decls) {
            add(d.name, (int)d.line, (int)d.col - 1, std::string(), pouIndex, describeType(d.type), varKindText(sec.kind));
         }
      }

      for (const auto& method : pou.methods) {
         const SymbolIndex methodIndex = buildIndex(symTab, findMemberScope(symTab, method.name, true));

         add(method.name, (int)method.line, -1, method.name, methodIndex, describeType(method.returnType), "METHOD");

         for (const auto& d : method.localVars) {
            add(d.name, (int)d.line, (int)d.col - 1, method.name, methodIndex, describeType(d.type), "VAR");
         }
         // Method parameters carry no position at all: record the METHOD line so
         // the caller can scan forward for the actual declaration line.
         for (const auto& param : method.parameters) {
            if (param.name.empty()) {
               continue;
            }
            Declaration decl;
            decl.line = -1;
            decl.col = -1;
            decl.ownerLine = (int)method.line;
            decl.name = param.name;
            decl.scope = method.name;
            decl.category = SymCategory::Parameter;
            decl.typeText = describeType(param.type);
            decl.kindText = "PARAMETER";
            index[symTab.normalizeKey(param.name)].push_back(std::move(decl));
         }
      }
   }

   for (const auto& st : tu.structs) {
      add(st.name, (int)st.line, (int)st.col - 1, std::string(), globalIndex, st.name, "STRUCT");
      for (const auto& m : st.members) {
         add(m.name, (int)m.line, (int)m.col - 1, std::string(), globalIndex, describeType(m.type), "STRUCT MEMBER");
      }
   }

   for (const auto& en : tu.enums) {
      add(en.name, (int)en.line, (int)en.col - 1, std::string(), globalIndex, en.name, "ENUM");
      for (const auto& value : en.enumerators) {
         add(value.name, (int)value.line, (int)value.col - 1, std::string(), globalIndex, en.name, "ENUMERATOR");
      }
   }

   return index;
}

// ============================================================================
//  Member access
// ============================================================================

namespace {

/**
 * @brief Render a resolved type the way it is written in source
 *
 * Symbols carry a TypeId rather than a syntactic TypeRef, so the name comes
 * from the type table. An array reports its element type, which is what the
 * completion list can usefully show.
 */
std::string describeResolvedType(const SymbolTable& st, TypeId typeId)
{
   const TypeInfo* info = st.getType(typeId);
   if (info == nullptr) {
      return {};
   }
   switch (info->kind) {
   case TypeKind::Array: {
      std::string elem = describeResolvedType(st, info->pointedTypeId);
      return elem.empty() ? "ARRAY" : ("ARRAY OF " + elem);
   }
   case TypeKind::Struct:
   case TypeKind::FunctionBlock:
      return info->name;
   case TypeKind::Elementary:
   case TypeKind::Enum:
   case TypeKind::Pointer:
   case TypeKind::Reference:
   case TypeKind::Interface:
   case TypeKind::Void:
   case TypeKind::Unknown:
   default:
      return info->name;
   }
}

} // namespace

// ============================================================================
//  Fuzzy name matching
// ============================================================================

namespace {

/// Score for lining a pattern character up with a name character.
constexpr int kCharScore = 16;
/// Extra for taking the very first character of the name.
constexpr int kFirstCharBonus = 8;
/// Extra for landing immediately after the previous match.
constexpr int kConsecutiveBonus = 12;
/// Extra for landing where a word starts, i.e. after '_' or on a camel hump.
constexpr int kBoundaryBonus = 10;
/// Cost of stepping over one character of the name without matching it.
constexpr int kGapPenalty = 3;
/// The whole name, case-insensitively: nothing can beat it.
constexpr int kExactBonus = 400;
/// A prefix, case-insensitively.
constexpr int kPrefixBonus = 300;
/// A prefix, spelled exactly as declared. IEC identifiers are case-insensitive,
/// so this is the one that says "you meant this one".
constexpr int kCasePrefixBonus = 360;
/// A shorter name is the tighter match when the scores are otherwise level.
constexpr int kTightnessPerChar = 1;

constexpr int kUnreachable = -1000000;

/// Fold to lower case, for the case-insensitive comparison IEC identifiers need.
char fold(char c)
{
   return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

/// Start of a word inside a name: the first character, anything after '_', or
/// the hump of a camelCase name.
bool isWordStart(const std::string& name, size_t index)
{
   if (index == 0) {
      return true;
   }
   const unsigned char here = static_cast<unsigned char>(name[index]);
   const unsigned char before = static_cast<unsigned char>(name[index - 1]);
   if (here == '_' || before == '_') {
      return true;
   }
   return std::islower(before) != 0 && std::isupper(here) != 0;
}

} // namespace

NameMatch matchName(const std::string& name, const std::string& pattern)
{
   NameMatch result;
   if (pattern.empty()) {
      result.matched = true;
      return result;
   }
   // A pattern longer than the name cannot be a subsequence of it.
   if (name.empty() || pattern.size() > name.size()) {
      return result;
   }

   const int m = static_cast<int>(pattern.size());
   const int n = static_cast<int>(name.size());

   // Greedy would be enough to decide *whether* a name matches, but not to rank
   // it well: "mrst" should land on MotorResetState in preference to anything
   // merely containing those letters. A small dynamic program keeps the best
   // alignment, and its table doubles as the record used to walk the match back.
   struct Cell
   {
      int score = kUnreachable;
      bool matched = false; ///< Whether the character at this index was taken
   };
   std::vector<Cell> grid(static_cast<size_t>(m + 1) * static_cast<size_t>(n + 1));
   auto cell = [&](int j, int i) -> Cell& {
      return grid[static_cast<size_t>(j) * static_cast<size_t>(n + 1) + static_cast<size_t>(i)];
   };

   cell(0, 0).score = 0;
   for (int j = 1; j <= m; ++j) {
      for (int i = 1; i <= n; ++i) {
         Cell best;
         // Step over a character of the name without taking it.
         if (cell(j, i - 1).score > kUnreachable) {
            best.score = cell(j, i - 1).score - kGapPenalty;
         }
         if (fold(name[static_cast<size_t>(i - 1)]) == fold(pattern[static_cast<size_t>(j - 1)])) {
            const Cell& diagonal = cell(j - 1, i - 1);
            if (diagonal.score > kUnreachable) {
               int gain = kCharScore;
               if (i == 1) {
                  gain += kFirstCharBonus;
               }
               if (isWordStart(name, static_cast<size_t>(i - 1))) {
                  gain += kBoundaryBonus;
               }
               if (diagonal.matched) {
                  gain += kConsecutiveBonus;
               }
               const int taken = diagonal.score + gain;
               if (taken > best.score) {
                  best.score = taken;
                  best.matched = true;
               }
            }
         }
         cell(j, i) = best;
      }
   }

   if (cell(m, n).score <= kUnreachable) {
      return result;
   }

   // Walk the table back to recover which characters were taken.
   std::vector<int> positions;
   positions.reserve(static_cast<size_t>(m));
   int i = n;
   for (int j = m; j >= 1; --j) {
      while (i >= 1) {
         const Cell& here = cell(j, i);
         const Cell& previous = cell(j, i - 1);
         const bool tookIt =
             here.matched && here.score > previous.score && (i == 1 || cell(j - 1, i - 1).score > kUnreachable);
         if (tookIt) {
            positions.push_back(i - 1);
            --i;
            break;
         }
         --i;
      }
   }
   std::reverse(positions.begin(), positions.end());

   int score = cell(m, n).score;
   if (positions.size() == name.size()) {
      score += kExactBonus;
      if (name == pattern) {
         score += kCasePrefixBonus;
      }
   }
   if (positions.size() == pattern.size()) {
      const bool sameCase = name.compare(0, pattern.size(), pattern) == 0;
      score += sameCase ? kCasePrefixBonus : kPrefixBonus;
   }
   score -= static_cast<int>(name.size()) * kTightnessPerChar;

   result.matched = true;
   result.score = score;
   result.positions = std::move(positions);
   return result;
}

bool MemberAccess::matches(const std::string& prefix) const
{
   return matchName(name, prefix).matched;
}

void rankMembers(MemberList& members, const std::string& pattern)
{
   std::vector<MemberAccess> kept;
   std::vector<int> scores;
   kept.reserve(members.size());
   scores.reserve(members.size());
   for (MemberAccess& member : members) {
      const NameMatch match = matchName(member.name, pattern);
      if (!match.matched) {
         continue;
      }
      member.matchPositions = match.positions;
      scores.push_back(match.score);
      kept.push_back(std::move(member));
   }

   // Stable, so members that score the same keep the block's declaration order.
   std::vector<size_t> order(kept.size());
   for (size_t i = 0; i < order.size(); ++i) {
      order[i] = i;
   }
   std::stable_sort(order.begin(), order.end(),
                    [&](size_t a, size_t b) { return scores[a] > scores[b]; });

   MemberList ranked;
   ranked.reserve(kept.size());
   for (size_t index : order) {
      ranked.push_back(std::move(kept[index]));
   }
   members = std::move(ranked);
}

ScopeId findMemberScopeFor(const SymbolTable& symTab, const std::string& ownerName, bool method)
{
   return findMemberScope(symTab, ownerName, method);
}

MemberList collectBlockMembers(const SymbolTable& symTab, SymbolId fbId)
{
   MemberList out;
   const Symbol* fb = symTab.get(fbId);
   if (fb == nullptr || fb->kind != SymbolKind::FunctionBlock) {
      return out;
   }

   // Base chain, root ancestor first, so the listing follows declaration order.
   std::vector<const Symbol*> chain;
   std::unordered_set<SymbolId> seen;
   for (const Symbol* cursor = fb; cursor != nullptr && seen.insert(cursor->id).second;
        cursor = (cursor->baseClassId != 0) ? symTab.get(cursor->baseClassId) : nullptr) {
      chain.push_back(cursor);
   }
   std::reverse(chain.begin(), chain.end());

   // The block that declares each name wins, so a redeclaration is listed once
   // and an override does not show up next to the method it replaces.
   std::unordered_map<std::string, const Symbol*> ownerByName;
   for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
      for (SymbolId paramId : (*it)->params) {
         if (const Symbol* p = symTab.get(paramId)) {
            ownerByName.emplace(symTab.normalizeKey(p->name), *it);
         }
      }
      for (SymbolId methodId : (*it)->members) {
         const Symbol* m = symTab.get(methodId);
         if (m != nullptr && m->kind == SymbolKind::Method) {
            ownerByName.emplace(symTab.normalizeKey(m->name), *it);
         }
      }
   }

   // The call interface, inheritance included: effectiveParams already flattens
   // the base chain in the right order and drops redeclared names.
   std::unordered_set<std::string> emitted;
   for (SymbolId paramId : symTab.effectiveParams(fbId)) {
      const Symbol* param = symTab.get(paramId);
      if (param == nullptr) {
         continue;
      }
      const std::string key = symTab.normalizeKey(param->name);
      if (!emitted.insert(key).second) {
         continue;
      }
      MemberAccess member;
      member.name = param->name;
      member.typeText = describeResolvedType(symTab, param->typeId);
      member.kind = MemberAccess::Kind::Parameter;
      member.isConstant = param->isConstant;
      member.isRetain = param->isRetain;
      auto owner = ownerByName.find(key);
      if (owner != ownerByName.end() && owner->second != fb) {
         member.inherited = true;
         member.declaredIn = owner->second->name;
      }
      out.push_back(std::move(member));
   }

   // Internal state. The symbol table does not record which variable section a
   // member came from, so a plain VAR and a VAR_TEMP are both reported as state;
   // only the direction-bearing sections were classified as parameters above.
   for (const Symbol* owner : chain) {
      const Scope* scope = symTab.getScope(owner->scopeId);
      if (scope == nullptr) {
         continue;
      }
      for (const auto& [name, symId] : scope->symbols) {
         const Symbol* sym = symTab.get(symId);
         if (sym == nullptr || sym->kind != SymbolKind::Variable) {
            continue;
         }
         const std::string key = symTab.normalizeKey(sym->name);
         if (!emitted.insert(key).second) {
            continue;
         }
         auto ownerIt = ownerByName.find(key);
         if (ownerIt != ownerByName.end() && ownerIt->second != owner) {
            continue; // shadowed by a more derived block
         }
         MemberAccess member;
         member.name = sym->name;
         member.typeText = describeResolvedType(symTab, sym->typeId);
         member.kind = MemberAccess::Kind::State;
         member.isConstant = sym->isConstant;
         member.isRetain = sym->isRetain;
         if (owner != fb) {
            member.inherited = true;
            member.declaredIn = owner->name;
         }
         out.push_back(std::move(member));
      }
   }

   // Methods, base first, an override replacing what it overrides.
   for (const Symbol* owner : chain) {
      for (SymbolId methodId : owner->members) {
         const Symbol* method = symTab.get(methodId);
         if (method == nullptr || method->kind != SymbolKind::Method) {
            continue;
         }
         const std::string key = symTab.normalizeKey(method->name);
         if (!emitted.insert(key).second) {
            continue;
         }
         MemberAccess member;
         member.name = method->name;
         member.typeText = describeResolvedType(symTab, method->returnTypeId);
         member.kind = MemberAccess::Kind::Method;
         member.isAbstract = method->isAbstract;
         member.isOverride = method->isOverride;
         if (owner != fb) {
            member.inherited = true;
            member.declaredIn = owner->name;
         }
         for (SymbolId paramId : method->params) {
            const Symbol* p = symTab.get(paramId);
            if (p == nullptr) {
               continue;
            }
            member.parameterTypes.push_back(describeResolvedType(symTab, p->typeId));
            member.parameterNames.push_back(p->name);
            switch (p->paramDir) {
            case st2cpp::semantic::ParamDir::Input:
               member.parameterDirs.push_back("IN");
               break;
            case st2cpp::semantic::ParamDir::Output:
               member.parameterDirs.push_back("OUT");
               break;
            case st2cpp::semantic::ParamDir::InOut:
               member.parameterDirs.push_back("IN_OUT");
               break;
            case st2cpp::semantic::ParamDir::None:
            default:
               member.parameterDirs.push_back(std::string());
               break;
            }
         }
         out.push_back(std::move(member));
      }
   }

   return out;
}

SymbolId findInstanceBlock(const SymbolTable& symTab, ScopeId scopeId, const std::string& name)
{
   if (name.empty()) {
      return 0;
   }
   // Walk up from the innermost scope so a METHOD local wins over the POU
   // variable of the same name.
   const Scope* scope = symTab.getScope(scopeId);
   while (scope != nullptr) {
      auto it = scope->symbols.find(symTab.normalizeKey(name));
      if (it != scope->symbols.end()) {
         const Symbol* sym = symTab.get(it->second);
         if (sym != nullptr) {
            const TypeInfo* type = symTab.getType(sym->typeId);
            if (type != nullptr && type->kind == TypeKind::FunctionBlock && type->symbolId != 0) {
               return type->symbolId;
            }
         }
         return 0; // the name exists but is not an instance of a block
      }
      if (scope->parentId == 0) {
         break;
      }
      scope = symTab.getScope(scope->parentId);
   }

   // Not found by scope walk: it may be a sibling file's block, imported from a
   // library descriptor. The external scope holds those.
   if (const Symbol* ext = symTab.get(symTab.lookupExternal(name))) {
      const TypeInfo* type = symTab.getType(ext->typeId);
      if (type != nullptr && type->kind == TypeKind::FunctionBlock && type->symbolId != 0) {
         return type->symbolId;
      }
   }
   return 0;
}

MemberAccessPoint memberAccessPointAt(const std::string& line, int cursorCol)
{
   MemberAccessPoint point;
   if (cursorCol < 0 || static_cast<size_t>(cursorCol) > line.size()) {
      return point;
   }

   auto isIdentChar = [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
   };

   // The fragment after the '.' is the prefix already typed.
   size_t end = static_cast<size_t>(cursorCol);
   size_t start = end;
   while (start > 0 && isIdentChar(line[start - 1])) {
      --start;
   }
   point.prefix = line.substr(start, end - start);

   if (start == 0 || line[start - 1] != '.') {
      return point;
   }
   const size_t dot = start - 1;

   // Only an identifier, or nothing, may precede the '.'; a literal, a closing
   // parenthesis or a second dot does not open member completion.
   size_t objEnd = dot;
   while (objEnd > 0 && isIdentChar(line[objEnd - 1])) {
      --objEnd;
   }
   if (objEnd == dot) {
      return point; // nothing before the '.'
   }
   // In a chained access "a.b." the object to complete is b, so a '.' before it
   // is fine: whether b is itself an instance is decided by findInstanceBlock.
   // A literal cannot be the object: "42." is a syntax error, not a completion.
   if (std::isdigit(static_cast<unsigned char>(line[objEnd])) != 0) {
      return point;
   }

   point.active = true;
   point.dotColumn = static_cast<int>(dot);
   point.objectName = line.substr(objEnd, dot - objEnd);
   return point;
}

// ============================================================================
//  Plain identifier completion
// ============================================================================

MemberList collectScopeNames(const SymbolTable& symTab, ScopeId scopeId, bool includeOwnMethods)
{
   MemberList out;
   // IEC 61131-3 identifiers are case-insensitive, so a name is the same name
   // whichever case it was declared in. Normalising the key is what makes the
   // shadowing rule work: a local 'count' and a POU 'Count' are one name, and the
   // innermost declaration is the one the source resolves to.
   std::unordered_set<std::string> seen;

   auto add = [&](const Symbol& sym) {
      if (sym.name.empty() || sym.kind == SymbolKind::Program) {
         return; // the POU being edited is not a name to complete inside itself
      }
      if (!seen.insert(symTab.normalizeKey(sym.name)).second) {
         return;
      }
      Suggestion entry;
      entry.name = sym.name;
      switch (sym.kind) {
      case SymbolKind::Parameter:
         entry.kind = Suggestion::Kind::Parameter;
         break;
      case SymbolKind::Method:
         entry.kind = Suggestion::Kind::Method;
         break;
      case SymbolKind::Function:
         entry.kind = Suggestion::Kind::Function;
         break;
      case SymbolKind::FunctionBlock:
         entry.kind = Suggestion::Kind::Block;
         break;
      case SymbolKind::Type:
         entry.kind = Suggestion::Kind::Type;
         break;
      case SymbolKind::Enumerator:
         entry.kind = Suggestion::Kind::Enumerator;
         break;
      default:
         // A plain VAR is a state variable of the enclosing block, and in a method
         // it is that method's own. It reads the same either way, so it does not
         // matter which the parser filed it under; the constant flag does.
         entry.kind = sym.isConstant ? Suggestion::Kind::Constant : Suggestion::Kind::State;
         break;
      }
      entry.isConstant = sym.isConstant;
      entry.isRetain = sym.isRetain;
      entry.isAbstract = sym.isAbstract;
      entry.isOverride = sym.isOverride;
      // For a callable the type is what it returns, which is the part that tells
      // two overloads apart; for a variable it is what it holds.
      entry.typeText = describeResolvedType(symTab, sym.typeId);
      entry.parameterTypes.reserve(sym.params.size());
      for (SymbolId paramId : sym.params) {
         const Symbol* param = symTab.get(paramId);
         if (param != nullptr) {
            entry.parameterTypes.push_back(describeResolvedType(symTab, param->typeId));
         }
      }
      out.push_back(std::move(entry));
   };

   // Innermost scope outwards, so the declaration that shadows the others is the
   // one kept and the one listed first. The walk goes all the way to the root,
   // because the global scope is where the workspace declares its own function
   // block types, and a name declared there is one a POU can name: declaring
   // 'motore : navFB' means 'navFB' has to be completable like any other type.
   for (const Scope* scope = symTab.getScope(scopeId); scope != nullptr;
        scope = (scope->id == 0 ? nullptr : symTab.getScope(scope->parentId))) {
      for (const Symbol& sym : symTab.getSymbols()) {
         if (sym.scopeId == scope->id) {
            add(sym);
         }
      }
   }

   // The methods of the block being edited, which its own body can name. A method
   // body does not get them: there a sibling method needs an instance, so it
   // belongs behind a dot rather than in the plain-name list.
   if (includeOwnMethods) {
      // Outermost scope above the global one, which is the POU the caret is in.
      ScopeId pouScope = scopeId;
      for (const Scope* scope = symTab.getScope(scopeId); scope != nullptr;
           scope = (scope->parentId == 0 || scope->parentId == symTab.globalScopeId())
                      ? nullptr
                      : symTab.getScope(scope->parentId)) {
         pouScope = scope->id;
      }
      auto underPou = [&](ScopeId candidate) {
         for (const Scope* scope = symTab.getScope(candidate); scope != nullptr;
              scope = (scope->parentId == 0) ? nullptr : symTab.getScope(scope->parentId)) {
            if (scope->id == pouScope) {
               return true;
            }
         }
         return false;
      };
      for (const Symbol& sym : symTab.getSymbols()) {
         if (sym.kind == SymbolKind::Method && underPou(sym.scopeId)) {
            add(sym);
         }
      }
   }

   // The library side: FUNCTION_BLOCKs, FUNCTIONs and types the workspace does
   // not declare itself, which is where a name like MAX or a standard block type
   // comes from. The POU level of the current file has already been walked.
   for (const Symbol& sym : symTab.getSymbols()) {
      if (sym.scopeId == symTab.externalScope()) {
         add(sym);
      }
   }
   return out;
}

std::string describeMember(const Suggestion& member)
{
   std::string text = member.name;
   // A function is called, so it gets parentheses like a method; a type name and a
   // constant are not, and spelling them with brackets would read as a call.
   if (member.kind == Suggestion::Kind::Method || member.kind == Suggestion::Kind::Function) {
      text += "(";
      for (size_t i = 0; i < member.parameterTypes.size(); ++i) {
         if (i != 0) {
            text += ", ";
         }
         text += member.parameterTypes[i];
      }
      text += ")";
   }
   if (!member.typeText.empty()) {
      text += " : " + member.typeText;
   }
   return text;
}

// ============================================================================
//  Call sites and signature help
// ============================================================================

namespace {

bool isIdentChar(char c)
{
   return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool isSpace(char c)
{
   return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string directionText(st2cpp::semantic::ParamDir dir)
{
   switch (dir) {
   case st2cpp::semantic::ParamDir::Input:
      return "IN";
   case st2cpp::semantic::ParamDir::Output:
      return "OUT";
   case st2cpp::semantic::ParamDir::InOut:
      return "IN_OUT";
   case st2cpp::semantic::ParamDir::None:
   default:
      return std::string();
   }
}

SignatureParam paramFromSymbol(const SymbolTable& symTab, const Symbol* param)
{
   SignatureParam out;
   if (param == nullptr) {
      return out;
   }
   out.name = param->name;
   out.type = describeResolvedType(symTab, param->typeId);
   out.direction = directionText(param->paramDir);
   out.hasDefault = param->hasDefaultValue;
   return out;
}

/**
 * @brief Look a name up in the scope chain, then in the external scope
 *
 * Mirrors what findInstanceBlock does for instances: walk up from the innermost
 * scope so a METHOD local wins over a POU variable of the same name.
 */
const Symbol* lookupInScopeChain(const SymbolTable& symTab, ScopeId scopeId, const std::string& name)
{
   const Scope* scope = symTab.getScope(scopeId);
   while (scope != nullptr) {
      auto it = scope->symbols.find(symTab.normalizeKey(name));
      if (it != scope->symbols.end()) {
         const Symbol* found = symTab.get(it->second);
         if (found != nullptr) {
            return found;
         }
      }
      if (scope->parentId == 0) {
         break;
      }
      scope = symTab.getScope(scope->parentId);
   }
   return symTab.get(symTab.lookupExternal(name));
}

/// A METHOD of `fb` or of one of its bases, base first.
const Symbol* findMethodInChain(const SymbolTable& symTab, SymbolId fbId, const std::string& name,
                                const Symbol** ownerOut)
{
   std::vector<const Symbol*> chain;
   std::unordered_set<SymbolId> seen;
   for (const Symbol* cursor = symTab.get(fbId); cursor != nullptr && seen.insert(cursor->id).second;
        cursor = (cursor->baseClassId != 0) ? symTab.get(cursor->baseClassId) : nullptr) {
      chain.push_back(cursor);
   }
   for (const Symbol* block : chain) {
      for (SymbolId memberId : block->members) {
         const Symbol* method = symTab.get(memberId);
         if (method != nullptr && method->kind == SymbolKind::Method &&
             symTab.normalizeKey(method->name) == symTab.normalizeKey(name)) {
            if (ownerOut != nullptr) {
               *ownerOut = block;
            }
            return method;
         }
      }
   }
   return nullptr;
}

} // namespace

CallSite callSiteAt(const std::vector<std::string>& lines, int line, int col)
{
   CallSite call;
   if (line < 0 || static_cast<size_t>(line) >= lines.size() || col < 0) {
      return call;
   }

   // Walk the buffer backwards as one flat character stream, remembering where
   // each line starts, so a call spread over several lines still resolves and a
   // ';' is recognised as the end of a statement.
   std::string flat;
   std::vector<size_t> lineStarts;
   flat.reserve(1024);
   for (size_t i = 0; i < lines.size(); ++i) {
      lineStarts.push_back(flat.size());
      flat += lines[i];
      flat += '\n';
   }

   auto positionOf = [&](int lineIndex, int column) -> size_t {
      if (lineIndex < 0 || static_cast<size_t>(lineIndex) >= lineStarts.size()) {
         return flat.size();
      }
      const std::string& text = lines[static_cast<size_t>(lineIndex)];
      const size_t safeColumn = std::min<size_t>(static_cast<size_t>(std::max(0, column)), text.size());
      return lineStarts[static_cast<size_t>(lineIndex)] + safeColumn;
   };

   const size_t start = positionOf(line, col);
   if (start > flat.size()) {
      return call;
   }

   int depth = 0;
   int commas = 0;
   size_t open = std::string::npos;
   // The comma before the argument being written, kept because that is where the
   // current argument's own text begins, and the hint after it has to describe
   // this argument rather than the ones already filled in.
   size_t lastComma = std::string::npos;
   bool inString = false;
   bool inLineComment = false;
   bool inBlockComment = false;
   // A guard against a buffer whose parentheses never balance: without it a
   // stray '(' could walk the whole file on every frame.
   const size_t floor = start > 4096 ? start - 4096 : 0;

   for (size_t i = start; i-- > floor;) {
      const char c = flat[i];
      const char next = (i + 1 < flat.size()) ? flat[i + 1] : '\0';

      if (inLineComment) {
         if (c == '\n') {
            inLineComment = false;
         }
         continue;
      }
      if (inBlockComment) {
         if (c == '*' && next == '/') {
            inBlockComment = false;
            --i;
         }
         continue;
      }
      if (inString) {
         // A quote only closes the literal when a second one follows, which is
         // how ST spells an embedded quote ('it''s').
         if (c == '\'') {
            if (next == '\'') {
               --i;
            } else {
               inString = false;
            }
         } else if (c == '\n') {
            inString = false; // an unterminated literal does not run past the line
         }
         continue;
      }

      if (c == '\'') {
         inString = true;
         continue;
      }
      if (c == '/' && next == '/') {
         inLineComment = true;
         --i;
         continue;
      }
      if (c == '/' && next == '*') {
         inBlockComment = true;
         --i;
         continue;
      }
      if (c == ')') {
         ++depth;
         continue;
      }
      if (c == '(') {
         if (depth == 0) {
            open = i;
            break;
         }
         --depth;
         continue;
      }
      if (c == ',') {
         if (depth == 0) {
            ++commas;
            lastComma = i;
         }
         continue;
      }
      if (c == ';') {
         // The cursor is not inside any call: a ';' at this level ends the
         // statement, so a '(' further back belongs to a previous one.
         if (depth == 0) {
            return call;
         }
         continue;
      }
   }

   if (open == std::string::npos) {
      return call;
   }

   // The callee sits before the '(': an identifier, optionally behind a '.'.
   size_t nameEnd = open;
   while (nameEnd > floor && isSpace(flat[nameEnd - 1])) {
      --nameEnd;
   }
   size_t nameStart = nameEnd;
   while (nameStart > floor && isIdentChar(flat[nameStart - 1])) {
      --nameStart;
   }
   if (nameStart == nameEnd) {
      return call; // nothing named before the '('
   }
   call.calleeName = flat.substr(nameStart, nameEnd - nameStart);

   size_t objectEnd = nameStart;
   while (objectEnd > floor && isSpace(flat[objectEnd - 1])) {
      --objectEnd;
   }
   if (objectEnd > floor && flat[objectEnd - 1] == '.') {
      size_t objectStart = objectEnd - 1;
      while (objectStart > floor && isIdentChar(flat[objectStart - 1])) {
         --objectStart;
      }
      if (objectStart != objectEnd - 1) {
         call.objectName = flat.substr(objectStart, objectEnd - 1 - objectStart);
      }
   }
   // A bare name preceded by an operator is a grouped expression, not a call:
   // naming it would offer a signature the user did not ask for.
   if (call.objectName.empty() && nameStart > floor) {
      const char before = flat[nameStart - 1];
      if (before == '=' || before == '<' || before == '>' || before == ':' || before == ',') {
         return call;
      }
   }

   call.active = true;
   call.argumentIndex = commas;
   call.label = call.objectName.empty() ? call.calleeName : call.objectName + "." + call.calleeName;

   // Report the '(' and the start of the current argument in editor coordinates.
   // They can be on different lines from each other and from the cursor, which is
   // why each carries its own line.
   auto inEditorCoordinates = [&](size_t flatIndex, int& outLine, int& outColumn) {
      for (size_t i = 0; i < lineStarts.size(); ++i) {
         if (lineStarts[i] <= flatIndex && (i + 1 >= lineStarts.size() || lineStarts[i + 1] > flatIndex)) {
            outLine = static_cast<int>(i);
            outColumn = static_cast<int>(flatIndex - lineStarts[i]);
            return;
         }
      }
   };
   inEditorCoordinates(open, call.parenLine, call.parenColumn);
   if (lastComma == std::string::npos) {
      call.argumentLine = call.parenLine;
      call.argumentColumn = call.parenColumn + 1;
   } else {
      inEditorCoordinates(lastComma, call.argumentLine, call.argumentColumn);
      call.argumentColumn += 1; // the character after the comma
   }
   return call;
}

bool resolveCallSignature(const SymbolTable& symTab, ScopeId scopeId, const CallSite& call, CallSignature& out)
{
   if (!call.active || call.calleeName.empty()) {
      return false;
   }

   CallSignature signature;
   signature.name = call.calleeName;

   if (!call.objectName.empty()) {
      // inst.Method( : the method of whatever inst is an instance of, base
      // chain included, since the block may not declare it itself.
      const SymbolId fbId = findInstanceBlock(symTab, scopeId, call.objectName);
      SymbolId typeId = fbId;
      if (typeId == 0) {
         // Type.Method( : a POU or a block reached through its type name, which
         // is how a METHOD is called without an instance.
         const SymbolId global = symTab.lookupGlobal(call.objectName);
         const Symbol* type = symTab.get(global);
         if (type != nullptr && (type->kind == SymbolKind::FunctionBlock ||
                                 type->kind == SymbolKind::Function || type->kind == SymbolKind::Program)) {
            typeId = type->id;
            signature.owner = type->name;
         } else {
            return false;
         }
      }

      const Symbol* owner = nullptr;
      const Symbol* callee = findMethodInChain(symTab, typeId, call.calleeName, &owner);
      if (callee == nullptr) {
         return false;
      }
      if (signature.owner.empty()) {
         signature.owner = call.objectName;
      }
      // The method came from a base when the declaring block is not the block
      // the instance was declared as.
      if (owner != nullptr && owner->id != typeId) {
         signature.inherited = true;
         signature.declaredIn = owner->name;
      }
      signature.returnType = describeResolvedType(symTab, callee->returnTypeId);
      for (SymbolId paramId : callee->params) {
         signature.params.push_back(paramFromSymbol(symTab, symTab.get(paramId)));
      }
   } else {
      // MyFunc( or MyBlock( : a plain name, resolved through the scope
      // chain. A block declared in the scope is callable through its
      // interface. A local variable that happens to be an instance of a
      // block is callable through that interface too; it is not looked up
      // as a callable itself, so the plain lookup has to fall back to it.
      const Symbol* callee = lookupInScopeChain(symTab, scopeId, call.calleeName);
      if (callee == nullptr) {
         return false;
      }
      if (callee->kind == SymbolKind::FunctionBlock || callee->kind == SymbolKind::Program) {
         // A derived block is constructed by calling it, so the base
         // interface is what the positional arguments bind against.
         for (SymbolId paramId : symTab.effectiveParams(callee->id)) {
            signature.params.push_back(paramFromSymbol(symTab, symTab.get(paramId)));
         }
      } else {
         const SymbolId fbId = findInstanceBlock(symTab, scopeId, call.calleeName);
         if (fbId != 0) {
            // An instance of a block: positional arguments bind against the
            // block's interface, not against the instance variable.
            for (SymbolId paramId : symTab.effectiveParams(fbId)) {
               signature.params.push_back(paramFromSymbol(symTab, symTab.get(paramId)));
            }
         } else if (callee->kind == SymbolKind::Function) {
            signature.returnType = describeResolvedType(symTab, callee->returnTypeId);
            for (SymbolId paramId : callee->params) {
               signature.params.push_back(paramFromSymbol(symTab, symTab.get(paramId)));
            }
         } else {
            return false;
         }
      }
   }

   out = std::move(signature);
   return true;
}

std::vector<SignatureSegment> signatureSegments(const CallSignature& signature, int activeArgument)
{
   std::vector<SignatureSegment> segments;
   segments.push_back({signature.name, false});
   segments.push_back({"(", false});

   for (size_t i = 0; i < signature.params.size(); ++i) {
      if (i != 0) {
         segments.push_back({", ", false});
      }
      const SignatureParam& param = signature.params[i];
      std::string text;
      if (!param.name.empty()) {
         text += param.name;
         if (!param.type.empty()) {
            text += " : ";
         }
      }
      text += param.type;
      if (!param.direction.empty()) {
         text += " " + param.direction;
      }
      if (param.hasDefault) {
         text += " := ...";
      }
      segments.push_back({std::move(text), static_cast<int>(i) == activeArgument});
   }

   // A cursor past the last parameter still gets a run, so an extra argument can
   // be pointed at instead of silently ignored.
   if (activeArgument >= static_cast<int>(signature.params.size()) && !signature.params.empty()) {
      segments.push_back({", ", false});
      segments.push_back({"too many arguments", true});
   }

   segments.push_back({")", false});
   if (!signature.returnType.empty()) {
      segments.push_back({" : " + signature.returnType, false});
   }
   if (signature.inherited) {
      segments.push_back({"   (from " + signature.declaredIn + ")", false});
   }
   return segments;
}

std::string formatSignature(const CallSignature& signature, int activeArgument)
{
   std::string text;
   for (const SignatureSegment& segment : signatureSegments(signature, activeArgument)) {
      text += segment.text;
   }
   return text;
}

} // namespace ST
} // namespace undoApp
