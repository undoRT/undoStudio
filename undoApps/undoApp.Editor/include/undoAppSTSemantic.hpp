/**
 * @file undoAppSTSemantic.hpp
 * @brief Resolved-symbol extraction for ST syntax highlighting
 * @ingroup undoapps
 *
 * Turns a parsed TranslationUnit plus the st2cpp symbol table into a flat list
 * of classified identifier tokens. Deliberately free of any UI dependency so
 * the mapping (which identifier belongs to which category, at which position)
 * can be exercised without a graphics context.
 *
 * Coordinates are 1-based lines (as st2cpp reports them) and 0-based columns
 * into the line, matching the editor's glyph indices.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include "ast/AST.h"
#include "semantic/SymbolTable.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace undoApp {
namespace ST {

/**
 * @brief Category of a resolved symbol, as far as highlighting cares
 */
enum class SymCategory
{
   Unresolved, ///< Not in the symbol table: leave the text as the lexer saw it
   Variable,
   Constant,   ///< VAR CONSTANT, or any symbol flagged constant
   Parameter,
   Function,   ///< FUNCTION / METHOD / IEC conversion
   Type,       ///< Built-in or user-defined type, INTERFACE
   Field,      ///< PROGRAM / FUNCTION_BLOCK instance, STRUCT member
   Enumerator
};

/**
 * @brief One classified identifier occurrence
 */
struct SemanticToken
{
   int line = 0;              ///< 1-based line in the generated file
   int col = 0;               ///< 0-based column of the first character
   int length = 0;            ///< Number of characters
   SymCategory category = SymCategory::Unresolved;
};

/**
 * @brief Result of a classification pass
 */
struct SemanticTokenSet
{
   std::vector<SemanticToken> tokens;
};

/**
 * @brief Classify every identifier the analyzer could resolve
 * @param tu        Parsed translation unit
 * @param symTab    Symbol table produced by SemanticAnalyzer for that same unit
 * @param srcLines  Generated source, 1-based (index 0 holds line 1)
 * @return Classified tokens, in AST traversal order
 *
 * The analyzer does not write symbol ids back into the AST, and POU/METHOD
 * members live in scopes unreachable from the global one, so visibility is
 * resolved by name against a per-scope index built here. A few nodes (POU and
 * METHOD names, STRUCT members) carry no position at all and are located in
 * the source text.
 */
SemanticTokenSet collectSemanticTokens(const TranslationUnit& tu,
                                       const st2cpp::semantic::SymbolTable& symTab,
                                       const std::vector<std::string>& srcLines);

/**
 * @brief The scope holding the variables of a POU or the locals of a METHOD
 * @param symTab    Symbol table of the current analysis
 * @param ownerName POU or METHOD name, as spelled in the source
 * @param method    True for a METHOD, false for a POU
 * @return Scope id, or 0 when there is no such scope
 *
 * st2cpp names these scopes after their owner ("FB_Motor", "METHOD_Reset"). A
 * METHOD scope is nested inside its POU's, so a name visible in the METHOD is
 * also found by walking up from here.
 */
st2cpp::semantic::ScopeId findMemberScopeFor(const st2cpp::semantic::SymbolTable& symTab,
                                             const std::string& ownerName,
                                             bool method);

/**
 * @brief Render a type reference the way it is written in source
 * @param type Type reference from the AST
 * @return Human-readable type, e.g. "INT", "STRING[80]", "POINTER TO Motor"
 */
std::string describeType(const TypeRef& type);

/**
 * @brief One declaration that a usage can be navigated back to
 *
 * Positions use the same coordinates as SemanticToken: 1-based lines in the
 * generated file, 0-based columns.
 */
struct Declaration
{
   int line = 0;         ///< Line of the declared name
   int col = 0;          ///< Column of the first character of the name
   std::string name;     ///< Identifier as spelled in the source
   int ownerLine = 0;    ///< For a METHOD parameter: the line of its METHOD
   std::string scope;    ///< Empty for POU level, else the owning METHOD name
   SymCategory category = SymCategory::Unresolved;
   std::string typeText; ///< Declared or return type, when known
   std::string kindText; ///< "VAR", "METHOD", "FUNCTION_BLOCK", ... for display
};

/**
 * @brief Declarations of a translation unit, keyed by normalized name
 *
 * A name can appear more than once when a METHOD parameter shadows a POU
 * variable, so every declaration is kept and the caller picks the one matching
 * the scope it is navigating from.
 */
using DeclarationIndex = std::unordered_map<std::string, std::vector<Declaration>>;

/**
 * @brief Index every declaration a navigation could target
 * @param tu     Parsed translation unit
 * @param symTab Symbol table for that unit, used to classify each declaration
 * @return Declarations keyed by normalized name
 */
DeclarationIndex collectDeclarations(const TranslationUnit& tu,
                                     const st2cpp::semantic::SymbolTable& symTab);

// ============================================================================
//  Member access
//
//  What the '.' operator can reach on a value: the call interface, the
//  internal state and the methods of a function block, inheritance included.
// ============================================================================

/**
 * @brief How a typed pattern lines up with a candidate name
 *
 * The positions are what the list highlights, so a fuzzy hit shows the user
 * why it matched instead of just claiming it did.
 */
struct NameMatch
{
   bool matched = false;            ///< Pattern occurs in order inside the name
   int score = 0;                   ///< Higher is better; only meaningful if matched
   std::vector<int> positions;      ///< Matched indices into the name, ascending
};

/**
 * @brief Match a typed pattern against a candidate name, fuzzily
 * @param name    Candidate name as spelled in the source
 * @param pattern What the user has typed so far
 * @return The match, with `positions` empty when the pattern is empty
 *
 * Case-insensitive, as IEC 61131-3 identifiers are, and tolerant of
 * interleaved characters: `rst` matches `Reset`, `Reset` matches
 * `MotorResetState`, and `Reset` does not match `resets`.
 *
 * Ranking prefers, in order: the whole name, then a prefix, then a word
 * boundary, then a plain in-order match. Within that, characters that continue
 * a run, start a word or sit on a camel-case hump raise the score, and every
 * skipped character lowers it slightly, so the tightest match sorts first.
 */
NameMatch matchName(const std::string& name, const std::string& pattern);

/**
 * @brief One entry of a completion list
 *
 * The same struct carries both kinds of completion: the members reachable through
 * a '.' on a function block instance, and the plain names visible from where the
 * caret is. They are shown in the same list, filtered and ranked the same way, so
 * they share a type; only the set of `Kind` values is wider than a member needs.
 */
struct Suggestion
{
   /// What kind of thing this is, which is also what its colour means.
   enum class Kind
   {
      Parameter,  ///< VAR_INPUT / VAR_OUTPUT / VAR_IN_OUT
      State,      ///< VAR / VAR_TEMP / VAR RETAIN
      Method,     ///< METHOD
      Constant,   ///< VAR CONSTANT
      Function,   ///< FUNCTION
      Block,      ///< A FUNCTION_BLOCK instance, or the block's type name
      Type,       ///< A type name
      Enumerator  ///< An enumerator of an enum
   };

   std::string name;       ///< Identifier as spelled in the source
   std::string typeText;   ///< Declared type, or the return type of a method
   Kind kind = Kind::State;
   bool isConstant = false;
   bool isRetain = false;
   bool isAbstract = false;
   bool isOverride = false;
   bool inherited = false;  ///< Declared by a base block, not by this one
   std::string declaredIn;  ///< Base block name, set when inherited
   std::vector<std::string> parameterTypes; ///< Method parameter types, for display
   std::vector<std::string> parameterNames; ///< Method parameter names, for signature help
   std::vector<std::string> parameterDirs;  ///< "IN", "OUT" or "IN_OUT", empty when unknown
   std::string note;                        ///< Short remark after the name, such as "given"

   /// True when the pattern occurs in this name, fuzzily and case-insensitively.
   bool matches(const std::string& prefix) const;

   /// Indices of `name` that the last `matches` call matched, for highlighting.
   std::vector<int> matchPositions;
};

/// The name a member-completion candidate used to be known by. Kept so callers
/// and tests that only deal with members keep reading correctly.
using MemberAccess = Suggestion;
using MemberList = std::vector<Suggestion>;

/**
 * @brief Keep the members a pattern matches and order them best first
 * @param members  List to filter in place
 * @param pattern  What the user has typed after the '.'
 *
 * Every surviving member gets its `matchPositions` filled, which is what the
 * list draws highlighted. The sort is stable, so members that score equally
 * keep the block's declaration order.
 */
void rankMembers(MemberList& members, const std::string& pattern);

/**
 * @brief Everything reachable through '.' on a value of a function block type
 * @param symTab Symbol table of the analysis the instance belongs to
 * @param fbId   Symbol id of the FUNCTION_BLOCK
 * @return Parameters, state and methods, flattened over the EXTENDS chain
 *
 * The base chain comes first so the listing matches the block's declaration
 * order, and an entry a derived block redeclares appears once, as the derived
 * declaration. Empty when fbId is not a function block.
 */
MemberList collectBlockMembers(const st2cpp::semantic::SymbolTable& symTab,
                               st2cpp::semantic::SymbolId fbId);

/**
 * @brief The function block an instance name refers to, if any
 * @param symTab   Symbol table of the current analysis
 * @param scopeId  Scope the lookup starts from, e.g. a METHOD's scope
 * @param name     Instance name as written in the source
 * @return Symbol id of the FUNCTION_BLOCK, or 0 when the name is not one
 *
 * An instance is a variable whose type resolved to a function block, which is
 * how `inst : Motor` becomes a member provider. External instances (a block
 * declared in a sibling file) resolve too, once the descriptor carries its
 * members.
 */
st2cpp::semantic::SymbolId findInstanceBlock(const st2cpp::semantic::SymbolTable& symTab,
                                             st2cpp::semantic::ScopeId scopeId,
                                             const std::string& name);

/**
 * @brief What a line looks like at the cursor, for member completion
 */
struct MemberAccessPoint
{
   bool active = false;     ///< A '.' was typed and the cursor follows it
   std::string objectName;  ///< Identifier before the '.'
   std::string prefix;      ///< Part of the member name already typed
   int dotColumn = -1;      ///< 0-based column of the '.', -1 when inactive
};

/**
 * @brief Decide whether the cursor sits in a member access
 * @param line      Text of the current line
 * @param cursorCol 0-based column of the cursor within that line
 * @return The access being completed, `active` false when there is none
 *
 * Only an identifier (or nothing) may precede the '.', so a literal or a closing
 * parenthesis does not open completion. The prefix is the identifier fragment
 * already typed after the '.', which the caller filters on.
 */
MemberAccessPoint memberAccessPointAt(const std::string& line, int cursorCol);

/**
 * @brief Render a member the way it is offered in the completion list
 * @param member Member to describe
 * @return e.g. "Reset() : BOOL", "ticks : INT", "gain : REAL"
 */
std::string describeMember(const Suggestion& member);

/**
 * @brief The names visible from where the caret is, for plain identifier completion
 * @param symTab  Symbol table of the analysis
 * @param scopeId Scope the caret is in, so a method local shadows a POU variable
 * @return Parameters, variables, constants and methods of the enclosing scopes,
 *         then the declarations and library names visible from outside
 *
 * Ordered the way a reader scans: the innermost scope first and, inside a scope,
 * declaration order. A name declared in more than one place appears once, as the
 * innermost declaration, which is the one that wins in the source. The POU being
 * edited is left out: completing its own name inside itself is noise.
 */
/**
 * @brief The names visible from a scope, for completing a plain identifier
 * @param symTab            Symbol table of the file being edited
 * @param scopeId           Scope the caret is in
 * @param includeOwnMethods Also offer the methods of the block being edited
 *
 * A name is offered when the source could resolve it from where the caret is: the
 * scopes from that point outwards, plus the library side. The block's own methods
 * are part of that only in its own body, since a method reaches its siblings
 * through an instance rather than by name.
 */
MemberList collectScopeNames(const st2cpp::semantic::SymbolTable& symTab,
                            st2cpp::semantic::ScopeId scopeId,
                            bool includeOwnMethods = true);

// ============================================================================
//  Call sites and signature help
//
//  What a call looks like from the inside: the callee, the argument the cursor
//  sits in, and the parameter list to show while it is being typed.
// ============================================================================

/**
 * @brief The call the cursor sits inside, if any
 */
struct CallSite
{
   bool active = false;        ///< The cursor is between a callee's parentheses
   std::string objectName;     ///< Instance before the '.', empty for a plain call
   std::string calleeName;     ///< Member or function being called
   std::string label;          ///< "motore.Reset" or "MAX", as it appears in the source
   int argumentIndex = 0;      ///< Which argument the cursor is in, 0-based
   int parenLine = -1;         ///< Line of the opening parenthesis
   int parenColumn = -1;       ///< Column of the opening parenthesis
   int argumentLine = -1;      ///< Line the current argument's text starts on
   int argumentColumn = -1;    ///< Its first column, just past '(' or the last ','
};

/**
 * @brief Find the call whose argument list the cursor is inside
 * @param lines Text of the buffer, one entry per line
 * @param line  0-based line of the cursor
 * @param col   0-based column of the cursor
 * @return The call, `active` false when the cursor is not inside one
 *
 * The scan walks backwards over the whole buffer, so a call split over several
 * lines still resolves. It skips string literals, line comments and block
 * comments, and stops at a `;`, which is what keeps a `(` from an earlier
 * statement from being attributed to the current one.
 */
CallSite callSiteAt(const std::vector<std::string>& lines, int line, int col);

/**
 * @brief One parameter of a call, as the signature popup shows it
 */
struct SignatureParam
{
   std::string name;         ///< As declared, empty when the declaration is unnamed
   std::string type;         ///< Declared type
   std::string direction;    ///< "IN", "OUT" or "IN_OUT"; empty when not a parameter
   bool hasDefault = false;  ///< Declared with an initial value, so it may be omitted
};

/**
 * @brief The parameter list of a callable
 */
struct CallSignature
{
   std::string name;      ///< Member or function name, without the object
   std::string owner;     ///< Block the method belongs to, empty for a plain call
   std::string returnType;///< Return type, empty for a call with no result
   bool inherited = false;
   std::string declaredIn;
   std::vector<SignatureParam> params;
};

/**
 * @brief Resolve the parameters of the call at a call site
 * @param symTab  Symbol table of the current analysis
 * @param scopeId Scope the lookup starts from, so a METHOD local wins
 * @param call    The call site to resolve
 * @param out     Filled with the signature on success, untouched on failure
 * @return True when the callee was found and its parameters were read
 *
 * Handles the three shapes a call takes in ST: a method through an instance
 * (`inst.Method(`), a FUNCTION or a FUNCTION_BLOCK instance called directly
 * (`MyBlock(`), and a POU reached through its type name. A METHOD is looked up
 * over the whole EXTENDS chain, since the block may not declare it itself.
 */
bool resolveCallSignature(const st2cpp::semantic::SymbolTable& symTab,
                          st2cpp::semantic::ScopeId scopeId,
                          const CallSite& call,
                          CallSignature& out);

/**
 * @brief One run of text in a rendered signature
 */
struct SignatureSegment
{
   std::string text;
   bool isParameter = false; ///< A parameter, which is what gets highlighted
};

/**
 * @brief Break a signature into the runs the popup draws
 * @param signature      What resolveCallSignature produced
 * @param activeArgument Argument the cursor is in, 0-based
 * @return Segments in reading order, each a plain string or a parameter
 *
 * A parameter count that has run past the last one still yields a run, so the
 * popup can flag an argument the callee does not take.
 */
std::vector<SignatureSegment> signatureSegments(const CallSignature& signature, int activeArgument);

/**
 * @brief Render a signature as one line, the way the tests and the label use it
 * @param signature      What resolveCallSignature produced
 * @param activeArgument Argument the cursor is in, 0-based
 * @return e.g. "Reset(set : BOOL, out : INT) : BOOL"
 */
std::string formatSignature(const CallSignature& signature, int activeArgument);

} // namespace ST
} // namespace undoApp
