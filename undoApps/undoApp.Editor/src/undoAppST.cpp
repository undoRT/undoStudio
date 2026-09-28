/**
 * @file undoAppST.cpp
 * @brief Implementation of the Structured Text editor undoApp
 * @ingroup undoapps
 *
 * This file implements the ST (Structured Text) development environment
 * as a plugin for undoStudio. It provides:
 * - Workspace explorer with file tree
 * - Two-pane editor (Variables + Body) like TwinCAT/CODESYS
 * - POU creation (Program, Function Block, Function)
 * - Syntax highlighting using custom ST LanguageDefinition
 * - Semantic validation using the st2cpp parser and AST
 * - AST outline panel
 * - Drag and drop file management
 * - Native file browser integration
 *
 * @author Salvatore Bamundo
 * @date June 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoAppST.hpp"
#include "undoAppSTSemantic.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"
#include "undoStudio/core/Application.hpp"
#include "undoStudio/core/ProjectManager.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <TextEditor.h>

// st2cpp headers
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "ast/AST.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cctype>
#include <algorithm>
#include <functional>
#include <chrono>

#ifdef _WIN32
#include <process.h>
#else
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifdef USE_TINYFILEDIALOGS
#include <tinyfiledialogs.h>
#endif

namespace fs = std::filesystem;

namespace undoApp {
namespace ST {

namespace {

/// @brief Where the ST editor keeps what it remembers between runs
constexpr const char* kEditorSettingsFile = "undoApp.Editor.ini";

/**
 * @brief Read the splitter position
 * @param pos Set to the stored value, or left alone when there is none
 */
void loadSplitterPos(float& pos)
{
   std::ifstream in(kEditorSettingsFile);
   std::string line;
   while (std::getline(in, line)) {
      const size_t equals = line.find('=');
      if (equals == std::string::npos || line.compare(0, 12, "splitter_pos=") != 0) {
         continue;
      }
      try {
         const float value = std::stof(line.substr(equals + 1));
         pos = std::max(0.1f, std::min(0.9f, value));
      } catch (const std::exception&) {
         // A value that is not a number leaves the default where it is.
      }
   }
}

/// @brief Write the splitter position, once the drag is over
/// @param pos Position to store
void saveSplitterPos(float pos)
{
   std::ofstream out(kEditorSettingsFile);
   if (!out.good()) {
      return; // nowhere to keep it, which costs the preference and nothing else
   }
   out << "# undoApp.Editor preferences, written by undoStudio\n"
       << "splitter_pos=" << pos << "\n";
}

} // namespace


// ============================================================================
// Project strictness -> st2cpp analyzer strictness
//
// project.toml declares it as [semantics] strictness = "on" | "off"; a new
// project is created with "on". "on" makes the analyzer enforce the IEC
// 61131-3 implicit-conversion rules, so a lossy or cross-family implicit
// assignment (INT := REAL) is reported instead of being silently
// static_cast-ed away by the generated C++.
// ============================================================================
static st2cpp::semantic::SemanticAnalyzer::Strictness projectStrictness()
{
   const bool strict = undoStudio::core::ProjectManager::getInstance().getStrictness()
                       == undoStudio::core::Strictness::On;
   return strict ? st2cpp::semantic::SemanticAnalyzer::Strictness::Strict
                 : st2cpp::semantic::SemanticAnalyzer::Strictness::Permissive;
}

// ============================================================================
// FUNCTION_BLOCK / METHOD text scanner
// ============================================================================
//
// Splits the inner content of a POU (everything between the header line and
// the final END_xxx) into: the POU-level VAR* sections, the list of METHODs
// (each with its own VAR* text and body text), and the trailing cyclic body.
//
// This replaces naive content.find("VAR") / content.rfind("END_VAR"): with
// METHODs in the file (each carrying its own VAR_INPUT/VAR_OUTPUT/VAR/END_VAR),
// the LAST "END_VAR" in the whole file lives inside the last method, not
// after the POU's own variable sections - rfind-based splitting would corrupt
// both the method list and the cyclic body.

namespace {

/// Corner radius shared by the member list and the signature help, so both
/// overlays read as one piece of UI
constexpr float kOverlayRounding = 6.0f;

std::string trimLine(const std::string& s)
{
   size_t a = s.find_first_not_of(" \t\r");
   if (a == std::string::npos) {
      return "";
   }
   size_t b = s.find_last_not_of(" \t\r");
   return s.substr(a, b - a + 1);
}

/// Case-insensitive, whole-word check that `trimmed` starts with keyword `kw`
bool startsWithKeyword(const std::string& trimmed, const std::string& kw)
{
   if (trimmed.size() < kw.size()) {
      return false;
   }
   for (size_t i = 0; i < kw.size(); ++i) {
      if (::toupper((unsigned char) trimmed[i]) != ::toupper((unsigned char) kw[i])) {
         return false;
      }
   }
   if (trimmed.size() > kw.size()) {
      char next = trimmed[kw.size()];
      if (isalnum((unsigned char) next) || next == '_') {
         return false;
      }
   }
   return true;
}

bool isVarSectionStart(const std::string& trimmed)
{
   static const char* kinds[] = {"VAR_INPUT", "VAR_OUTPUT", "VAR_IN_OUT", "VAR_EXTERNAL", "VAR_GLOBAL", "VAR_TEMP", "VAR"};
   for (auto* k : kinds) {
      if (startsWithKeyword(trimmed, k)) {
         return true;
      }
   }
   return false;
}

struct FBParts
{
   std::string pouVarText;
   std::string cyclicBody;
   std::vector<undoApp::ST::MethodData> methods;
};

FBParts splitFunctionBlockBody(const std::string& innerContent)
{
   FBParts parts;

   std::vector<std::string> lines;
   {
      std::istringstream stream(innerContent);
      std::string rawLine;
      while (std::getline(stream, rawLine)) {
         lines.push_back(rawLine);
      }
   }

   size_t i = 0;
   std::ostringstream pouVar;

   // Phase 1: POU-level VAR* sections, until METHOD or content ends
   while (i < lines.size()) {
      std::string trimmed = trimLine(lines[i]);
      if (trimmed.empty()) {
         ++i;
         continue;
      }
      if (isVarSectionStart(trimmed)) {
         while (i < lines.size()) {
            pouVar << lines[i] << "\n";
            if (startsWithKeyword(trimLine(lines[i]), "END_VAR")) {
               ++i;
               break;
            }
            ++i;
         }
      } else {
         break;
      }
   }

   // Phase 2: METHOD ... END_METHOD blocks
   while (i < lines.size()) {
      std::string trimmed = trimLine(lines[i]);
      if (trimmed.empty()) {
         ++i;
         continue;
      }
      if (!startsWithKeyword(trimmed, "METHOD")) {
         break;
      }

      undoApp::ST::MethodData method;
      method.visibility = "PUBLIC";
      {
         size_t mpos = trimmed.find("METHOD");
         std::string rest = trimmed.substr(mpos + 6);
         size_t colonPos = rest.find(':');
         std::string namePart = colonPos != std::string::npos ? rest.substr(0, colonPos) : rest;
         std::string retPart = colonPos != std::string::npos ? rest.substr(colonPos + 1) : "";
         namePart = trimLine(namePart);
         method.returnType = trimLine(retPart);

         // The name must stay the bare identifier. Keeping the access specifier in
         // it made "METHOD PUBLIC Twice" register as a method named
         // "PUBLIC Twice", and every lookup by the real name then missed: the
         // method tab would not open on a cross-file jump, and the edits of a
         // reopened tab were not written back to the model.
         size_t spacePos = namePart.find_first_of(" \t");
         if (spacePos != std::string::npos) {
            const std::string head = namePart.substr(0, spacePos);
            if (head == "PUBLIC" || head == "PRIVATE" || head == "PROTECTED" || head == "INTERNAL") {
               method.visibility = head;
               namePart = trimLine(namePart.substr(spacePos));
            }
         }
         method.name = namePart;
      }
      ++i;

      std::ostringstream varBuf, bodyBuf;
      bool inVarPhase = true;
      while (i < lines.size()) {
         std::string t = trimLine(lines[i]);
         if (startsWithKeyword(t, "END_METHOD")) {
            ++i;
            break;
         }
         if (t.empty()) {
            ++i;
            continue;
         }
         if (inVarPhase && isVarSectionStart(t)) {
            while (i < lines.size()) {
               varBuf << lines[i] << "\n";
               if (startsWithKeyword(trimLine(lines[i]), "END_VAR")) {
                  ++i;
                  break;
               }
               ++i;
            }
            continue;
         }
         inVarPhase = false;
         bodyBuf << lines[i] << "\n";
         ++i;
      }
      method.variablesText = varBuf.str();
      method.bodyText = bodyBuf.str();
      parts.methods.push_back(std::move(method));
   }

   // Phase 3: whatever remains is the cyclic body
   std::ostringstream bodyOut;
   for (; i < lines.size(); ++i) {
      bodyOut << lines[i] << "\n";
   }

   parts.pouVarText = pouVar.str();
   parts.cyclicBody = bodyOut.str();
   return parts;
}

} // namespace

// ============================================================================
// Static Helper Functions
// ============================================================================

/**
 * @brief Build a hierarchical file tree from a directory path
 * @param node Root node to populate with children
 * @param root Filesystem path to scan recursively
 *
 * Recursively scans the directory and builds a hierarchical tree structure
 * of files and folders. Directories are sorted before files alphabetically.
 */
static void buildFileTree(FileNode& node, const fs::path& root)
{
   node.children.clear();
   try {
      for (const auto& entry : fs::directory_iterator(root)) {
         FileNode child;
         child.name = entry.path().filename().string();
         child.path = entry.path().string();
         child.isDirectory = entry.is_directory();

         if (entry.is_directory()) {
            buildFileTree(child, entry.path());
         }
         node.children.push_back(child);
      }

      // Sort: directories first, then files alphabetically
      std::sort(node.children.begin(), node.children.end(), [](const FileNode& a, const FileNode& b) {
         if (a.isDirectory != b.isDirectory) {
            return a.isDirectory > b.isDirectory;
         }
         return a.name < b.name;
      });
   } catch (const std::exception& e) {
      std::cerr << "[undoApp.ST] Error building file tree: " << e.what() << std::endl;
   }
}

// ============================================================================
// ST Language Definition for TextEditor
// ============================================================================

/**
 * @brief Create a LanguageDefinition for Structured Text
 * @return TextEditor::LanguageDefinition with ST keywords, types, and comments
 *
 * Adds all keywords both in uppercase and lowercase to support case-insensitive
 * matching. Also configures comment delimiters, preprocessor char, and token
 * regex patterns for syntax highlighting.
 */
static TextEditor::LanguageDefinition CreateSTLanguageDefinition()
{
   TextEditor::LanguageDefinition lang;
   lang.mName = "Structured Text";

   // ========================================================================
   // 1. Keywords - Add both uppercase and lowercase variants
   // ========================================================================
   const std::vector<std::string> keywords = {// POU Keywords
                                              "PROGRAM",
                                              "END_PROGRAM",
                                              "FUNCTION_BLOCK",
                                              "END_FUNCTION_BLOCK",
                                              "FUNCTION",
                                              "END_FUNCTION",
                                              "METHOD",
                                              "END_METHOD",
                                              "PROPERTY",
                                              "END_PROPERTY",

                                              // Variable Keywords
                                              "VAR",
                                              "END_VAR",
                                              "VAR_INPUT",
                                              "VAR_OUTPUT",
                                              "VAR_IN_OUT",
                                              "VAR_EXTERNAL",
                                              "VAR_GLOBAL",
                                              "VAR_TEMP",
                                              "CONSTANT",
                                              "RETAIN",
                                              "AT",

                                              // Control Flow
                                              "IF",
                                              "THEN",
                                              "ELSIF",
                                              "ELSE",
                                              "END_IF",
                                              "FOR",
                                              "TO",
                                              "BY",
                                              "DO",
                                              "END_FOR",
                                              "WHILE",
                                              "END_WHILE",
                                              "REPEAT",
                                              "UNTIL",
                                              "END_REPEAT",
                                              "CASE",
                                              "OF",
                                              "END_CASE",
                                              "EXIT",
                                              "RETURN",

                                              // OOP Keywords
                                              "INTERFACE",
                                              "END_INTERFACE",
                                              "EXTENDS",
                                              "IMPLEMENTS",
                                              "ABSTRACT",
                                              "FINAL",
                                              "OVERRIDE",
                                              "SUPER",
                                              "PRIVATE",
                                              "PROTECTED",
                                              "PUBLIC",

                                              // Operators
                                              "NOT",
                                              "AND",
                                              "OR",
                                              "XOR",
                                              "MOD",
                                              "ADR",
                                              "SIZEOF",

                                              // Literals
                                              "TRUE",
                                              "FALSE",

                                              // Type Keywords
                                              "TYPE",
                                              "END_TYPE",
                                              "STRUCT",
                                              "END_STRUCT",
                                              "ENUM",
                                              "ARRAY",
                                              "POINTER",
                                              "REF_TO",

                                              // Data Types
                                              "BOOL",
                                              "SINT",
                                              "INT",
                                              "DINT",
                                              "LINT",
                                              "USINT",
                                              "UINT",
                                              "UDINT",
                                              "ULINT",
                                              "REAL",
                                              "LREAL",
                                              "BYTE",
                                              "WORD",
                                              "DWORD",
                                              "LWORD",
                                              "STRING",
                                              "WSTRING",
                                              "TIME",
                                              "DATE",
                                              "DT",
                                              "TOD"};

   // Insert both uppercase and lowercase versions
   for (const auto& kw : keywords) {
      lang.mKeywords.insert(kw);
      std::string lower = kw;
      std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
      lang.mKeywords.insert(lower);
   }

   // ========================================================================
   // 2. Comments
   // ========================================================================
   lang.mCommentStart = "(*";
   lang.mCommentEnd = "*)";
   lang.mSingleLineComment = "//";

   // ========================================================================
   // 3. Preprocessor / Attributes
   // ========================================================================
   lang.mPreprocChar = '{';

   // ========================================================================
   // 4. Case sensitivity: ST is case-insensitive
   // ========================================================================
   lang.mCaseSensitive = false;

   // ========================================================================
   // 5. Auto-indentation
   // ========================================================================
   lang.mAutoIndentation = true;

   // ========================================================================
   // 6. Token regex patterns for syntax highlighting
   // Order matters: the first pattern that matches at the current position wins.
   // ========================================================================
   lang.mTokenRegexStrings.push_back(
      std::make_pair<std::string, TextEditor::PaletteIndex>("\\'(\\\\.|[^\\'])*\\'",
                                                            TextEditor::PaletteIndex::String)); // 'ST string literal'

   lang.mTokenRegexStrings.push_back(
      std::make_pair<std::string, TextEditor::PaletteIndex>("16#[0-9a-fA-F_]+",
                                                            TextEditor::PaletteIndex::Number)); // Hex literal, e.g. 16#FF

   lang.mTokenRegexStrings.push_back(
      std::make_pair<std::string, TextEditor::PaletteIndex>("2#[01_]+", TextEditor::PaletteIndex::Number)); // Binary literal, e.g. 2#1010

   lang.mTokenRegexStrings.push_back(
      std::make_pair<std::string, TextEditor::PaletteIndex>("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?",
                                                            TextEditor::PaletteIndex::Number)); // Decimal and real numbers

   lang.mTokenRegexStrings.push_back(
      std::make_pair<std::string, TextEditor::PaletteIndex>("[a-zA-Z_][a-zA-Z0-9_]*",
                                                            TextEditor::PaletteIndex::Identifier)); // Identifiers

   lang.mTokenRegexStrings.push_back(
      std::make_pair<std::string, TextEditor::PaletteIndex>(":=|[+\\-*/<>=()\\[\\].,;:%]",
                                                            TextEditor::PaletteIndex::Punctuation)); // Operators and punctuation

   return lang;
}

// ============================================================================
// Singleton Instance
// ============================================================================

/**
 * @brief Get the singleton instance of STApp
 * @return Reference to the single STApp instance
 */
STApp& STApp::getInstance()
{
   static STApp instance;
   return instance;
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

/**
 * @brief Initialize the ST undoApp
 * @return true on success
 *
 * Creates the editor widgets, registers all UI panels with ImGuiManager,
 * and sets up the ST language definition for syntax highlighting.
 */
bool STApp::initialize()
{
   if (m_initialized) {
      return true;
   }
   std::cout << "[undoApp.ST] Initializing..." << std::endl;

   setupEditors();
   registerPanels();
   m_initialized = true;
   std::cout << "[undoApp.ST] Initialization complete" << std::endl;
   return true;
}

/**
 * @brief Shutdown the ST undoApp
 *
 * Removes all registered panels from ImGuiManager.
 */
void STApp::shutdown()
{
   if (!m_initialized) {
      return;
   }
   std::cout << "[undoApp.ST] Shutting down..." << std::endl;

   auto& mgr = undoStudio::ui::ImGuiManager::getInstance();
   mgr.removePanel("Workspace");
   mgr.removePanel("ST Editor");
   mgr.removePanel("ST Output");
   mgr.removePanel("ST Outline");

   m_initialized = false;
   std::cout << "[undoApp.ST] Shutdown complete" << std::endl;
}

/**
 * @brief Register UI panels with ImGuiManager
 *
 * Uses std::bind to pass non-static member functions as callbacks.
 * Registers four panels: Workspace, ST Editor, ST Output, and ST Outline.
 */
void STApp::registerPanels()
{
   auto& mgr = undoStudio::ui::ImGuiManager::getInstance();

   mgr.addPanel("Workspace", std::bind(&STApp::renderWorkspacePanel, this));
   mgr.addPanel("ST Editor", std::bind(&STApp::renderEditorPanel, this));
   mgr.addPanel("ST Output", std::bind(&STApp::renderOutputPanel, this));
   mgr.addPanel("ST Outline", std::bind(&STApp::renderOutlinePanel, this));

   std::cout << "[undoApp.ST] Panels registered" << std::endl;
}

// ============================================================================
// Editor Setup
// ============================================================================

/**
 * @brief Setup and configure the TextEditor widgets
 *
 * Creates the Variables and Body editors, configures their palette with
 * the undoStudio dark theme, and sets the ST LanguageDefinition.
 * Current line highlight is enabled with corporate azure color.
 */
void STApp::setupEditors()
{
   // The splitter is where the user left it, from the start of the run rather than
   // from the first time they drag it.
   loadSplitterPos(m_splitterPos);

   m_variablesEditor = std::make_unique<TextEditor>();
   m_bodyEditor = std::make_unique<TextEditor>();

   // Create the ST language definition
   auto stLang = CreateSTLanguageDefinition();

   // Setup dark palette matching undoStudio theme
   TextEditor::Palette palette;
   palette[(int) TextEditor::PaletteIndex::Default] = IM_COL32(230, 230, 230, 255);
   palette[(int) TextEditor::PaletteIndex::Keyword] = IM_COL32(0, 204, 255, 255);  // Cyan
   palette[(int) TextEditor::PaletteIndex::Number] = IM_COL32(255, 179, 51, 255);  // Orange
   palette[(int) TextEditor::PaletteIndex::String] = IM_COL32(153, 255, 153, 255); // Light green
   palette[(int) TextEditor::PaletteIndex::CharLiteral] = IM_COL32(153, 255, 153, 255);
   palette[(int) TextEditor::PaletteIndex::Punctuation] = IM_COL32(255, 255, 255, 255);
   palette[(int) TextEditor::PaletteIndex::Preprocessor] = IM_COL32(179, 179, 179, 255);
   palette[(int) TextEditor::PaletteIndex::Identifier] = IM_COL32(204, 230, 255, 255); // Light blue
   palette[(int) TextEditor::PaletteIndex::KnownIdentifier] = IM_COL32(204, 230, 255, 255);
   palette[(int) TextEditor::PaletteIndex::Comment] = IM_COL32(102, 179, 102, 255); // Green
   palette[(int) TextEditor::PaletteIndex::MultiLineComment] = IM_COL32(102, 179, 102, 255);
   palette[(int) TextEditor::PaletteIndex::Background] = IM_COL32(13, 20, 38, 255); // Dark blue
   palette[(int) TextEditor::PaletteIndex::Cursor] = IM_COL32(0, 204, 255, 255);    // Cyan
   palette[(int) TextEditor::PaletteIndex::Selection] = IM_COL32(51, 77, 128, 204);
   palette[(int) TextEditor::PaletteIndex::ErrorMarker] = IM_COL32(255, 51, 51, 40);   // Red
   palette[(int) TextEditor::PaletteIndex::LineNumber] = IM_COL32(128, 140, 160, 255); // Grey-blue

   // Current line highlight
   palette[(int) TextEditor::PaletteIndex::CurrentLineFill] = IM_COL32(0, 180, 216, 40); // Transparent cyan
   palette[(int) TextEditor::PaletteIndex::CurrentLineEdge] = IM_COL32(0, 180, 216, 80); // Slightly more visible

   // Semantic colors, applied from the st2cpp symbol table. Deliberately
   // distinct in hue and lightness so the categories stay separable on the
   // dark background, and distinct from the plain-identifier color above.
   palette[(int) TextEditor::PaletteIndex::SemVariable] = IM_COL32(126, 214, 223, 255);    // Teal
   palette[(int) TextEditor::PaletteIndex::SemParameter] = IM_COL32(255, 170, 100, 255);   // Warm orange
   palette[(int) TextEditor::PaletteIndex::SemFunction] = IM_COL32(180, 140, 255, 255);   // Violet
   palette[(int) TextEditor::PaletteIndex::SemType] = IM_COL32(255, 121, 198, 255);        // Pink
   palette[(int) TextEditor::PaletteIndex::SemField] = IM_COL32(120, 220, 140, 255);       // Green
   palette[(int) TextEditor::PaletteIndex::SemEnumerator] = IM_COL32(255, 214, 102, 255);  // Sand
   palette[(int) TextEditor::PaletteIndex::SemConstant] = IM_COL32(160, 255, 214, 255);   // Mint

   for (auto* ed : {m_variablesEditor.get(), m_bodyEditor.get()}) {
      ed->SetPalette(palette);
      ed->SetLanguageDefinition(stLang);
      ed->SetShowWhitespaces(false);
      ed->SetTabSize(3);
   }
}

// ============================================================================
// Semantic analysis
// ============================================================================

/**
 * @brief Outcome of one st2cpp analysis pass
 */
struct SemanticReport
{
   std::vector<ParseError> errors;  ///< Errors: become markers on the editors
   std::vector<std::string> notes;  ///< Warnings and notes: Output panel only
};

/**
 * @brief Split an analysis result by severity
 * @param info       Result of one st2cpp analysis pass
 * @param sourceName File name, used in diagnostic locations
 * @return Errors and warnings, already expressed as ParseError
 *
 * The analysis itself is delegated to the compiler front-end rather than
 * reimplemented here. A hand-rolled name check cannot see the symbol table, so
 * it both misses real type errors (`INT := someString + 1`) and invents false
 * positives on perfectly valid calls, such as a METHOD invoked from its own POU
 * body.
 */
static SemanticReport reportFromDiagnostics(const st2cpp::semantic::SemanticInfo& info,
                                            const std::string& sourceName)
{
   SemanticReport report;

   for (const auto& diag : info.diagnostics.all()) {
      if (diag.location.line == 0) {
         continue; // no usable position: cannot be mapped to an editor
      }

      // The code name makes the cause obvious in the Output panel, and it is
      // what a reader would grep for in the compiler front-end.
      std::string text = std::string(st2cpp::semantic::diagnosticCodeToString(diag.code)) + ": " + diag.message;
      if (!diag.suggestion.empty()) {
         text += " (" + diag.suggestion + ")";
      }

      if (diag.severity == st2cpp::semantic::DiagnosticSeverity::Error) {
         report.errors.emplace_back(text, diag.location.line, diag.location.column, sourceName);
      } else {
         const char* label = (diag.severity == st2cpp::semantic::DiagnosticSeverity::Warning) ? "warning" : "note";
         report.notes.push_back(std::string(label) + " at line " + std::to_string(diag.location.line) + ": " + text);
      }
   }

   return report;
}

// ============================================================================
// Additional syntax check: missing semicolons
// ============================================================================

/**
 * @brief Check for missing semicolons in the source code
 * @param source The full ST source code to check
 * @param errors Vector to append errors
 *
 * Uses the lexer to identify tokens and checks that each line containing
 * a statement (assignment, function call, etc.) ends with a semicolon.
 * Lines that are empty, comments, or declarations (VAR, END_VAR, etc.)
 * are ignored.
 */
/**
 * @brief Test whether a trimmed line ends with a standalone keyword
 * @param trimmed Line without leading whitespace
 * @param keyword Uppercase keyword to look for
 * @return true when the last word is exactly the keyword
 *
 * Word-boundary aware on purpose: an identifier such as `MYTHEN` must not be
 * mistaken for the `THEN` that opens an IF block.
 */
static bool endsWithKeyword(const std::string& trimmed, const char* keyword)
{
   const size_t kwLen = std::strlen(keyword);
   if (trimmed.size() <= kwLen) {
      return false;
   }
   if (trimmed.compare(trimmed.size() - kwLen, kwLen, keyword) != 0) {
      return false;
   }
   const char before = trimmed[trimmed.size() - kwLen - 1];
   return std::isalnum(static_cast<unsigned char>(before)) == 0 && before != '_';
}

static void checkMissingSemicolons(const std::string& source, std::vector<ParseError>& errors){
   std::istringstream stream(source);
   std::string line;
   int lineNum = 1;

   while (std::getline(stream, line)) {
      // Trim whitespace
      size_t start = line.find_first_not_of(" \t");
      if (start == std::string::npos) {
         ++lineNum;
         continue; // empty line
      }
      std::string trimmed = line.substr(start);

      // Skip lines that are comments or declarations
      if (trimmed.empty() || trimmed.rfind("(*", 0) == 0 || trimmed.rfind("//", 0) == 0 || trimmed.rfind("PROGRAM", 0) == 0
          || trimmed.rfind("END_PROGRAM", 0) == 0 || trimmed.rfind("FUNCTION_BLOCK", 0) == 0
          || trimmed.rfind("END_FUNCTION_BLOCK", 0) == 0 || trimmed.rfind("FUNCTION", 0) == 0 || trimmed.rfind("END_FUNCTION", 0) == 0
          || trimmed.rfind("VAR", 0) == 0 || trimmed.rfind("END_VAR", 0) == 0 || trimmed.rfind("TYPE", 0) == 0
          || trimmed.rfind("END_TYPE", 0) == 0 || trimmed.rfind("STRUCT", 0) == 0 || trimmed.rfind("END_STRUCT", 0) == 0
          || trimmed.rfind("ENUM", 0) == 0 || trimmed.rfind("METHOD", 0) == 0 || trimmed.rfind("END_METHOD", 0) == 0
          || trimmed.rfind("PROPERTY", 0) == 0 || trimmed.rfind("END_PROPERTY", 0) == 0) {
         ++lineNum;
         continue;
      }

      // Check if line ends with ';' (ignoring trailing whitespace)
      size_t last = line.find_last_not_of(" \t");
      if (last != std::string::npos && line[last] != ';') {
         // A statement header opens a block and is not terminated: IF/ELSIF ...
         // THEN, FOR/WHILE ... DO, CASE ... OF, ELSE, REPEAT, UNTIL ...
         const char* const kHeaders[] = {"THEN", "DO", "OF", "ELSE", "UNTIL", "REPEAT"};
         bool isBlockHeader = false;
         for (const char* kw : kHeaders) {
            if (endsWithKeyword(trimmed, kw)) {
               isBlockHeader = true;
               break;
            }
         }

         // But skip lines that are just "{" or "}" or other preprocessor
         if (!isBlockHeader && trimmed != "{" && trimmed != "}" && trimmed != "(*" && trimmed != "*)") {
            std::ostringstream oss;
            oss << "Missing semicolon at end of line";
            errors.emplace_back(oss.str(), lineNum, 0);
         }
      }
      ++lineNum;
   }
}

// ============================================================================
// Error Mapping Helpers
// ============================================================================

/**
 * @brief Count the number of lines in a text string
 * @param text The text to count lines in
 * @return Number of lines (1 for empty string)
 */
int STApp::countLines(const std::string& text)
{
   if (text.empty()) {
      return 0;
   }
   int count = 1;
   for (char c : text) {
      if (c == '\n') {
         ++count;
      }
   }
   return count;
}

/**
 * @brief Close the currently open ST file and clear the editor state
 *
 * The workspace tree is left untouched: only the per-file state is dropped, so
 * the user can pick another file straight away. Semantic spans are cleared
 * along with the text, otherwise they would be re-applied to the new content.
 */
void STApp::closeFile()
{
   m_currentFilePath.clear();
   m_currentFileContent.clear();
   m_ast.reset();
   m_errors.clear();
   m_outputLines.clear();

   m_pouName.clear();
   m_pouType = POUType::Program;
   m_functionReturnType.clear();
   m_currentContext.clear();
   m_draggedItemPath.clear();
   m_dropTargetPath.clear();

   resetMethodState();

   for (auto* ed : {m_variablesEditor.get(), m_bodyEditor.get()}) {
      if (ed) {
         ed->ClearSemanticTokens();
         ed->SetErrorMarkers(TextEditor::ErrorMarkers());
         ed->SetText("");
      }
   }
}

// ============================================================================
// Unsaved-changes handling
// ============================================================================

/**
 * @brief Track whether the editor text differs from the file on disk
 *
 * TextEditor::IsTextChanged() is a one-shot flag cleared at the start of every
 * Render(), so it has to be sampled after rendering and accumulated into a
 * sticky flag. A load sets it too, hence the skipped frame (see below).
 */
void STApp::updateDirtyState()
{
   // TextEditor::IsTextChanged() is also raised by the SetText calls a load
   // performs, and it is cleared at the start of the next Render(). Sampling it
   // on the frame right after loading would therefore report the load itself as
   // an edit, so that one frame is skipped and the flag is consumed by the
   // render that follows.
   if (m_ignoreChangeFrames > 0) {
      --m_ignoreChangeFrames;
      return;
   }

   bool changed = false;
   if (m_variablesEditor && m_variablesEditor->IsTextChanged()) {
      changed = true;
   }
   if (m_bodyEditor && m_bodyEditor->IsTextChanged()) {
      changed = true;
   }
   for (auto& [name, pair] : m_methodEditors) {
      if ((pair.variables && pair.variables->IsTextChanged()) || (pair.body && pair.body->IsTextChanged())) {
         changed = true;
      }
   }

   if (changed) {
      m_isDirty = true;
   }
}

/**
 * @brief Open a file, asking first when there are unsaved changes
 * @param path File to open
 *
 * Every path that switches the open file goes through here, so a Ctrl+Click
 * into another file cannot silently discard work.
 */
void STApp::requestOpenFile(const std::string& path)
{
   if (path.empty() || path == m_currentFilePath) {
      return;
   }
   if (m_isDirty) {
      m_pendingOpenPath = path;
      m_showDirtyPrompt = true;
      return;
   }
   openFile(path);
}

/**
 * @brief Open a file and land on one of its METHOD tabs
 * @param filePath   File that owns the method
 * @param methodName Method to show
 *
 * When the file is not open yet, the tab request is deferred until the switch
 * completes, so it survives the unsaved-changes prompt.
 */
void STApp::openMethodOf(const std::string& filePath, const std::string& methodName)
{
   if (filePath == m_currentFilePath) {
      revealMethodTab(methodName);
      return;
   }
   m_pendingTabAfterOpen = methodName;
   requestOpenFile(filePath);
}

/**
 * @brief Render the save / discard confirmation
 */
void STApp::renderDirtyPrompt()
{
   if (!m_showDirtyPrompt) {
      return;
   }

   ImGui::OpenPopup("Unsaved changes");
   if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      const std::string name = m_currentFilePath.empty() ? std::string("(unsaved)") : fs::path(m_currentFilePath).filename().string();
      ImGui::Text("'%s' has unsaved changes.", name.c_str());
      ImGui::Text("Save them before opening another file?");
      ImGui::Separator();

      if (ImGui::Button("Save")) {
         saveCurrentFile();
         const std::string target = m_pendingOpenPath;
         m_pendingOpenPath.clear();
         m_showDirtyPrompt = false;
         m_isDirty = false;
         ImGui::CloseCurrentPopup();
         if (!target.empty()) {
            openFile(target);
         }
      }
      ImGui::SameLine();
      if (ImGui::Button("Discard")) {
         const std::string target = m_pendingOpenPath;
         m_pendingOpenPath.clear();
         m_showDirtyPrompt = false;
         m_isDirty = false;
         ImGui::CloseCurrentPopup();
         if (!target.empty()) {
            openFile(target);
         }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_pendingOpenPath.clear();
         m_showDirtyPrompt = false;
         m_isDirty = true; // still unsaved
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }
}

// ============================================================================
// Workspace declaration index
// ============================================================================

/**
 * @brief Index the declarations of one .st file
 * @param path     File to parse
 * @param outDecls Accumulator, keyed by normalized name
 *
 * Each file is parsed standalone: navigation only needs to know *where* a name
 * is declared, not whether the project as a whole type-checks. Full cross-file
 * type resolution would need the other files fed in as a LibraryRegistry.
 */
void STApp::indexSTFile(const std::string& path,
                        std::unordered_map<std::string, std::vector<WorkspaceDeclaration>>& outDecls) const
{
   std::ifstream file(path);
   if (!file.is_open()) {
      return;
   }
   std::stringstream buffer;
   buffer << file.rdbuf();
   const std::string source = buffer.str();

   TranslationUnit tu;
   try {
      Lexer lexer(source);
      Parser parser(std::move(lexer.tokenize()));
      tu = parser.parseTranslationUnit();
   } catch (const std::exception&) {
      return; // a file that does not parse simply contributes no declarations
   }

   st2cpp::semantic::SemanticAnalyzer analyzer;
   analyzer.setSourceName(path);
   const st2cpp::semantic::SemanticInfo info = analyzer.analyze(tu, projectStrictness());
   if (!info.symbolTable) {
      return;
   }

   // Record the file's METHODs so the workspace tree can show them nested
   // under their FUNCTION_BLOCK.
   {
      std::vector<WorkspaceMethod> methods;
      for (const auto& pou : tu.pous) {
         if (pou.kind != POUKind::FUNCTION_BLOCK) {
            continue; // only a FUNCTION_BLOCK owns methods
         }
         for (const auto& m : pou.methods) {
            WorkspaceMethod wm;
            wm.name = m.name;
            wm.returnType = describeType(m.returnType);
            wm.line = (int)m.line;
            methods.push_back(std::move(wm));
         }
      }
      if (!methods.empty()) {
         m_workspaceMethods[path] = std::move(methods);
      }
   }

   // collectDeclarations works in generated-file coordinates; for indexing we
   // only need the positions, which are identical here because the file being
   // parsed is the file itself.
   const DeclarationIndex decls = collectDeclarations(tu, *info.symbolTable);

   std::vector<std::string> fileLines;
   {
      std::istringstream stream(source);
      std::string l;
      while (std::getline(stream, l)) {
         fileLines.push_back(l);
      }
   }

   // Find the line that declares a name, used for METHOD parameters, whose
   // position the parser does not report.
   auto findDeclLine = [&](int fromLine, const std::string& name) {
      for (int l = std::max(1, fromLine); l <= (int)fileLines.size() && l < fromLine + 40; ++l) {
         const std::string& text = fileLines[l - 1];
         const size_t at = text.find(name);
         if (at == std::string::npos) {
            continue;
         }
         // A declaration reads "<name> : <type>"; anything else is a usage.
         if (text.find(':', at + name.size()) != std::string::npos) {
            return l;
         }
      }
      return -1;
   };

   for (const auto& [key, list] : decls) {
      for (const auto& decl : list) {
         int line = decl.line;
         if (line < 1) {
            if (decl.ownerLine < 1) {
               continue; // no position and no owning METHOD: nothing to point at
            }
            line = findDeclLine(decl.ownerLine, decl.name);
            if (line < 1) {
               continue;
            }
         }
         WorkspaceDeclaration wd;
         wd.file = path;
         wd.line = line;
         wd.scope = decl.scope;
         wd.category = decl.category;
         wd.typeText = decl.typeText;
         wd.kindText = decl.kindText;
         wd.pouName = tu.pous.empty() ? std::string() : tu.pous.front().name;
         outDecls[key].push_back(std::move(wd));
      }
   }
}

/**
 * @brief Build the workspace index on first use
 *
 * Parsing every .st file costs a fraction of a millisecond each, and the result
 * is cached until a file is saved, created, renamed or deleted.
 */
void STApp::ensureWorkspaceIndex()
{
   if (m_workspaceIndexValid) {
      return;
   }
   m_workspaceDecls.clear();
   m_workspaceMethods.clear();

   std::vector<std::string> files;
   std::function<void(const FileNode&)> walk = [&](const FileNode& node) {
      if (node.isDirectory) {
         for (const auto& child : node.children) {
            walk(child);
         }
         return;
      }
      if (fs::path(node.path).extension() == ".st") {
         files.push_back(node.path);
      }
   };
   walk(m_rootNode);

   // Without a loaded workspace the tree is empty, and cross-file navigation
   // would silently find nothing. Fall back to the folder of the open file so
   // it still works when a single .st was opened directly.
   if (files.empty() && !m_currentFilePath.empty()) {
      std::error_code ec;
      const fs::path dir = fs::path(m_currentFilePath).parent_path();
      for (const auto& entry : fs::directory_iterator(dir, ec)) {
         if (entry.is_regular_file() && entry.path().extension() == ".st") {
            files.push_back(entry.path().string());
         }
      }
   }

   for (const auto& path : files) {
      indexSTFile(path, m_workspaceDecls);
   }
   m_workspaceIndexValid = true;
}
/**
 * @brief Build (or reuse) the library registry describing the sibling files
 *
 * Each other .st file in the workspace is parsed, analysed and exported as a
 * semantic-only LibraryDescriptor, then registered. The analyzer looks names up
 * in its external scope before reporting an unknown type, which is what makes a
 * FUNCTION_BLOCK declared in a sibling file usable from the open one. The
 * result is cached and rebuilt only when the workspace index is invalidated.
 */
void STApp::ensureProjectRegistry()
{
   if (m_projectRegistry) {
      return;
   }
   ensureWorkspaceIndex();

   m_projectRegistry = std::make_unique<st2cpp::library::LibraryRegistry>();

   // Reuse the file list built for the declaration index.
   std::vector<std::string> files;
   std::function<void(const FileNode&)> walk = [&](const FileNode& node) {
      if (node.isDirectory) {
         for (const auto& child : node.children) {
            walk(child);
         }
         return;
      }
      if (fs::path(node.path).extension() == ".st") {
         files.push_back(node.path);
      }
   };
   walk(m_rootNode);
   if (files.empty() && !m_currentFilePath.empty()) {
      std::error_code ec;
      for (const auto& entry : fs::directory_iterator(fs::path(m_currentFilePath).parent_path(), ec)) {
         if (entry.is_regular_file() && entry.path().extension() == ".st") {
            files.push_back(entry.path().string());
         }
      }
   }

   for (const auto& path : files) {
      if (path == m_currentFilePath) {
         continue; // the open file is analysed directly, not as a dependency
      }

      std::ifstream file(path);
      if (!file.is_open()) {
         continue;
      }
      std::stringstream buffer;
      buffer << file.rdbuf();

      TranslationUnit tu;
      st2cpp::semantic::SemanticInfo info;
      try {
         Lexer lexer(buffer.str());
         Parser parser(std::move(lexer.tokenize()));
         tu = parser.parseTranslationUnit();
         st2cpp::semantic::SemanticAnalyzer analyzer;
         analyzer.setSourceName(path);
         info = analyzer.analyze(tu, projectStrictness());
      } catch (const std::exception&) {
         continue; // a sibling that does not parse contributes no symbols
      }

      st2cpp::semantic::LibraryExportOptions options;
      options.id = path; // the file path is the library identity
      options.name = tu.pous.empty() ? fs::path(path).stem().string() : tu.pous.front().name;
      options.version = "0.0.0";

      // A descriptor still cannot carry PROGRAM or INTERFACE POUs, and the
      // builder drops them. Everything else it represents: EXTENDS, ABSTRACT,
      // FINAL and methods all convert, so a sibling FUNCTION_BLOCK is exported
      // with its members and its methods and a sibling `inst : ThatBlock`
      // resolves completely.
      TranslationUnit exportable = tu;
      exportable.pous.clear();
      for (const auto& pou : tu.pous) {
         if (pou.kind == POUKind::PROGRAM) {
            continue; // not representable
         }
         exportable.pous.push_back(pou);
      }
      if (exportable.pous.empty() && tu.structs.empty() && tu.enums.empty()) {
         continue;
      }

      const st2cpp::semantic::LibraryExportResult built =
         st2cpp::semantic::LibraryDescriptorBuilder::build(exportable, info, options);

      // Export errors are still expected for the constructs a descriptor
      // cannot carry (PROGRAM, INTERFACE, type aliases). The descriptor is
      // usable for the parts that did convert, so it is registered regardless.
      if (built.descriptor.has_value()) {
         std::string error;
         m_projectRegistry->registerLibrary(*built.descriptor, error);
      }
   }
}

/**
 * @brief Drop the cached index after the workspace changed
 * @param path File that changed; kept for diagnostics when a whole rebuild
 *             turns out to be necessary
 */
void STApp::invalidateWorkspaceIndex(const std::string& path)
{
   (void)path;
   m_workspaceIndexValid = false;
   m_projectRegistry.reset();
   m_workspaceMethods.clear();
}

std::vector<STApp::WorkspaceDeclaration> STApp::findWorkspaceDeclarations(const std::string& name) const
{
   const_cast<STApp*>(this)->ensureWorkspaceIndex();
   auto it = m_workspaceDecls.find(st2cpp::semantic::SymbolTable::normalizeKey(name));
   if (it == m_workspaceDecls.end()) {
      return {};
   }
   return it->second;
}

// ============================================================================
// Go-to-declaration
// ============================================================================

/**
 * @brief Extract the identifier surrounding a position
 * @param editor  Editor to read from
 * @param line    0-based line
 * @param col     0-based column
 * @param outName Receives the identifier, when one is found
 * @return true when the position sits on an identifier
 *
 * Bounds are expanded by hand rather than taken from the selection, so the same
 * code serves both Ctrl+Click and hover.
 */
bool STApp::identifierAt(const TextEditor& editor, int line, int col, std::string& outName)
{
   const std::vector<std::string> lines = editor.GetTextLines();
   if (line < 0 || line >= (int)lines.size()) {
      return false;
   }
   const std::string& text = lines[line];
   if (text.empty()) {
      return false;
   }

   auto isIdentChar = [](char c) {
      return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
   };

   // A position just past the last character still belongs to that word.
   int probe = col;
   if (probe < 0 || probe > (int)text.size()) {
      return false;
   }
   if (probe == (int)text.size() || !isIdentChar(text[probe])) {
      if (probe == 0 || !isIdentChar(text[probe - 1])) {
         return false;
      }
      --probe;
   }

   int start = probe;
   while (start > 0 && isIdentChar(text[start - 1])) {
      --start;
   }
   int end = probe;
   while (end < (int)text.size() && isIdentChar(text[end])) {
      ++end;
   }

   outName = text.substr(start, end - start);
   return !outName.empty();
}

/**
 * @brief Locate a name inside a generated line
 * @param srcLines Generated source lines
 * @param line     1-based line number
 * @param name     Identifier to find
 * @return 0-based column, or -1 when the name is not on that line
 */
int STApp::columnOfName(const std::vector<std::string>& srcLines, int line, const std::string& name)
{
   if (line < 1 || line > (int)srcLines.size() || name.empty()) {
      return -1;
   }
   const std::string& text = srcLines[line - 1];
   const size_t at = text.find(name);
   return (at == std::string::npos) ? -1 : (int)at;
}

/**
 * @brief Pick the declaration a navigation should target
 * @param name  Identifier under the cursor
 * @param scope METHOD the cursor is in, empty for the POU body
 * @return The best matching declaration, or nullptr
 *
 * A METHOD parameter legitimately shadows a POU variable of the same name, so
 * the declaration in the innermost scope wins.
 */
const Declaration* STApp::findDeclaration(const std::string& name, const std::string& scope) const
{
   auto it = m_declarations.find(st2cpp::semantic::SymbolTable::normalizeKey(name));
   if (it == m_declarations.end() || it->second.empty()) {
      return nullptr;
   }
   const std::vector<Declaration>& candidates = it->second;

   if (!scope.empty()) {
      for (const auto& decl : candidates) {
         if (decl.scope == scope) {
            return &decl;
         }
      }
   }
   for (const auto& decl : candidates) {
      if (decl.scope.empty()) {
         return &decl;
      }
   }
   return &candidates.front();
}

/**
 * @brief Show the cursor on a generated-file line, opening its METHOD tab
 * @param fullLine 1-based line in the generated file
 * @param col      0-based column, or -1 to stay at the line start
 */
void STApp::revealGeneratedLine(int fullLine, int col, int tokenLength)
{
   const SourceSegment* seg = segmentForLine(fullLine);
   if (!seg) {
      return; // header or footer: nothing to reveal
   }
   if (!seg->methodName.empty()) {
      // Materialize the tab before switching, otherwise there is no editor.
      auto it = m_methodEditors.find(seg->methodName);
      if (it == m_methodEditors.end()) {
         const int idx = findMethodIndex(seg->methodName);
         if (idx >= 0) {
            getOrCreateMethodEditors(m_methods[idx]);
            it = m_methodEditors.find(seg->methodName);
         }
      }
      if (it != m_methodEditors.end()) {
         requestTabSelection(seg->methodName);
      }
   }

   if (TextEditor* ed = editorForSegment(*seg)) {
      const int editorLine = fullLine - seg->fullStart;
      TextEditor::Coordinates pos;
      pos.mLine = editorLine;
      pos.mColumn = (col < 0) ? 0 : col;
      ed->SetCursorPosition(pos);

      // Selecting the name makes the jump visible: in a split view both panes
      // are on screen at once, so moving the caret alone is easy to miss.
      if (col >= 0 && tokenLength > 0) {
         TextEditor::Coordinates end;
         end.mLine = editorLine;
         end.mColumn = col + tokenLength;
         ed->SetSelection(pos, end, TextEditor::SelectionMode::Normal);
      }
   }
}

/**
 * @brief Handle Ctrl+Click and hover on one editor
 * @param id     Editor id, used only to keep ImGui ids distinct
 * @param editor Editor that was just rendered
 *
 * Must be called right after editor.Render(): TextEditor clears
 * IsTextChanged() at the start of Render(), and Ctrl+Click has already parked
 * the cursor on the clicked glyph by then.
 */
void STApp::handleNavigationInput(const char* id, TextEditor& editor)
{
   (void)id;

   if (!ImGui::IsItemHovered()) {
      return;
   }

   const bool ctrl = ImGui::GetIO().KeyCtrl;
   const bool click = ImGui::IsMouseClicked(0);

   if (click && !ctrl) {
      return; // a plain click is just a cursor move
   }

   if (!click) {
      showIdentifierTooltip(editor);
      return;
   }

   // Ctrl+Click: TextEditor has already moved the cursor onto the glyph.
   const TextEditor::Coordinates cur = editor.GetCursorPosition();
   std::string name;
   if (!identifierAt(editor, cur.mLine, cur.mColumn, name)) {
      return;
   }

   // Work out which METHOD the click happened in, so a shadowing parameter
   // wins over a POU variable of the same name. The cursor is in editor
   // coordinates while the segments describe the generated file, so the line
   // has to be translated before it can be matched.
   std::string scope;
   for (const auto& seg : m_sourceMap) {
      const int generatedLine = cur.mLine + seg.fullStart;
      if (generatedLine >= seg.fullStart && generatedLine <= seg.fullEnd) {
         scope = seg.methodName;
         break;
      }
   }

   if (const Declaration* decl = findDeclaration(name, scope)) {
      revealDeclaration(*decl);
      return;
   }

   // Not declared in this file: look through the workspace.
   ensureWorkspaceIndex();
   const std::vector<WorkspaceDeclaration> hits = findWorkspaceDeclarations(name);
   if (hits.empty()) {
      // Say so instead of doing nothing: a click that appears broken is worse
      // than one that explains itself.
      addOutput(OutSeverity::Warning, "Ctrl+Click: no declaration found for '" + name + "'");
      return;
   }

   const WorkspaceDeclaration* pick = &hits.front();
   for (const auto& hit : hits) {
      if (hit.file == m_currentFilePath) {
         pick = &hit; // prefer the open file when the name is declared twice
         break;
      }
   }

   m_jumpCandidates = hits;
   m_jumpCandidateName = name;
   m_pendingJumpName = name;

   if (pick->file == m_currentFilePath) {
      revealGeneratedLine(pick->line, -1);
      return;
   }

   if (hits.size() > 1) {
      m_showJumpPopup = true; // let the user choose between declarations
      return;
   }
   requestOpenFile(pick->file);
}

// ============================================================================
//  Overlays drawn over the text
//
//  The member list, the signature help and the parameter hint share a look, a
//  lifetime and one rule: the editor keeps the keyboard. They are overlays
//  rather than real popups, the characters that filter the list go to the editor
//  as usual, and only the navigation and confirmation keys are intercepted
//  before the editor would act on them.
//
//  They are drawn on the editor's own draw list, and take their colours from the
//  editor's palette, so a member looks the same in the text and in the list.
// ============================================================================

namespace {

/// Rows the member list shows at once. A longer list scrolls.
constexpr int kCompletionMaxRows = 10;

/// A colour from the editor's palette, so the overlays agree with the text.
ImU32 editorPaletteColor(const TextEditor& editor, TextEditor::PaletteIndex slot)
{
   const TextEditor::Palette& palette = editor.GetPalette();
   return palette[static_cast<int>(slot)];
}

/// @brief Where the identifier ending at the caret begins
/// @return Column of its first character, or `cursorCol` when there is none
static int identifierStartAt(const std::string& line, int cursorCol)
{
   int start = std::min(cursorCol, static_cast<int>(line.size()));
   while (start > 0 && (std::isalnum(static_cast<unsigned char>(line[static_cast<size_t>(start - 1)])) != 0 ||
                        line[static_cast<size_t>(start - 1)] == '_')) {
      --start;
   }
   return start;
}

/// @brief Whether the word starting at `start` is somewhere a name can be completed
///
/// A suggestion list in the middle of a string, a comment or an assignment reads
/// as the editor having lost track of where it is, and none of the three can be
/// completed anyway.
static bool completesIdentifier(const std::string& line, int start)
{
   int i = start - 1;
   // Step over the spaces, to catch both ':=' and '= name' as well as ': name'.
   while (i >= 0 && (line[static_cast<size_t>(i)] == ' ' || line[static_cast<size_t>(i)] == '\t')) {
      --i;
   }
   if (i < 0) {
      return true; // the first word on the line
   }
   const char before = line[static_cast<size_t>(i)];
   // '=' and ':' are where an assignment starts, which is where most names are
   // typed; '(' ',' '[' open a call or an index. A '.' means the member list is
   // the right one, and a quote means this is a literal.
   return before == '(' || before == ',' || before == '[' || before == '=' || before == ':';
}

/// @brief Whether the caret sits inside a comment or a string literal on its line
static bool inCommentOrString(const std::string& line, int cursorCol)
{
   const int end = std::min(cursorCol, static_cast<int>(line.size()));
   bool inString = false;
   bool inLineComment = false;
   for (int i = 0; i < end; ++i) {
      const char c = line[static_cast<size_t>(i)];
      if (inLineComment) {
         continue;
      }
      if (inString) {
         // A doubled quote is an escaped one, so it does not close the literal.
         if (c == '\'') {
            if (i + 1 < end && line[static_cast<size_t>(i + 1)] == '\'') {
               ++i;
            } else {
               inString = false;
            }
         }
         continue;
      }
      if (c == '\'') {
         inString = true;
      } else if (c == '/' && i + 1 < end && line[static_cast<size_t>(i + 1)] == '/') {
         inLineComment = true;
      }
   }
   return inString || inLineComment;
}

/// @brief Whether two names are the same name, IEC spelling
///
/// IEC 61131-3 identifiers are case-insensitive, so 'Motore' and 'motore' are one
/// name, and a word that is already spelled like the candidate it matched has
/// nothing left to complete.
static bool sameName(const std::string& a, const std::string& b)
{
   if (a.size() != b.size()) {
      return false;
   }
   for (size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
         return false;
      }
   }
   return true;
}

/// @brief The names the other files of the workspace declare
///
/// A symbol table only knows the file it was built from, so a function block type
/// declared in a sibling file is invisible to it even though the analyzer resolved
/// it. The registry holds every other .st of the workspace, which is where the
/// name in 'motore : idFB' comes from once the user starts typing it.
///
/// Only the things a POU can name on their own are offered. A method of a sibling
/// block is not one of them: it needs an instance, so it belongs behind a dot.
static MemberList collectProjectNames(const st2cpp::library::LibraryRegistry& registry)
{
   MemberList out;
   auto add = [&](const std::string& name, Suggestion::Kind kind, const std::string& typeText) {
      if (name.empty()) {
         return;
      }
      Suggestion entry;
      entry.name = name;
      entry.kind = kind;
      entry.typeText = typeText;
      out.push_back(std::move(entry));
   };
   for (const st2cpp::library::LibraryDescriptor* descriptor : registry.all()) {
      if (descriptor == nullptr) {
         continue;
      }
      for (const st2cpp::library::FunctionBlockDef& block : descriptor->functionBlocks) {
         add(block.name, Suggestion::Kind::Block, "FUNCTION_BLOCK");
      }
      for (const st2cpp::library::FunctionDef& function : descriptor->functions) {
         add(function.name, Suggestion::Kind::Function, function.returnType.name);
      }
      for (const st2cpp::library::GlobalVariable& global : descriptor->globalVariables) {
         add(global.name, global.constant ? Suggestion::Kind::Constant : Suggestion::Kind::State,
             global.type.name);
      }
      for (const st2cpp::library::Constant& constant : descriptor->constants) {
         add(constant.name, Suggestion::Kind::Constant, constant.type.name);
      }
      for (const st2cpp::library::EnumTypeDef& enumeration : descriptor->enums) {
         add(enumeration.name, Suggestion::Kind::Type, "ENUM");
      }
      for (const st2cpp::library::StructTypeDef& structure : descriptor->types) {
         add(structure.name, Suggestion::Kind::Type, "STRUCT");
      }
   }
   return out;
}

ImU32 kindColor(const TextEditor& editor, const MemberAccess& member)
{
   switch (member.kind) {
   case MemberAccess::Kind::Method:
   case MemberAccess::Kind::Function:
      return editorPaletteColor(editor, TextEditor::PaletteIndex::SemFunction);
   case MemberAccess::Kind::Parameter:
      return editorPaletteColor(editor, TextEditor::PaletteIndex::SemParameter);
   case MemberAccess::Kind::Block:
   case MemberAccess::Kind::Type:
   case MemberAccess::Kind::Enumerator:
      // A block instance, a type and an enumerator are all named things rather
      // than values, so they share the type colour and differ by their label.
      return editorPaletteColor(editor, TextEditor::PaletteIndex::SemType);
   case MemberAccess::Kind::State:
   case MemberAccess::Kind::Constant:
   default:
      return editorPaletteColor(editor, TextEditor::PaletteIndex::SemVariable);
   }
}

/// One-letter tag in front of the name, so a list of mixed kinds is readable
/// without a legend
const char* kindTag(const MemberAccess& member)
{
   switch (member.kind) {
   case MemberAccess::Kind::Parameter:
      return "p";
   case MemberAccess::Kind::Method:
      return "m";
   case MemberAccess::Kind::Function:
      return "f";
   case MemberAccess::Kind::Constant:
      return "c";
   case MemberAccess::Kind::Block:
      return "b";
   case MemberAccess::Kind::Type:
      return "t";
   case MemberAccess::Kind::Enumerator:
      return "e";
   case MemberAccess::Kind::State:
   default:
      return "v";
   }
}

/// The right-hand column of a member row: what it is, and what it takes.
std::string memberDetail(const MemberAccess& member)
{
   std::string detail;
   if (member.kind == MemberAccess::Kind::Method) {
      detail = "(";
      for (size_t i = 0; i < member.parameterTypes.size(); ++i) {
         if (i != 0) {
            detail += ", ";
         }
         detail += member.parameterTypes[i];
      }
      detail += ")";
   }
   if (!member.typeText.empty()) {
      // A parameter carries its direction as well as its type: 'IN : INT' says
      // whether the call has to give it a value or only pass one along.
      if (member.kind == MemberAccess::Kind::Parameter && !member.parameterDirs.empty() &&
          !member.parameterDirs.front().empty()) {
         detail += member.parameterDirs.front();
      }
      detail += " : " + member.typeText;
   }
   return detail;
}

/**
 * @brief Whether two overlay rectangles overlap
 *
 * A shared edge counts, since two boxes that touch still hide one another's border.
 */
static bool overlaysOverlap(const ImRect& a, const ImRect& b)
{
   return a.Min.x < b.Max.x && b.Min.x < a.Max.x && a.Min.y < b.Max.y && b.Min.y < a.Max.y;
}

/**
 * @brief Where an overlay goes relative to the glyph the cursor is on
 * @param anchor      Screen position of the cursor glyph
 * @param editorMin   Top-left of the editor's visible text area
 * @param editorMax   Bottom-right of the editor's visible text area
 * @param height      Height the overlay will take
 * @param width       Width it will take
 * @param preferAbove True to sit above the line when there is room for it
 * @param avoid       Space another overlay is already using, empty when there is none
 *
 * Flips above the line when it would not fit below, and is pulled inside both the
 * editor and the screen, so an overlay never hangs off either.
 *
 * Two overlays anchored to the same cursor want the same place, and a cursor on the
 * first line of an editor has room below but none above, which is where the
 * signature help ends up when it cannot sit above the line. The one arriving second
 * steps around the one already on screen instead of covering it: the other side of
 * the cursor if that is free, otherwise the far side of the first, so the overlay
 * the user is looking at stays next to the text.
 */
ImVec2 overlayPosition(const ImVec2& anchor, const ImVec2& editorMin, const ImVec2& editorMax, float width,
                       float height, bool preferAbove, const ImRect& avoid)
{
   const ImVec2 display = ImGui::GetIO().DisplaySize;
   const float lineHeight = ImGui::GetTextLineHeight();

   const float below = anchor.y + lineHeight;
   const float above = anchor.y - height;
   const float first = preferAbove ? above : below;
   const float second = preferAbove ? below : above;

   const auto boxAt = [&](float y) {
      return ImRect(ImVec2(anchor.x, y), ImVec2(anchor.x + width, y + height));
   };
   const auto inEditor = [&](float y) {
      return y >= editorMin.y - 1.0f && y + height <= editorMax.y + 1.0f;
   };
   const auto clearOf = [&](float y) {
      return avoid.Max.y <= avoid.Min.y || !overlaysOverlap(boxAt(y), avoid);
   };

   float y = first;
   if (!inEditor(y) || !clearOf(y)) {
      y = second;
      if (!inEditor(y) || !clearOf(y)) {
         // Both sides of the cursor are spoken for. Stacking under the other overlay
         // keeps the most recent one, the one being looked at, next to the text;
         // stacking over it is the fallback when there is no room underneath.
         const float stackedBelow = avoid.Max.y;
         const float stackedAbove = avoid.Min.y - height;
         if (avoid.Max.y > avoid.Min.y && inEditor(stackedBelow)) {
            y = stackedBelow;
         } else if (inEditor(stackedAbove)) {
            y = stackedAbove;
         } else {
            y = first;
         }
      }
   }
   y = std::max(0.0f, std::min(y, std::max(0.0f, display.y - height)));

   const float x = std::max(0.0f, std::min(anchor.x, std::min(editorMax.x, display.x) - 4.0f));
   return ImVec2(x, y);
}

/**
 * @brief Move a window that landed on top of another one
 * @param other Space to keep clear of, empty when there is none
 *
 * A window's position is worked out before the window exists, and ImGui then gives
 * it the size its content asks for rather than the size that was asked for, so the
 * box that ends up on screen can be taller than the one the placement was computed
 * against. This is where that difference shows up. Nothing has been drawn into the
 * window yet, so moving it costs nothing, and the draw list, taken after this,
 * lands where the window actually is.
 *
 * The overlay arriving second is the one that gives way: it is the one the user is
 * looking at, and it is the one that just appeared.
 */
static void stepAround(const ImRect& other)
{
   if (other.Max.y <= other.Min.y) {
      return; // nothing to keep clear of
   }
   const ImVec2 pos = ImGui::GetWindowPos();
   const ImVec2 size = ImGui::GetWindowSize();
   const ImRect drawn(pos, ImVec2(pos.x + size.x, pos.y + size.y));
   const bool clearVertically = drawn.Max.y <= other.Min.y || drawn.Min.y >= other.Max.y;
   const bool clearHorizontally = drawn.Max.x <= other.Min.x || drawn.Min.x >= other.Max.x;
   if (clearVertically || clearHorizontally) {
      return;
   }

   const float below = other.Max.y + 1.0f;
   const float above = other.Min.y - size.y - 1.0f;
   const float target = (below + size.y <= ImGui::GetIO().DisplaySize.y) ? below : std::max(0.0f, above);
   if (target != pos.y) {
      ImGui::SetWindowPos(ImVec2(pos.x, target));
   }
}

/**
 * @brief The frame both overlays share: a soft shadow under a hairline border
 *
 * A real popup window draws its shadow from the style; an overlay does not, so
 * two offset fills stand in for one. At this size it is indistinguishable.
 */
void drawOverlayShadow(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, float rounding)
{
   for (int pass = 2; pass >= 1; --pass) {
      const float spread = static_cast<float>(pass) * 2.5f;
      drawList->AddRectFilled(ImVec2(min.x + spread * 0.5f, min.y + spread), ImVec2(max.x + spread, max.y + spread * 2.0f),
                              IM_COL32(0, 0, 0, 70 / pass), rounding + spread);
   }
}

/// The shape that tells the kinds apart, with no dependency on the font carrying
/// a symbol: a filled disc for a value, a ring for a parameter.
void drawKindMarker(ImDrawList* drawList, const ImVec2& centre, ImU32 color, bool hollow)
{
   if (hollow) {
      drawList->AddCircle(centre, 4.0f, color, 0, 1.6f);
   } else {
      drawList->AddCircleFilled(centre, 4.0f, color, 12);
   }
}

/// Width of a run of text, in the font the overlay is drawn with.
float measure(ImFont* font, float fontSize, const std::string& text)
{
   return text.empty() ? 0.0f : font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str()).x;
}

/// One unbreakable piece of a signature, all of it in the same style
struct StyledWord
{
   std::string text;
   bool isParameter = false;
};

/**
 * @brief Break a signature into the words it wraps on
 *
 * A parameter is a whole number of words, so the active parameter is never split
 * across a line: a break through it would paint half the highlight.
 */
std::vector<StyledWord> splitSignatureWords(const std::vector<SignatureSegment>& segments)
{
   std::string flat;
   std::vector<char> flags;
   for (const SignatureSegment& segment : segments) {
      for (char c : segment.text) {
         flat.push_back(c);
         flags.push_back(segment.isParameter ? 1 : 0);
      }
   }

   std::vector<StyledWord> words;
   size_t i = 0;
   while (i < flat.size()) {
      StyledWord word;
      word.isParameter = flags[i] != 0;
      while (i < flat.size() && flat[i] != ' ' && (flags[i] != 0) == word.isParameter) {
         word.text.push_back(flat[i]);
         ++i;
      }
      if (!word.text.empty()) {
         words.push_back(word);
      }
      while (i < flat.size() && flat[i] == ' ') {
         ++i;
      }
   }
   return words;
}

/// A row of the wrapped signature
struct SignatureRow
{
   std::vector<StyledWord> words;
   float width = 0.0f; ///< Measured, so the highlight can be drawn behind a run
};

/**
 * @brief Flow a signature into rows no wider than maxWidth
 */
std::vector<SignatureRow> layoutSignature(const std::vector<SignatureSegment>& segments, float maxWidth, ImFont* font,
                                          float fontSize)
{
   const float spaceWidth = measure(font, fontSize, " ");
   std::vector<SignatureRow> rows;
   SignatureRow row;

   for (const StyledWord& word : splitSignatureWords(segments)) {
      const float width = measure(font, fontSize, word.text);
      const float advance = row.words.empty() ? width : spaceWidth + width;
      if (!row.words.empty() && row.width + advance > maxWidth) {
         rows.push_back(std::move(row));
         row = SignatureRow{};
         row.words.push_back(word);
         row.width = width;
         continue;
      }
      row.width += advance;
      row.words.push_back(word);
   }
   if (!row.words.empty()) {
      rows.push_back(std::move(row));
   }
   if (rows.empty()) {
      rows.push_back(SignatureRow{});
   }
   return rows;
}

/// Where a word sits within a row, so the active parameter can be backed
float xOffsetOf(const SignatureRow& row, size_t wordIndex, ImFont* font, float fontSize)
{
   const float spaceWidth = measure(font, fontSize, " ");
   float x = 0.0f;
   for (size_t i = 0; i < wordIndex && i < row.words.size(); ++i) {
      x += measure(font, fontSize, row.words[i].text) + spaceWidth;
   }
   return x;
}

} // namespace

/**
 * @brief The METHOD whose body a line sits in
 * @param line 0-based line in the generated text
 * @return The METHOD name, or empty for POU-level code
 *
 * The editor shows every METHOD of a FUNCTION_BLOCK on its own tab, and the text
 * under the cursor is the concatenation of the POU and whichever METHOD is open,
 * so which one a line belongs to decides what is in scope there.
 */
std::string STApp::methodScopeAtLine(int line) const
{
   for (const auto& segment : m_sourceMap) {
      const int generatedLine = line + segment.fullStart;
      if (generatedLine >= segment.fullStart && generatedLine <= segment.fullEnd) {
         return segment.methodName;
      }
   }
   return std::string();
}

/**
 * @brief The scope an identifier on a line resolves from
 * @param line 0-based line in the generated text
 * @return Scope id, or 0 when nothing is in scope
 *
 * A METHOD scope is nested inside its POU's, so looking up from it also reaches
 * the POU variables, and a METHOD local correctly shadows one of them.
 */
st2cpp::semantic::ScopeId STApp::scopeIdForLine(int line) const
{
   if (!m_semantic || !m_semantic->symbolTable) {
      return 0;
   }
   const std::string methodScope = methodScopeAtLine(line);
   if (!methodScope.empty()) {
      const st2cpp::semantic::ScopeId scopeId = findMemberScopeFor(*m_semantic->symbolTable, methodScope, true);
      if (scopeId != 0) {
         return scopeId;
      }
   }
   if (!m_pouName.empty()) {
      return findMemberScopeFor(*m_semantic->symbolTable, m_pouName, false);
   }
   return 0;
}

/**
 * @brief Recompute the member completion for the editor that was just rendered
 * @param id                Editor id, matching the one passed to Render
 * @param editor            Editor that was just rendered
 * @param editorHasKeyboard Whether this editor, specifically, is the focused one
 *
 * Called right after Render(), for the same reason as handleNavigationInput: the
 * cursor has already moved onto the glyph under the pointer, and the line the
 * cursor sits on is the one the user is editing.
 *
 * `editorHasKeyboard` is passed in rather than read here. io.WantTextInput is
 * how the editor claims the keyboard, and the editor is muted on every frame
 * where a navigation key was consumed, so the flag reads false on exactly the
 * frames this list has to survive. Reading it once, right after Render() and
 * while the editor is unmuted, is the only moment the two are both true.
 *
 * The suggestion list is a function of the text alone, so it is recomputed from
 * scratch every frame. That keeps it honest when the block, the method list or
 * the file itself changes underneath an open list.
 */
bool STApp::buildParameterPicker(TextEditor& editor, const std::vector<std::string>& lines,
                                  const TextEditor::Coordinates& cursor)
{
   if (cursor.mLine < 0 || static_cast<size_t>(cursor.mLine) >= lines.size()) {
      return false;
   }
   const CallSite call = callSiteAt(lines, cursor.mLine, cursor.mColumn);
   if (!call.active) {
      return false;
   }
   CallSignature signature;
   if (!m_semantic || !m_semantic->symbolTable ||
       !resolveCallSignature(*m_semantic->symbolTable, scopeIdForLine(cursor.mLine), call, signature) ||
       signature.params.empty()) {
      return false;
   }

   // An unnamed parameter has no name to complete to, so a list of nothing but
   // them would be an empty window the user has to dismiss.
   int nameless = 0;
   for (const SignatureParam& param : signature.params) {
      if (param.name.empty()) {
         ++nameless;
      }
   }
   if (nameless == static_cast<int>(signature.params.size())) {
      return false;
   }

   // What the user has written at the caret filters the list, so a name that is
   // half typed narrows the parameters the same way it narrows any other list.
   const std::string& line = lines[static_cast<size_t>(cursor.mLine)];
   const int queryStart = identifierStartAt(line, cursor.mColumn);
   const std::string query =
       line.substr(static_cast<size_t>(queryStart), static_cast<size_t>(cursor.mColumn - queryStart));

   MemberList unused;
   MemberList given;
   for (size_t i = 0; i < signature.params.size(); ++i) {
      const SignatureParam& param = signature.params[i];
      if (param.name.empty()) {
         continue;
      }
      Suggestion entry;
      entry.name = param.name;
      entry.kind = Suggestion::Kind::Parameter;
      entry.typeText = param.type;
      entry.parameterDirs.push_back(param.direction);
      entry.isConstant = param.hasDefault;
      if (static_cast<int>(i) < call.argumentIndex) {
         // Written already: still offered, so a wrong value can be replaced by
         // name, but below the ones this call still owes.
         entry.note = "given";
         given.push_back(std::move(entry));
      } else {
         unused.push_back(std::move(entry));
      }
   }
   {
      MemberList all = std::move(unused);
      all.insert(all.end(), std::make_move_iterator(given.begin()), std::make_move_iterator(given.end()));
      rankMembers(all, query);
      if (all.empty()) {
         return false;
      }

      // A list that is the same question keeps where the user had walked to; the
      // text changing under it is a new question and sends the highlight back to
      // the first parameter still owed.
      const bool newQuery = (m_completionEditor != &editor) || (query != m_completionPrefix);
      if (newQuery) {
         m_completionSelected = 0;
         m_completionScroll = 0;
      }

      m_completionIsParameterList = true;
      m_completionEditor = &editor;
      m_completionEditorMin = editor.GetEditorScreenMin();
      m_completionEditorMax = editor.GetEditorScreenMax();
      m_completionCursorScreen = editor.GetCursorScreenPos();
      m_completionCandidates = std::move(all);
      m_completionPrefix = query;
      m_completionObject = call.label;
      m_completionLine = cursor.mLine;
      m_completionColStart = queryStart;
      m_completionColEnd = cursor.mColumn;
      m_completionDismissedValid = false;
      if (m_completionSelected >= static_cast<int>(m_completionCandidates.size())) {
         m_completionSelected = 0;
      }
   }
   return true;
}

void STApp::updateMemberCompletion(const char* id, TextEditor& editor, bool editorHasKeyboard)
{
   (void)id;
   // Both editors are visited every frame and this runs for each of them, so the
   // geometry is only adopted when this editor is the one holding the list.
   // Writing it unconditionally would leave the list drawn over the body editor
   // even when the completion belongs to the declarations editor.
   auto dismiss = [&]() {
      if (m_completionEditor == &editor) {
         clearMemberCompletion();
      }
   };

   // A list belonging to this editor must not survive the editor losing focus or
   // the cursor leaving the access, or it would sit on screen offering
   // suggestions that no longer apply. Other editors' lists are left alone: both
   // editors are visited every frame, and only one of them owns the list.
   if (!editorHasKeyboard) {
      dismiss();
      return;
   }

   // A statement list answers about the same word this one would, and only one of
   // them can be on screen. What is being typed is a keyword rather than a name,
   // so the statement list is the one that applies; when the word stops being one
   // it closes on its own, and this list is free to answer again.
   if (m_snippetEditor == &editor && !m_snippetCandidates.empty()) {
      return;
   }

   const TextEditor::Coordinates cursor = editor.GetCursorPosition();
   const std::vector<std::string> lines = editor.GetTextLines();
   if (cursor.mLine < 0 || static_cast<size_t>(cursor.mLine) >= lines.size()) {
      dismiss();
      return;
   }

   const std::string& line = lines[static_cast<size_t>(cursor.mLine)];

   // Neither kind of list belongs inside a comment or a string literal, and a name
   // cannot be completed in one anyway. Checked before anything else so the
   // dismissed-spot bookkeeping is not disturbed by a list that was never
   // possible here.
   if (inCommentOrString(line, cursor.mColumn)) {
      dismiss();
      return;
   }

   // The parameters of the call being written are the one thing the list can offer
   // that the text cannot: what the callee is called, written out as names. Tab
   // asks for them, and a request is not a mode, so the list does not survive the
   // caret leaving the call or the call losing its parentheses.
   if (m_completionIsParameterList && m_completionEditor == &editor) {
      if (!buildParameterPicker(editor, lines, cursor)) {
         dismiss();
      }
      return;
   }
   if (m_parameterPickerEditor == &editor) {
      m_parameterPickerEditor = nullptr;
      if (editorHasKeyboard && buildParameterPicker(editor, lines, cursor)) {
         return;
      }
      // No call, or one with nothing to choose from: fall through, so Tab is still
      // Tab and the usual list for the text gets its chance instead.
   }

   // A list has to have something to filter on. After a '.' the prefix is what
   // follows it; without one, the identifier being typed is the prefix, which is
   // what makes a half-typed variable or method name completable.
   const MemberAccessPoint point = memberAccessPointAt(line, cursor.mColumn);
   const int queryStart = point.active ? point.dotColumn + 1 : identifierStartAt(line, cursor.mColumn);
   const bool identifierQuery = !point.active && queryStart < cursor.mColumn && completesIdentifier(line, queryStart);
   if (!point.active && !identifierQuery) {
      dismiss();
      return;
   }

   // Escape closed the list at this exact spot, and it has to stay closed there.
   // The text still reads "inst.", so recomputing from scratch would otherwise
   // put the list straight back on the next frame and Escape would look like it
   // had done nothing at all. Typing shifts the column, which reopens it.
   if (m_completionDismissedValid && m_completionDismissed.line == cursor.mLine &&
       m_completionDismissed.column == cursor.mColumn) {
      dismiss();
      return;
   }

   if (!m_semantic || !m_semantic->symbolTable) {
      dismiss();
      return;
   }

   // Members of the instance after a '.', or the names visible from the caret.
   // The instance has to be one the caret can actually see, which is what
   // findInstanceBlock checks by resolving it from the scope on this line.
   MemberList candidates;
   const std::string query = line.substr(static_cast<size_t>(queryStart),
                                         static_cast<size_t>(cursor.mColumn - queryStart));
   if (point.active) {
      const st2cpp::semantic::ScopeId scopeId = scopeIdForLine(cursor.mLine);
      const st2cpp::semantic::SymbolId fbId =
          (scopeId != 0) ? findInstanceBlock(*m_semantic->symbolTable, scopeId, point.objectName) : 0;
      if (fbId == 0) {
         dismiss();
         return;
      }
      candidates = collectBlockMembers(*m_semantic->symbolTable, fbId);
   } else {
      candidates = collectScopeNames(*m_semantic->symbolTable, scopeIdForLine(cursor.mLine),
                                     m_activeTab.empty());
      if (m_projectRegistry) {
         // The scope knows the file being edited; the registry knows its siblings.
         // Both are in scope for the user, so both are offered, and a name declared
         // in the two places at once is one name.
         std::unordered_set<std::string> taken;
         for (const MemberAccess& candidate : candidates) {
            taken.insert(st2cpp::semantic::SymbolTable::normalizeKey(candidate.name));
         }
         for (MemberAccess& declared : collectProjectNames(*m_projectRegistry)) {
            if (taken.insert(st2cpp::semantic::SymbolTable::normalizeKey(declared.name)).second) {
               candidates.push_back(std::move(declared));
            }
         }
      }
   }
   if (candidates.empty()) {
      dismiss();
      return;
   }

   // Fuzzy, best match first. What does not match is dropped, and every survivor
   // carries the positions that did, which is what the list paints highlighted.
   rankMembers(candidates, query);
   if (candidates.empty()) {
      dismiss();
      return;
   }

   // A word that is already spelled like the only thing it matches has nothing
   // left to complete, and a list holding one item that is already in the buffer
   // is noise. This is also what keeps the list from reappearing over a name the
   // user has just accepted, which would make a second Tab insert it twice.
   if (!point.active && candidates.size() == 1 && sameName(candidates.front().name, query)) {
      dismiss();
      return;
   }

   // A different instance or a longer pattern is a different question, so the
   // highlight goes back to the best match rather than staying wherever the user
   // had walked to, and a list that is no longer the one being asked about must
   // not inherit its scroll position.
   const bool newQuery = (m_completionEditor != &editor) || (query != m_completionPrefix) ||
                         (point.active && (point.objectName != m_completionObject));
   if (newQuery) {
      m_completionSelected = 0;
      m_completionScroll = 0;
   }

   m_completionEditor = &editor;
   m_completionEditorMin = editor.GetEditorScreenMin();
   m_completionEditorMax = editor.GetEditorScreenMax();
   m_completionCursorScreen = editor.GetCursorScreenPos();
   m_completionCandidates = std::move(candidates);
   m_completionPrefix = query;
   m_completionObject = point.objectName;
   m_completionLine = cursor.mLine;
   m_completionColStart = queryStart;
   m_completionColEnd = cursor.mColumn;
   m_completionDismissedValid = false;

   if (m_completionSelected >= static_cast<int>(m_completionCandidates.size())) {
      m_completionSelected = 0;
   }
}

/**
 * @brief Dismiss the member completion
 */
void STApp::clearMemberCompletion()
{
   m_completionIsParameterList = false;
   m_completionEditor = nullptr;
   m_completionCandidates.clear();
   m_completionSelected = 0;
   m_completionScroll = 0;
   m_completionPrefix.clear();
   m_completionObject.clear();
   m_completionLine = -1;
   m_completionColStart = 0;
   m_completionColEnd = 0;
}

/**
 * @brief Close the list, and remember where it was, so it stays closed
 */
void STApp::dismissMemberCompletion()
{
   if (m_completionEditor != nullptr) {
      m_completionDismissed = {m_completionLine, m_completionColEnd};
      m_completionDismissedValid = true;
   }
   clearMemberCompletion();
}

/**
 * @brief Move the highlighted suggestion
 * @param delta -1 for up, +1 for down; wraps around
 */
void STApp::moveMemberCompletion(int delta)
{
   if (m_completionCandidates.empty()) {
      return;
   }
   const int count = static_cast<int>(m_completionCandidates.size());
   m_completionSelected = ((m_completionSelected + delta) % count + count) % count;
}

/**
 * @brief Move the highlight a screenful at a time
 * @param pages -1 up, +1 down
 */
void STApp::moveMemberCompletionByPage(int pages)
{
   if (m_completionCandidates.empty()) {
      return;
   }
   const int count = static_cast<int>(m_completionCandidates.size());
   const int page = std::max(1, m_completionPageSize);
   m_completionSelected = std::max(0, std::min(count - 1, m_completionSelected + pages * page));
}

/**
 * @brief Replace what was typed after the '.' with the highlighted member
 *
 * A method is completed with its parentheses and the cursor between them, since
 * a call is what the user wants next and that is where the signature help opens;
 * a parameter or state member is completed with its name alone.
 */
void STApp::acceptMemberCompletion()
{
   if (!m_completionEditor || m_completionCandidates.empty()) {
      return;
   }
   const MemberAccess& member = m_completionCandidates[static_cast<size_t>(m_completionSelected)];

   std::string insertion = member.name;
   // Anything callable is completed ready to be called, with the caret between the
   // parentheses: a method of the block, or a function. A type name is not called,
   // so brackets there would be a syntax error the user has to undo.
   const bool callable = member.kind == MemberAccess::Kind::Method || member.kind == MemberAccess::Kind::Function;
   if (callable) {
      insertion += "()";
   }

   TextEditor::Coordinates start;
   start.mLine = m_completionLine;
   start.mColumn = m_completionColStart;
   TextEditor::Coordinates end;
   end.mLine = m_completionLine;
   end.mColumn = m_completionColEnd;

   m_completionEditor->SetSelection(start, end);
   // InsertText inserts at the cursor and does not consume the selection, so the
   // prefix the user already typed would survive next to the completion. Delete()
   // is used rather than DeleteSelection() because the latter is private, and it
   // records the removal in the undo history, which InsertText alone would not.
   if (m_completionColEnd > m_completionColStart) {
      m_completionEditor->Delete();
   }
   if (callable) {
      // Between the brackets, not after them, so the next thing typed is the first
      // argument and the signature help opens on the call.
      const TextEditor::Coordinates insideBrackets{m_completionLine,
                                                    m_completionColStart + static_cast<int>(member.name.size()) + 1};
      m_completionEditor->SetSelection(insideBrackets, insideBrackets);
   }
   m_completionEditor->InsertText(insertion);
   clearMemberCompletion();
}

/**
 * @brief Draw the member completion list
 *
 * Under the glyph the cursor is on rather than at the edge of the pane, and drawn
 * as a borderless overlay: the editor keeps the keyboard, so the list never
 * blinks and the text being typed is still the text that filters it.
 */
void STApp::renderMemberCompletion()
{
   m_completionRectMin = ImVec2(0.0f, 0.0f);
   m_completionRectMax = ImVec2(0.0f, 0.0f);
   if (m_completionEditor == nullptr || m_completionCandidates.empty()) {
      return;
   }

   const int count = static_cast<int>(m_completionCandidates.size());
   const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
   const int visible = std::min(count, kCompletionMaxRows);
   m_completionPageSize = std::max(1, visible - 1);

   // Follow the highlight, then keep the window within the list.
   if (m_completionSelected < m_completionScroll) {
      m_completionScroll = m_completionSelected;
   }
   if (m_completionSelected >= m_completionScroll + visible) {
      m_completionScroll = m_completionSelected - visible + 1;
   }
   const int maxScroll = std::max(0, count - visible);
   m_completionScroll = std::max(0, std::min(maxScroll, m_completionScroll));

   // The wheel scrolls the list, as it does in every other suggestion list, but
   // only while the pointer is over it: everywhere else it still scrolls the
   // text underneath.
   if (ImGui::IsWindowHovered()) {
      const float wheel = ImGui::GetIO().MouseWheel;
      if (wheel != 0.0f) {
         m_completionScroll -= static_cast<int>(wheel * 3.0f);
         m_completionScroll = std::max(0, std::min(maxScroll, m_completionScroll));
      }
   }

   const float width = std::min(520.0f, std::max(280.0f, ImGui::GetIO().DisplaySize.x * 0.4f));
   const float height = static_cast<float>(visible) * rowHeight;
   const ImVec2 position = overlayPosition(m_completionCursorScreen, m_completionEditorMin, m_completionEditorMax, width,
                                            height, false, ImRect(m_signatureRectMin, m_signatureRectMax));

   ImGui::SetNextWindowPos(position, ImGuiCond_Always);
   ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
   ImGui::SetNextWindowBgAlpha(0.98f);

   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kOverlayRounding);
   const bool shown = ImGui::Begin("##memberCompletion", nullptr, kMemberCompletionWindowFlags);
   ImGui::PopStyleVar(2);
   if (!shown) {
      ImGui::End();
      return;
   }

   // The signature help is already on screen, and a list that ended up on it
   // moves before anything is drawn into it.
   stepAround(ImRect(m_signatureRectMin, m_signatureRectMax));

   m_completionRectMin = ImGui::GetWindowPos();
   m_completionRectMax = ImVec2(m_completionRectMin.x + ImGui::GetWindowSize().x,
                                m_completionRectMin.y + ImGui::GetWindowSize().y);

   ImDrawList* drawList = ImGui::GetWindowDrawList();
   ImFont* font = ImGui::GetFont();
   const float fontSize = ImGui::GetFontSize();
   // The corner radius the style was pushed with, read from the constant rather
   // than from the style: PopStyleVar() has already run, so ImGui::GetStyle() is
   // back to the app default by now and would round the shadow differently from
   // the window it is meant to sit under.
   const ImVec2 windowMin = ImGui::GetWindowPos();
   const ImVec2 windowSize = ImGui::GetWindowSize();
   const ImVec2 windowMax(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
   const float rounding = kOverlayRounding;
   const ImU32 background = ImGui::GetColorU32(ImGuiCol_PopupBg);
   const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
   const ImU32 selectedFill = ImGui::GetColorU32(ImGuiCol_HeaderActive);
   const ImU32 hoverFill = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
   const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
   const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
   const ImU32 matchColor = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

   drawOverlayShadow(drawList, windowMin, windowMax, rounding);
   drawList->AddRectFilled(windowMin, windowMax, background, rounding);
   drawList->AddRect(windowMin, windowMax, borderColor, rounding, 0, 1.0f);

   const TextEditor& editor = *m_completionEditor;
   for (int row = 0; row < visible; ++row) {
      const int index = m_completionScroll + row;
      const MemberAccess& member = m_completionCandidates[static_cast<size_t>(index)];
      const ImVec2 rowMin(windowMin.x, windowMin.y + static_cast<float>(row) * rowHeight);
      const ImVec2 rowMax(windowMax.x, rowMin.y + rowHeight);

      ImGui::PushID(index);
      ImGui::InvisibleButton("##row", ImVec2(rowMax.x - rowMin.x, rowHeight));
      const bool hovered = ImGui::IsItemHovered();
      const bool clicked = ImGui::IsItemClicked();
      ImGui::PopID();

      const bool selected = (index == m_completionSelected);
      if (selected || hovered) {
         drawList->AddRectFilled(ImVec2(rowMin.x + 1.0f, rowMin.y), ImVec2(rowMax.x - 1.0f, rowMax.y),
                                 selected ? selectedFill : hoverFill, rounding * 0.5f);
      }
      if (clicked) {
         m_completionSelected = index;
         acceptMemberCompletion();
         ImGui::End();
         return;
      }

      // The text sits a little above the row's bottom edge, which is what leaves
      // it optically centred rather than mathematically so.
      const float textY = rowMin.y + (rowHeight - fontSize) * 0.5f;
      drawKindMarker(drawList, ImVec2(rowMin.x + 12.0f, rowMin.y + rowHeight * 0.5f), kindColor(editor, member),
                     member.kind == MemberAccess::Kind::Parameter);
      // A plain identifier list mixes variables, methods, functions and types, and
      // the colour alone does not say which is which. One letter does, and it costs
      // nothing next to the marker that already has the space.
      const char* tag = kindTag(member);
      drawList->AddText(font, fontSize - 2.0f, ImVec2(rowMin.x + 24.0f, textY), dimColor, tag);

      // The name, with the characters the pattern matched painted brighter, so a
      // fuzzy hit shows why it matched instead of only claiming to.
      std::vector<char> mask(member.name.size(), 0);
      for (int position : member.matchPositions) {
         if (position >= 0 && static_cast<size_t>(position) < mask.size()) {
            mask[static_cast<size_t>(position)] = 1;
         }
      }
      const ImU32 nameColor = selected ? matchColor : textColor;
      const ImU32 highlightColor = selected ? textColor : matchColor;

      float x = rowMin.x + 24.0f + measure(font, fontSize - 2.0f, tag) + 4.0f;
      for (size_t i = 0; i < member.name.size();) {
         const bool matched = mask[i] != 0;
         size_t j = i;
         while (j < member.name.size() && (mask[j] != 0) == matched) {
            ++j;
         }
         const std::string piece = member.name.substr(i, j - i);
         drawList->AddText(font, fontSize, ImVec2(x, textY), matched ? highlightColor : nameColor, piece.c_str());
         x += measure(font, fontSize, piece);
         i = j;
      }

      // The right-hand column, dimmed. Dropped rather than clipped when it would
      // collide with the name: a half-written type is worse than none at all.
      const std::string detail = memberDetail(member);
      const float detailWidth = measure(font, fontSize, detail);
      if (!detail.empty() && x + 12.0f + detailWidth < rowMax.x - 8.0f) {
         drawList->AddText(font, fontSize, ImVec2(rowMax.x - 8.0f - detailWidth, textY), dimColor, detail.c_str());
      }

      const std::string note = !member.note.empty() ? member.note
                                                    : (member.inherited ? "from " + member.declaredIn : "");
      if (!note.empty() && x + 10.0f + measure(font, fontSize, note) < rowMax.x - 8.0f - detailWidth) {
         drawList->AddText(font, fontSize, ImVec2(x + 10.0f, textY), dimColor, note.c_str());
      }
   }

   ImGui::End();
}

// ============================================================================
//  Statement completion
//
//  A list of statement skeletons, kept in its own state rather than folded into
//  the member completion: the two can be answering about the same word, and only
//  one of them can be on screen. While a statement list is up it takes the keys
//  first, because what is being typed is a keyword rather than a name.
//
//  The overlay, the key handling and the state all follow the member completion
//  it sits next to, down to the blocked-key bookkeeping that stops a press the
//  list has taken from also reaching the editor.
// ============================================================================

/**
 * @brief Recompute the statement skeletons offered for what is being typed
 * @param id                Editor id, matching the one passed to Render
 * @param editor            Editor that was just rendered
 * @param editorHasKeyboard Whether this editor, specifically, is the focused one
 * @param inVariables       True for the Variables pane
 *
 * The pane decides what is on offer and the text decides the rest. A caret
 * inside a comment, a string or a call offers nothing, and neither does a word
 * that is not the start of a keyword or that was reached through a '.'.
 */
void STApp::updateStatementCompletion(const char* id, TextEditor& editor, bool editorHasKeyboard, bool inVariables)
{
   (void)id;
   // Both editors are visited every frame and this runs for each, so the geometry
   // is adopted only when this editor is the one holding the list.
   auto dismiss = [&]() {
      if (m_snippetEditor == &editor) {
         clearStatementCompletion();
      }
   };

   if (!editorHasKeyboard) {
      dismiss();
      return;
   }

   const TextEditor::Coordinates cursor = editor.GetCursorPosition();
   const std::vector<std::string> lines = editor.GetTextLines();
   if (cursor.mLine < 0 || static_cast<size_t>(cursor.mLine) >= lines.size()) {
      dismiss();
      return;
   }

   const std::string& line = lines[static_cast<size_t>(cursor.mLine)];

   // Checked before anything else, so that a list which was never possible here
   // does not disturb the dismissed-spot bookkeeping.
   if (inCommentOrString(line, cursor.mColumn)) {
      dismiss();
      return;
   }

   // Inside a call the word being written is an argument, not a statement.
   if (m_signatureEditor == &editor) {
      dismiss();
      return;
   }

   int startCol = 0;
   const std::string prefix = statementPrefixAt(line, cursor.mColumn, startCol);
   if (prefix.empty()) {
      dismiss();
      return;
   }

   // A word reached through a '.' is a member, and a keyword is never one. With
   // "re" typed after "motore." REPEAT matches the word, so a list opening here
   // would take the keyboard off the member list that the text is asking about.
   if (memberAccessPointAt(line, cursor.mColumn).active) {
      dismiss();
      return;
   }

   // Only a genuine keyword prefix is worth a list. Anything looser hides an
   // identifier that happens to begin like a statement.
   if (!hasStatementPrefix(prefix)) {
      dismiss();
      return;
   }

   // Escape closed the list at this exact spot and it has to stay closed there.
   // The text still reads "IF", so recomputing would put the list straight back
   // and Escape would look like it had done nothing. Typing shifts the column.
   if (m_snippetDismissedValid && m_snippetDismissed.line == cursor.mLine &&
       m_snippetDismissed.column == cursor.mColumn) {
      dismiss();
      return;
   }

   std::vector<StatementSnippet> candidates =
       snippetsForPane(inVariables ? SnippetPane::Variables : SnippetPane::Body);
   rankSnippets(candidates, prefix);
   if (candidates.empty()) {
      dismiss();
      return;
   }

   // The subsequence walk rankSnippets performed, over the same text, kept so a
   // fuzzy hit can show which characters matched. The row is as long as that
   // text; only its first label.size() entries are painted.
   std::string needle = prefix;
   std::transform(needle.begin(), needle.end(), needle.begin(),
                  [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
   std::vector<std::vector<int>> matchMask;
   matchMask.reserve(candidates.size());
   for (const StatementSnippet& candidate : candidates) {
      std::string haystack = candidate.label;
      haystack += ' ';
      haystack += candidate.insert;
      std::transform(haystack.begin(), haystack.end(), haystack.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      std::vector<int> row(haystack.size(), 0);
      size_t at = 0;
      for (char c : needle) {
         const size_t found = haystack.find(c, at);
         if (found == std::string::npos) {
            break;
         }
         row[found] = 1;
         at = found + 1;
      }
      matchMask.push_back(std::move(row));
   }

   // A different pane or a longer pattern is a different question, so the
   // highlight goes back to the best match and the scroll position does not
   // carry over from a list that is no longer the one being asked about.
   if (m_snippetEditor != &editor || prefix != m_snippetPrefix || inVariables != m_snippetInVariables) {
      m_snippetSelected = 0;
      m_snippetScroll = 0;
   }

   m_snippetEditor = &editor;
   m_snippetEditorMin = editor.GetEditorScreenMin();
   m_snippetEditorMax = editor.GetEditorScreenMax();
   m_snippetCursorScreen = editor.GetCursorScreenPos();
   m_snippetCandidates = std::move(candidates);
   m_snippetMatchMask = std::move(matchMask);
   m_snippetPrefix = prefix;
   m_snippetInVariables = inVariables;
   m_snippetLine = cursor.mLine;
   m_snippetColStart = startCol;
   m_snippetColEnd = cursor.mColumn;
   m_snippetDismissedValid = false;

   if (m_snippetSelected >= static_cast<int>(m_snippetCandidates.size())) {
      m_snippetSelected = 0;
   }
}

/**
 * @brief Close the statement completion
 *
 * The dismissed spot is left alone. It is what dismissStatementCompletion
 * records, and clearing it here would wipe the record in the same breath that
 * sets it.
 */
void STApp::clearStatementCompletion()
{
   m_snippetEditor = nullptr;
   m_snippetCandidates.clear();
   m_snippetMatchMask.clear();
   m_snippetSelected = 0;
   m_snippetScroll = 0;
   m_snippetPrefix.clear();
   m_snippetInVariables = false;
   m_snippetLine = -1;
   m_snippetColStart = 0;
   m_snippetColEnd = 0;
}

/**
 * @brief Close the statement completion, and keep it closed where the caret is
 *
 * Without the recorded spot the next frame reads the same word, finds the same
 * matches and puts the list back.
 */
void STApp::dismissStatementCompletion()
{
   if (m_snippetEditor != nullptr && m_snippetLine >= 0) {
      m_snippetDismissed.line = m_snippetLine;
      m_snippetDismissed.column = m_snippetColEnd;
      m_snippetDismissedValid = true;
   }
   clearStatementCompletion();
}

/**
 * @brief Move the highlight through the list
 * @param delta Rows to move, negative to go up
 */
void STApp::moveStatementCompletion(int delta)
{
   const int count = static_cast<int>(m_snippetCandidates.size());
   if (count == 0) {
      return;
   }
   m_snippetSelected = (m_snippetSelected + delta % count + count) % count;
}

/**
 * @brief Move the highlight by a page
 * @param pages Pages to move, negative to go up
 */
void STApp::moveStatementCompletionByPage(int pages)
{
   moveStatementCompletion(pages * m_completionPageSize);
}

/**
 * @brief Write the selected skeleton out, and put the caret inside it
 *
 * The typed prefix is replaced, since the skeleton carries its own keyword. The
 * insertion goes through the editor's own InsertText, so it lands in the undo
 * history like anything else typed.
 */
void STApp::acceptStatementCompletion()
{
   if (m_snippetEditor == nullptr || m_snippetCandidates.empty()) {
      return;
   }
   const StatementSnippet& snippet = m_snippetCandidates[static_cast<size_t>(m_snippetSelected)];

   const TextEditor::Coordinates cursor = m_snippetEditor->GetCursorPosition();
   const std::vector<std::string> lines = m_snippetEditor->GetTextLines();
   const std::string line =
       (cursor.mLine >= 0 && static_cast<size_t>(cursor.mLine) < lines.size())
           ? lines[static_cast<size_t>(cursor.mLine)]
           : std::string();

   // Whatever indentation the caret line already carries is what the skeleton is
   // written under, so accepting IF inside a loop produces an IF that is itself
   // inside the loop.
   const std::string base = leadingWhitespace(line);

   // The editor measures a tab as it displays it, so the indent step is written
   // as wide as the editor will show it. A file that indents with spaces keeps
   // being indented with spaces.
   const std::string tab = std::string(static_cast<size_t>(std::max(1, m_snippetEditor->GetTabSize())), ' ');

   std::string text;
   int caretLine = 0;
   int caretCol = 0;
   expandSnippet(snippet, base, tab, text, caretLine, caretCol);

   TextEditor::Coordinates start;
   start.mLine = m_snippetLine;
   start.mColumn = m_snippetColStart;
   TextEditor::Coordinates end;
   end.mLine = m_snippetLine;
   end.mColumn = m_snippetColEnd;
   m_snippetEditor->SetSelection(start, end);
   // InsertText inserts at the cursor and does not consume the selection, so the
   // prefix the user already typed would survive next to the skeleton. Delete()
   // is used rather than DeleteSelection() because the latter is private, and it
   // records the removal in the undo history, which InsertText alone would not.
   if (m_snippetColEnd > m_snippetColStart) {
      m_snippetEditor->Delete();
   }
   m_snippetEditor->InsertText(text);

   // Placed by hand: InsertText leaves the caret after the text it wrote, which
   // for a multi-line skeleton is the last line of the construct.
   const TextEditor::Coordinates caret{m_snippetLine + caretLine, caretCol};
   m_snippetEditor->SetCursorPosition(caret);

   clearStatementCompletion();

   // Every span below the insertion now describes a line further down than the
   // one it was measured on. They are dropped rather than moved, and the next
   // Validate paints them again from the text as it now stands.
   invalidateSemanticTokens();
}

/**
 * @brief Draw the statement completion list
 *
 * A borderless overlay anchored to the glyph being typed. The editor keeps the
 * keyboard, so the list filters itself as the word grows.
 */
void STApp::renderStatementCompletion()
{
   m_snippetRectMin = ImVec2(0.0f, 0.0f);
   m_snippetRectMax = ImVec2(0.0f, 0.0f);
   if (m_snippetEditor == nullptr || m_snippetCandidates.empty()) {
      return;
   }

   const int count = static_cast<int>(m_snippetCandidates.size());
   const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
   const int visible = std::min(count, kCompletionMaxRows);

   // Follow the highlight, then keep the window within the list.
   if (m_snippetSelected < m_snippetScroll) {
      m_snippetScroll = m_snippetSelected;
   }
   if (m_snippetSelected >= m_snippetScroll + visible) {
      m_snippetScroll = m_snippetSelected - visible + 1;
   }
   const int maxScroll = std::max(0, count - visible);
   m_snippetScroll = std::max(0, std::min(maxScroll, m_snippetScroll));

   if (ImGui::IsWindowHovered()) {
      const float wheel = ImGui::GetIO().MouseWheel;
      if (wheel != 0.0f) {
         m_snippetScroll -= static_cast<int>(wheel * 3.0f);
         m_snippetScroll = std::max(0, std::min(maxScroll, m_snippetScroll));
      }
   }

   // A label and a dimmed detail need more room than a bare name.
   const float width = std::min(560.0f, std::max(320.0f, ImGui::GetIO().DisplaySize.x * 0.45f));
   const float height = static_cast<float>(visible) * rowHeight;
   const ImVec2 position =
       overlayPosition(m_snippetCursorScreen, m_snippetEditorMin, m_snippetEditorMax, width, height, false,
                       ImRect(m_signatureRectMin, m_signatureRectMax));

   ImGui::SetNextWindowPos(position, ImGuiCond_Always);
   ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
   ImGui::SetNextWindowBgAlpha(0.98f);

   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kOverlayRounding);
   const bool shown = ImGui::Begin("##statementCompletion", nullptr, kMemberCompletionWindowFlags);
   ImGui::PopStyleVar(2);
   if (!shown) {
      ImGui::End();
      return;
   }

   // The signature help is already on screen, and a list that ended up on it
   // moves before anything is drawn into it.
   stepAround(ImRect(m_signatureRectMin, m_signatureRectMax));

   m_snippetRectMin = ImGui::GetWindowPos();
   m_snippetRectMax = ImVec2(m_snippetRectMin.x + ImGui::GetWindowSize().x,
                             m_snippetRectMin.y + ImGui::GetWindowSize().y);

   ImDrawList* drawList = ImGui::GetWindowDrawList();
   ImFont* font = ImGui::GetFont();
   const float fontSize = ImGui::GetFontSize();
   const ImVec2 windowMin = ImGui::GetWindowPos();
   const ImVec2 windowSize = ImGui::GetWindowSize();
   const ImVec2 windowMax(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
   const float rounding = kOverlayRounding;
   const ImU32 background = ImGui::GetColorU32(ImGuiCol_PopupBg);
   const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
   const ImU32 selectedFill = ImGui::GetColorU32(ImGuiCol_HeaderActive);
   const ImU32 hoverFill = ImGui::GetColorU32(ImGuiCol_HeaderHovered);
   const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
   const ImU32 dimColor = ImGui::GetColorU32(ImGuiCol_TextDisabled);
   const ImU32 matchColor = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);

   drawOverlayShadow(drawList, windowMin, windowMax, rounding);
   drawList->AddRectFilled(windowMin, windowMax, background, rounding);
   drawList->AddRect(windowMin, windowMax, borderColor, rounding, 0, 1.0f);

   for (int row = 0; row < visible; ++row) {
      const int index = m_snippetScroll + row;
      const StatementSnippet& snippet = m_snippetCandidates[static_cast<size_t>(index)];
      const ImVec2 rowMin(windowMin.x, windowMin.y + static_cast<float>(row) * rowHeight);
      const ImVec2 rowMax(windowMax.x, rowMin.y + rowHeight);

      ImGui::PushID(index);
      ImGui::InvisibleButton("##row", ImVec2(rowMax.x - rowMin.x, rowHeight));
      const bool hovered = ImGui::IsItemHovered();
      const bool clicked = ImGui::IsItemClicked();
      ImGui::PopID();

      const bool selected = (index == m_snippetSelected);
      if (selected || hovered) {
         drawList->AddRectFilled(ImVec2(rowMin.x + 1.0f, rowMin.y), ImVec2(rowMax.x - 1.0f, rowMax.y),
                                 selected ? selectedFill : hoverFill, rounding * 0.5f);
      }
      if (clicked) {
         m_snippetSelected = index;
         acceptStatementCompletion();
         ImGui::End();
         return;
      }

      const float textY = rowMin.y + (rowHeight - fontSize) * 0.5f;
      const std::vector<int> emptyMask;
      const std::vector<int>& mask =
          (static_cast<size_t>(index) < m_snippetMatchMask.size())
              ? m_snippetMatchMask[static_cast<size_t>(index)]
              : emptyMask;

      // The label, with the characters the pattern matched painted brighter.
      const ImU32 labelColor = selected ? matchColor : textColor;
      const ImU32 highlightColor = selected ? textColor : matchColor;
      float x = rowMin.x + 12.0f;
      for (size_t i = 0; i < snippet.label.size();) {
         const bool matched = i < mask.size() && mask[i] != 0;
         size_t j = i;
         while (j < snippet.label.size() && (j < mask.size() && mask[j] != 0) == matched) {
            ++j;
         }
         const std::string piece = snippet.label.substr(i, j - i);
         drawList->AddText(font, fontSize, ImVec2(x, textY), matched ? highlightColor : labelColor, piece.c_str());
         x += measure(font, fontSize, piece);
         i = j;
      }

      // The right-hand column, dimmed. Dropped rather than clipped when it would
      // collide with the label: a half-written hint is worse than none at all.
      const float detailWidth = measure(font, fontSize - 2.0f, snippet.detail);
      if (!snippet.detail.empty() && x + 12.0f + detailWidth < rowMax.x - 8.0f) {
         drawList->AddText(font, fontSize - 2.0f, ImVec2(rowMax.x - 8.0f - detailWidth, textY), dimColor,
                           snippet.detail.c_str());
      }
   }

   ImGui::End();
}

/**
 * @brief Consume the statement completion keys
 * @return the key taken, so the caller can block it on the owning editor
 *
 * Runs before the member list, because the two can be answering about the same
 * word. Arrows walk, Enter and Tab write the skeleton out, Escape closes.
 */
ImGuiKey STApp::handleStatementCompletionKeys()
{
   if (!statementCompletionOpen()) {
      return ImGuiKey_None;
   }

   if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
      moveStatementCompletion(1);
      return ImGuiKey_DownArrow;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
      moveStatementCompletion(-1);
      return ImGuiKey_UpArrow;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
      moveStatementCompletionByPage(1);
      return ImGuiKey_PageDown;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
      moveStatementCompletionByPage(-1);
      return ImGuiKey_PageUp;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Tab)) {
      acceptStatementCompletion();
      return ImGuiKey_Tab;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
      acceptStatementCompletion();
      return ImGui::IsKeyPressed(ImGuiKey_Enter) ? ImGuiKey_Enter : ImGuiKey_KeypadEnter;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      dismissStatementCompletion();
      return ImGuiKey_Escape;
   }

   return ImGuiKey_None;
}

/**
 * @brief Drop the semantic spans the editors are painting
 *
 * The spans are line and column pairs measured against text that has since
 * moved, so a skeleton leaves every span underneath it pointing at the wrong
 * glyph. A recolorize is cheaper than a variable painted on a keyword.
 */
void STApp::invalidateSemanticTokens()
{
   std::vector<TextEditor*> editors = {m_variablesEditor.get(), m_bodyEditor.get()};
   for (auto& [name, pair] : m_methodEditors) {
      editors.push_back(pair.variables.get());
      editors.push_back(pair.body.get());
   }
   for (auto* ed : editors) {
      if (ed) {
         ed->ClearSemanticTokens();
         ed->Recolorize();
      }
   }
}

// ============================================================================
//  Signature help
// ============================================================================

/**
 * @brief Recompute the signature of the call the cursor is inside
 * @param id                Editor id, matching the one passed to Render
 * @param editor            Editor that was just rendered
 * @param editorHasKeyboard Whether this editor, specifically, is the focused one
 *
 * Recomputed from the text every frame, for the same reason the member list is:
 * the cursor moves between arguments without any key being pressed, and the
 * parameter being written has to follow it.
 */
void STApp::updateSignatureHelp(const char* id, TextEditor& editor, bool editorHasKeyboard)
{
   (void)id;
   auto dismiss = [&]() {
      if (m_signatureEditor == &editor) {
         clearSignatureHelp();
      }
   };

   if (!editorHasKeyboard) {
      dismiss();
      return;
   }

   const TextEditor::Coordinates cursor = editor.GetCursorPosition();
   const std::vector<std::string> lines = editor.GetTextLines();
   if (cursor.mLine < 0 || static_cast<size_t>(cursor.mLine) >= lines.size()) {
      dismiss();
      return;
   }

   const CallSite call = callSiteAt(lines, cursor.mLine, cursor.mColumn);
   if (!call.active) {
      dismiss();
      return;
   }
   // Escape closed this call, and it has to stay closed there: the text still
   // reads "inst.Method(", so recomputing would put it straight back. The spot
   // has to be compared against the remembered one, not against m_signatureCall,
   // which the dismissal has already reset to empty.
   if (m_signatureDismissedValid && m_signatureDismissed.line == call.parenLine &&
       m_signatureDismissed.column == call.parenColumn) {
      dismiss();
      return;
   }

   const st2cpp::semantic::ScopeId scopeId = scopeIdForLine(cursor.mLine);
   CallSignature signature;
   if (!m_semantic || !m_semantic->symbolTable ||
       !resolveCallSignature(*m_semantic->symbolTable, scopeId, call, signature)) {
      dismiss();
      return;
   }

   // The inline hint is only worth drawing while the argument is still blank, so
   // it never sits on top of what has been written. Only the text between the
   // '(' and the cursor is inspected: the same line's own contents decide it.
   // Whether the argument being written is still blank, which is what decides
   // whether the inline hint is drawn. It is measured from where this argument
   // starts, not from the '(': the second argument of "SetSpeed(1, " is blank
   // even though the first one is not, and that is exactly when the hint is
   // wanted. The scan crosses lines, so a call the user has wrapped still works.
   bool argumentEmpty = true;
   for (int l = call.argumentLine; l <= cursor.mLine && argumentEmpty; ++l) {
      const std::string& text = lines[static_cast<size_t>(l)];
      const int from = (l == call.argumentLine) ? call.argumentColumn : 0;
      const int to = (l == cursor.mLine) ? cursor.mColumn : static_cast<int>(text.size());
      for (int c = from; c < to && c < static_cast<int>(text.size()); ++c) {
         if (text[static_cast<size_t>(c)] != ' ' && text[static_cast<size_t>(c)] != '\t') {
            argumentEmpty = false;
            break;
         }
      }
   }

   m_signatureEditor = &editor;
   m_signatureEditorMin = editor.GetEditorScreenMin();
   m_signatureEditorMax = editor.GetEditorScreenMax();
   m_signatureCursorScreen = editor.GetCursorScreenPos();
   m_signatureCall = call;
   m_signature = std::move(signature);
   m_signatureArgument = call.argumentIndex;
   m_signatureArgumentEmpty = argumentEmpty;
   m_signatureDismissedValid = false;
}

/**
 * @brief Dismiss the signature help
 */
void STApp::clearSignatureHelp()
{
   m_signatureEditor = nullptr;
   m_signature = CallSignature{};
   m_signatureCall = CallSite{};
   m_signatureArgument = 0;
   m_signatureArgumentEmpty = false;
}

/**
 * @brief Close the signature, and remember which call it was for
 */
void STApp::dismissSignatureHelp()
{
   if (m_signatureEditor != nullptr) {
      m_signatureDismissed = {m_signatureCall.parenLine, m_signatureCall.parenColumn};
      m_signatureDismissedValid = true;
   }
   clearSignatureHelp();
}

/**
 * @brief Draw the parameter list of the call the cursor is inside
 *
 * Above the line when there is room, since a panel over the argument being
 * written would hide the very text the parameters are being compared against,
 * and the argument in hand is painted so the eye finds it without counting
 * commas.
 */
void STApp::renderSignatureHelp()
{
   // Whatever was on screen last frame is gone; the list that draws after this one
   // keeps clear of whatever ends up here instead.
   m_signatureRectMin = ImVec2(0.0f, 0.0f);
   m_signatureRectMax = ImVec2(0.0f, 0.0f);
   if (m_signatureEditor == nullptr) {
      return;
   }

   // While the parameter list is open, the signature is what the list is offering:
   // the same names, with their types and their directions, in a list the user is
   // picking from. Two overlays anchored to one cursor are answering one question
   // between them, and the one just asked for is the one that wins. Nothing behind
   // this is touched, so the signature is back the moment the list closes, and the
   // inline hint with it.
   if (m_completionIsParameterList) {
      return;
   }

   ImFont* font = ImGui::GetFont();
   const float fontSize = ImGui::GetFontSize();
   const float lineHeight = ImGui::GetTextLineHeightWithSpacing();

   const std::vector<SignatureSegment> segments = signatureSegments(m_signature, m_signatureArgument);
   const float width = std::min(ImGui::GetIO().DisplaySize.x * 0.8f,
                                std::max(320.0f, ImGui::GetIO().DisplaySize.x * 0.5f));
   const std::vector<SignatureRow> rows = layoutSignature(segments, width - 24.0f, font, fontSize);
   const float height = static_cast<float>(rows.size()) * lineHeight + 10.0f;

   const ImVec2 position = overlayPosition(m_signatureCursorScreen, m_signatureEditorMin, m_signatureEditorMax, width,
                                            height, true, ImRect());

   ImGui::SetNextWindowPos(position, ImGuiCond_Always);
   ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
   ImGui::SetNextWindowBgAlpha(0.98f);

   ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 5.0f));
   ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kOverlayRounding);
   const bool shown = ImGui::Begin("##signatureHelp", nullptr, kSignatureHelpWindowFlags);
   ImGui::PopStyleVar(2);
   if (!shown) {
      m_signatureRectMin = ImVec2(0.0f, 0.0f);
      m_signatureRectMax = ImVec2(0.0f, 0.0f);
      ImGui::End();
      return;
   }

   // The box as drawn rather than the one asked for, since a list anchored to the
   // same cursor has to keep clear of what is really on screen.
   m_signatureRectMin = ImGui::GetWindowPos();
   m_signatureRectMax = ImVec2(m_signatureRectMin.x + ImGui::GetWindowSize().x,
                               m_signatureRectMin.y + ImGui::GetWindowSize().y);

   ImDrawList* drawList = ImGui::GetWindowDrawList();
   // The corner radius the style was pushed with, read from the constant rather
   // than from the style: PopStyleVar() has already run, so ImGui::GetStyle() is
   // back to the app default by now and would round the shadow differently from
   // the window it is meant to sit under.
   const ImVec2 windowMin = ImGui::GetWindowPos();
   const ImVec2 windowSize = ImGui::GetWindowSize();
   const ImVec2 windowMax(windowMin.x + windowSize.x, windowMin.y + windowSize.y);
   const float rounding = kOverlayRounding;
   drawOverlayShadow(drawList, windowMin, windowMax, rounding);
   drawList->AddRectFilled(windowMin, windowMax, ImGui::GetColorU32(ImGuiCol_PopupBg), rounding);
   drawList->AddRect(windowMin, windowMax, ImGui::GetColorU32(ImGuiCol_Border), rounding, 0, 1.0f);

   const ImU32 textColor = ImGui::GetColorU32(ImGuiCol_Text);
   const ImU32 activeColor = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
   const ImU32 activeFill = ImGui::GetColorU32(ImGuiCol_HeaderActive);
   const float spaceWidth = measure(font, fontSize, " ");

   for (size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
      const SignatureRow& row = rows[rowIndex];
      const float rowTop = windowMin.y + 5.0f + static_cast<float>(rowIndex) * lineHeight;

      // The active parameter is backed as well as recoloured, so it reads at a
      // glance without relying on the colour alone.
      for (size_t word = 0; word < row.words.size(); ++word) {
         if (!row.words[word].isParameter) {
            continue;
         }
         const float left = windowMin.x + 10.0f + xOffsetOf(row, word, font, fontSize);
         const float right = left + measure(font, fontSize, row.words[word].text);
         drawList->AddRectFilled(ImVec2(left - 2.0f, rowTop - 1.0f), ImVec2(right + 2.0f, rowTop + fontSize + 1.0f),
                                 activeFill, 3.0f);
      }

      float x = windowMin.x + 10.0f;
      for (size_t word = 0; word < row.words.size(); ++word) {
         const StyledWord& styled = row.words[word];
         drawList->AddText(font, fontSize, ImVec2(x, rowTop), styled.isParameter ? activeColor : textColor,
                           styled.text.c_str());
         x += measure(font, fontSize, styled.text) + spaceWidth;
      }
   }

   ImGui::End();
}

/**
 * @brief Name the argument the cursor is in, in place, right after it
 *
 * Drawn on the editor's own draw list, which is where a hint has to be to sit
 * among the glyphs: the editor's child has already ended, and the parent's draw
 * list carries on past it. It stops as soon as anything is typed, which is what
 * makes it a hint rather than text in the way.
 */
void STApp::renderParameterNameHint(const char* id, TextEditor& editor)
{
   (void)id;
   if (m_signatureEditor != &editor || !m_signatureArgumentEmpty) {
      return;
   }
   if (m_signatureArgument < 0 || m_signatureArgument >= static_cast<int>(m_signature.params.size())) {
      return;
   }

   const SignatureParam& param = m_signature.params[static_cast<size_t>(m_signatureArgument)];
   std::string hint = param.name;
   if (!param.type.empty()) {
      hint += param.name.empty() ? param.type : " : " + param.type;
   }
   if (hint.empty()) {
      return;
   }

   ImDrawList* drawList = ImGui::GetWindowDrawList();
   drawList->AddText(ImGui::GetFont(), ImGui::GetFontSize(), editor.GetCursorScreenPos(),
                     ImGui::GetColorU32(ImGuiCol_TextDisabled), hint.c_str());
}

/**
 * @brief Reveal a name that was queued before a file switch
 *
 * Called at the end of openFile, once the new file has been parsed and its
 * declaration index rebuilt.
 */
void STApp::applyPendingJump()
{
   if (m_pendingJumpName.empty()) {
      return;
   }
   const std::string name = m_pendingJumpName;
   m_pendingJumpName.clear();

   // The cursor is in the POU tab after a file switch; a METHOD target still
   // needs its own tab, so try the POU scope first and then any scope.
   if (const Declaration* decl = findDeclaration(name, std::string())) {
      revealDeclaration(*decl);
      return;
   }
   auto it = m_declarations.find(st2cpp::semantic::SymbolTable::normalizeKey(name));
   if (it == m_declarations.end() || it->second.empty()) {
      return;
   }
   revealDeclaration(it->second.front());
}

/**
 * @brief Test whether a word is an ST keyword
 * @param word Identifier to test
 * @return true for VAR, END_VAR, IF, THEN, ...
 *
 * Keywords never carry a resolved symbol, so hovering one must stay silent
 * instead of reporting it as an unresolved identifier.
 */
static bool isSTKeyword(const std::string& word)
{
   static const TextEditor::LanguageDefinition lang = CreateSTLanguageDefinition();
   std::string upper = word;
   for (char& c : upper) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
   }
   return lang.mKeywords.count(upper) != 0;
}

/**
 * @brief Show the resolved declaration of the identifier under the mouse
 * @param editor Editor that was just rendered
 *
 * Silent unless the feature is enabled and a real declaration was found: a
 * tooltip that pops up on every keyword is noise, not help.
 */
void STApp::showIdentifierTooltip(TextEditor& editor)
{
   if (!m_showTooltips) {
      return;
   }

   const TextEditor::Coordinates at = editor.ScreenToCoordinates(ImGui::GetMousePos());
   std::string name;
   if (!identifierAt(editor, at.mLine, at.mColumn, name)) {
      return;
   }
   if (isSTKeyword(name)) {
      return; // VAR, END_VAR, THEN, ... are not symbols
   }

   std::string scope;
   for (const auto& seg : m_sourceMap) {
      const int generatedLine = at.mLine + seg.fullStart;
      if (generatedLine >= seg.fullStart && generatedLine <= seg.fullEnd) {
         scope = seg.methodName;
         break;
      }
   }

   ImGui::BeginTooltip();

   if (const Declaration* decl = findDeclaration(name, scope)) {
      ImGui::Text("%s", name.c_str());
      ImGui::Separator();
      if (!decl->scope.empty()) {
         ImGui::Text("%s %s", decl->kindText.c_str(), decl->scope.c_str());
      } else {
         ImGui::Text("%s", decl->kindText.c_str());
      }
      if (!decl->typeText.empty() && decl->typeText != name) {
         ImGui::TextDisabled("  : %s", decl->typeText.c_str());
      }
   } else {
      // Not in this file: show where it is declared, if we know.
      const std::vector<WorkspaceDeclaration> hits = findWorkspaceDeclarations(name);
      if (hits.empty()) {
         ImGui::EndTooltip();
         return; // unresolved and unknown: say nothing rather than nagging
      }
      ImGui::Text("%s", name.c_str());
      ImGui::Separator();
      for (const auto& hit : hits) {
         const std::string file = fs::path(hit.file).filename().string();
         if (hit.scope.empty()) {
            ImGui::Text("%s  %s", hit.kindText.c_str(), file.c_str());
         } else {
            ImGui::Text("%s %s  %s", hit.kindText.c_str(), hit.scope.c_str(), file.c_str());
         }
         if (!hit.typeText.empty() && hit.typeText != name) {
            ImGui::TextDisabled("  : %s", hit.typeText.c_str());
         }
      }
   }

   ImGui::TextDisabled("Ctrl+Click to go to declaration");
   ImGui::EndTooltip();
}

/**
 * @brief Switch to a METHOD tab
 * @param methodName Name of the METHOD
 * @return true when the tab exists and is now active
 *
 * A METHOD name lives on a generated header line that no editor owns, so it has
 * no cursor position to move: switching tabs is the whole navigation.
 */
bool STApp::revealMethodTab(const std::string& methodName)
{
   if (methodName.empty()) {
      return false;
   }
   const int idx = findMethodIndex(methodName);
   if (idx < 0) {
      return false;
   }
   getOrCreateMethodEditors(m_methods[idx]);
   requestTabSelection(methodName);
   return true;
}

/**
 * @brief Ask the tab bar to show a tab on the next frame
 * @param tabName METHOD name, or empty for the POU tab
 *
 * ImGui owns tab selection, so a switch has to be requested through
 * ImGuiTabItemFlags_SetSelected: assigning the active-tab name alone updates
 * only our own bookkeeping and leaves the bar showing the previous tab.
 */
void STApp::requestAddMethodDialog()
{
   m_addMethodDialogPending = true;
}

bool STApp::consumeAddMethodDialogRequest()
{
   if (!m_addMethodDialogPending) {
      return false;
   }
   m_addMethodDialogPending = false;
   m_newMethodName.clear();
   m_newMethodReturnType.clear();
   return true;
}

void STApp::requestTabSelection(const std::string& tabName)
{
   m_activeTab = tabName;
   if (tabName.empty()) {
      m_pendingSelectPOU = true;
      m_pendingTabSelection.clear();
   } else {
      m_pendingTabSelection = tabName;
      m_pendingSelectPOU = false;
   }
}

/**
 * @brief Reveal a declaration, whichever form it takes
 * @param decl Target declaration
 * @return true when something was revealed
 *
 * Handles the three shapes a target can have: a normal line owned by an editor,
 * a METHOD header that only exists as a tab, and a METHOD parameter whose line
 * the parser does not report.
 */
bool STApp::revealDeclaration(const Declaration& decl)
{
   if (decl.kindText == "METHOD" && decl.scope == decl.name) {
      return revealMethodTab(decl.scope);
   }

   if (decl.line < 1) {
      // METHOD parameter: scan the method's own lines for its declaration.
      if (decl.ownerLine < 1 || decl.name.empty()) {
         return false;
      }
      for (int l = decl.ownerLine; l <= (int)m_srcLines.size() && l < decl.ownerLine + 40; ++l) {
         const SourceSegment* seg = segmentForLine(l);
         if (!seg || seg->methodName != decl.scope) {
            continue;
         }
         const int col = columnOfName(m_srcLines, l, decl.name);
         if (col < 0) {
            continue;
         }
         // A declaration reads "<name> : <type>"; a usage does not.
         if (m_srcLines[l - 1].find(':', col + (int)decl.name.size()) == std::string::npos) {
            continue;
         }
         revealGeneratedLine(l, col, (int)decl.name.size());
         return true;
      }
      return false;
   }

   int col = decl.col;
   if (col < 0) {
      // The parser reports the keyword, not the identifier.
      col = columnOfName(m_srcLines, decl.line, decl.name.empty() ? std::string() : decl.name);
   }
   const int len = decl.name.empty() ? 0 : (int)decl.name.size();
   revealGeneratedLine(decl.line, col, len);
   return segmentForLine(decl.line) != nullptr;
}

void STApp::addOutput(OutSeverity severity, std::string text){
   OutputLine line;
   line.severity = severity;
   line.text = std::move(text);
   m_outputLines.push_back(std::move(line));
}

/**
 * @brief Set error markers on the appropriate editors based on m_errors
 *
 * Clears existing markers and sets new ones on the Variables and Body editors.
 * Errors that cannot be mapped (header/footer) are shown as a marker on the
 * first line of the Variables editor as a hint.
 */
void STApp::setErrorMarkers()
{
   // Clear existing markers on every editor, method tabs included.
   std::vector<TextEditor*> editors = {m_variablesEditor.get(), m_bodyEditor.get()};
   for (auto& [name, pair] : m_methodEditors) {
      editors.push_back(pair.variables.get());
      editors.push_back(pair.body.get());
   }
   for (auto* ed : editors) {
      if (ed) {
         ed->SetErrorMarkers(TextEditor::ErrorMarkers());
      }
   }

   // Group errors by the editor that owns the offending line, so that several
   // errors on one line accumulate instead of overwriting each other.
   std::map<TextEditor*, TextEditor::ErrorMarkers> markers;

   for (const auto& err : m_errors) {
      const SourceSegment* seg = segmentForLine(err.line);
      if (!seg) {
         // Header or footer: no editor owns it, so surface a hint instead.
         if (markers[m_variablesEditor.get()].find(1) == markers[m_variablesEditor.get()].end()) {
            markers[m_variablesEditor.get()][1] = "Error in file header or footer (see Output panel)";
         }
         continue;
      }

      TextEditor* ed = editorForSegment(*seg);
      if (!ed) {
         continue; // method tab not open
      }
      markers[ed][err.line - seg->fullStart + 1] = err.what();
   }

   for (auto& [ed, map] : markers) {
      if (ed && !map.empty()) {
         ed->SetErrorMarkers(map);
      }
   }
}

// ============================================================================
// Semantic highlighting (st2cpp analyzer -> TextEditor semantic tokens)
// ============================================================================

/**
 * @brief Map a symbol category to the palette slot that paints it
 * @param category Category produced by collectSemanticTokens
 * @return Palette index
 */
static TextEditor::PaletteIndex paletteForCategory(SymCategory category)
{
   switch (category) {
   case SymCategory::Variable:
      return TextEditor::PaletteIndex::SemVariable;
   case SymCategory::Constant:
      return TextEditor::PaletteIndex::SemConstant;
   case SymCategory::Parameter:
      return TextEditor::PaletteIndex::SemParameter;
   case SymCategory::Function:
      return TextEditor::PaletteIndex::SemFunction;
   case SymCategory::Type:
      return TextEditor::PaletteIndex::SemType;
   case SymCategory::Field:
      return TextEditor::PaletteIndex::SemField;
   case SymCategory::Enumerator:
      return TextEditor::PaletteIndex::SemEnumerator;
   case SymCategory::Unresolved:
      break;
   }
   return TextEditor::PaletteIndex::Default;
}

/**
 * @brief Re-run the st2cpp analyzer and paint resolved symbols in the editors
 *
 * Variables, constants, parameters, functions, types, instances and
 * enumerators each get their own color, so they are told apart at a glance
 * instead of all looking like plain identifiers. The spans are handed to the
 * TextEditor, which re-applies them on every colorization pass, so they
 * survive typing.
 */
void STApp::refreshSemanticHighlighting()
{
   std::vector<TextEditor*> editors = {m_variablesEditor.get(), m_bodyEditor.get()};
   for (auto& [name, pair] : m_methodEditors) {
      editors.push_back(pair.variables.get());
      editors.push_back(pair.body.get());
   }

   // Start from a clean slate: the previous run described a different text.
   for (auto* ed : editors) {
      if (ed) {
         ed->ClearSemanticTokens();
      }
   }

   if (!m_ast || !m_semantic || !m_semantic->symbolTable) {
      return;
   }

   // Reuse the analysis cached by validateAndParse: the AST has not changed
   // since, so re-running the analyzer here would only cost time.
   const SemanticTokenSet tokens = collectSemanticTokens(*m_ast, *m_semantic->symbolTable, m_srcLines);

   // Translate generated-file coordinates into the editor that owns the line.
   for (const auto& tok : tokens.tokens) {
      const SourceSegment* seg = segmentForLine(tok.line);
      if (!seg) {
         continue; // header or footer: no editor owns it
      }
      TextEditor* ed = editorForSegment(*seg);
      if (!ed) {
         continue; // method tab not open
      }
      ed->SetSemanticToken(tok.line - seg->fullStart,
                           tok.col,
                           tok.col + tok.length,
                           paletteForCategory(tok.category));
   }

   // Force a repaint so the new spans become visible immediately.
   for (auto* ed : editors) {
      if (ed) {
         ed->Recolorize();
      }
   }
}

/**
 * @brief Validate and parse the current ST code
 *
 * Generates the full ST source from the two editor sections, runs it through
 * the st2cpp lexer and parser, and stores the resulting AST and any errors.
 * Performs semantic analysis on the AST to detect undeclared identifiers.
 * The generated file content is displayed in the Output panel for debugging.
 * Error markers are placed on the appropriate editors.
 */
void STApp::validateAndParse()
{
   m_errors.clear();
   // The previous program is kept aside rather than dropped. A half-typed line is
   // not valid ST, so the parse below is expected to fail while a name is being
   // written, and the declarations in scope have not changed in the meantime.
   // Throwing the model away with the failed parse would leave the completion with
   // nothing to offer at exactly the moment it is being used, so a failed pass
   // puts the last one that worked back.
   std::unique_ptr<TranslationUnit> lastAst = std::move(m_ast);
   std::unique_ptr<st2cpp::semantic::SemanticInfo> lastSemantic = std::move(m_semantic);
   DeclarationIndex lastDeclarations = std::move(m_declarations);
   m_ast.reset();
   m_semantic.reset();
   m_declarations.clear();
   m_outputLines.clear();

   // One pass produces both the text and the map from generated-file lines
   // back to the editor that owns it.
   const std::string fullSource = generateSTFileWithMap(m_sourceMap);

   m_srcLines.clear();
   {
      std::istringstream stream(fullSource);
      std::string l;
      while (std::getline(stream, l)) {
         m_srcLines.push_back(l);
      }
   }


   // Debug: display the complete generated file
   addOutput(OutSeverity::Info, "// Generated ST file:");
   addOutput(OutSeverity::Info, "// --- START ---");
   for (const auto& l : m_srcLines) {
      addOutput(OutSeverity::Info, "// " + l);
   }
   addOutput(OutSeverity::Info, "// --- END ---");
   addOutput(OutSeverity::Info, "");

   // --- Additional syntax check for missing semicolons ---
   checkMissingSemicolons(fullSource, m_errors);
   if (!m_errors.empty()) {
      addOutput(OutSeverity::Error, "Syntax errors found:");
      for (const auto& err : m_errors) {
         addOutput(OutSeverity::Error, std::string("  ") + err.what());
      }
   }

   try {
      Lexer lexer(fullSource);
      auto tokens = lexer.tokenize();
      Parser parser(std::move(tokens));
      auto tu = parser.parseTranslationUnit();
      m_ast = std::make_unique<TranslationUnit>(std::move(tu));
      addOutput(OutSeverity::Success, "Parsing successful: " + std::to_string(m_ast->pous.size()) + " POU(s) found");

      // One analysis pass feeds diagnostics, semantic colors and the
      // declaration index: running the analyzer three times per keystroke
      // would be wasteful for no benefit. The project registry is built first
      // (and cached) so sibling declarations are visible to the analyzer.
      ensureProjectRegistry();

      st2cpp::semantic::SemanticAnalyzer analyzer;
      analyzer.setSourceName(m_currentFilePath);
      const auto strictness = projectStrictness();
      if (m_projectRegistry && !m_projectRegistry->all().empty()) {
         // Sibling files are visible, so a FUNCTION_BLOCK or type declared
         // elsewhere in the project resolves instead of being reported unknown.
         m_semantic = std::make_unique<st2cpp::semantic::SemanticInfo>(analyzer.analyze(*m_ast, *m_projectRegistry, strictness));
      } else {
         m_semantic = std::make_unique<st2cpp::semantic::SemanticInfo>(analyzer.analyze(*m_ast, strictness));
      }

      const SemanticReport report = reportFromDiagnostics(*m_semantic, m_currentFilePath);
      m_errors.insert(m_errors.end(), report.errors.begin(), report.errors.end());

      m_declarations = collectDeclarations(*m_ast, *m_semantic->symbolTable);

      if (!report.errors.empty()) {
         addOutput(OutSeverity::Error, "Semantic errors found:");
         for (const auto& err : report.errors) {
            addOutput(OutSeverity::Error, "  line " + std::to_string(err.line) + ": " + err.message);
         }
      }
      if (!report.notes.empty()) {
         addOutput(OutSeverity::Warning, "Semantic warnings:");
         for (const auto& note : report.notes) {
            addOutput(OutSeverity::Warning, "  " + note);
         }
      }

      // Show body statement count for debugging
      if (m_ast && !m_ast->pous.empty()) {
         const auto& pou = m_ast->pous[0];
         addOutput(OutSeverity::Info, "  Body statements: " + std::to_string(pou.body.size()));
         if (pou.body.empty()) {
            addOutput(OutSeverity::Warning, "No body statements found");
         }
      }
   } catch (const ParseError& e) {
      m_errors.push_back(e);
      m_ast = std::move(lastAst);
      m_semantic = std::move(lastSemantic);
      m_declarations = std::move(lastDeclarations);

      // Map the error line to the editor and section that owns it
      std::string section;
      if (const SourceSegment* seg = segmentForLine(e.line)) {
         section = seg->methodName.empty() ? (seg->isVariables ? "Variables" : "Body")
                                          : ("METHOD " + seg->methodName + " " + (seg->isVariables ? "Variables" : "Body"));
         std::ostringstream oss;
         oss << "Error in " << section << " section at line " << (e.line - seg->fullStart + 1) << ":" << e.col << " - " << e.what();
         addOutput(OutSeverity::Info, oss.str());
         std::cerr << "[undoApp.ST] " << oss.str() << std::endl;
      } else {
         std::ostringstream oss;
         oss << "Error at line " << e.line << ":" << e.col << " - " << e.what();
         addOutput(OutSeverity::Info, oss.str());
         std::cerr << "[undoApp.ST] " << oss.str() << std::endl;
      }
   } catch (const std::exception& e) {
      m_ast = std::move(lastAst);
      m_semantic = std::move(lastSemantic);
      m_declarations = std::move(lastDeclarations);
      addOutput(OutSeverity::Info, std::string("Unexpected error: ") + e.what());
      std::cerr << "[undoApp.ST] Unexpected error: " << e.what() << std::endl;
   }

   // Set error markers on the appropriate editors
   setErrorMarkers();

   // Paint the symbols the analyzer resolved. Runs even when parsing failed,
   // because a partial AST still resolves the declarations it did manage to
   // read, which keeps highlighting useful while the user is mid-edit.
   refreshSemanticHighlighting();
}

/// @brief Whether an argument can be handed to a shell as it stands
///
/// Plain names, the ones every path is made of, need nothing. A leading dash does:
/// a file called -o would be read as an option rather than as a name. Everything
/// else, a space, a quote, a semicolon, a dollar, is left to the quoting below.
static bool isPlainShellArgument(const std::string& argument)
{
   if (argument.empty() || argument[0] == '-') {
      return false;
   }
   for (const char character : argument) {
      const bool plain = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
                         (character >= '0' && character <= '9') || character == '_' || character == '.' ||
                         character == '/' || character == '~' || character == ':' || character == '@' ||
                         character == '%' || character == '+' || character == '=' || character == ',';
      if (!plain) {
         return false;
      }
   }
   return true;
}

/// @brief One shell argument, quoted when it would not survive being pasted
///
/// A path comes from the file system and from the user, so it is not trusted to be
/// free of spaces, quotes or semicolons. Quoting means a file called "my plc.st" is
/// one argument, and a file called $(rm -rf x).st is a name that does not exist
/// rather than a command that runs.
static std::string shellQuote(const std::string& argument)
{
   if (isPlainShellArgument(argument)) {
      return argument;
   }
#ifdef _WIN32
   // The Windows shell has no single quotes, and %VAR% expands inside double ones.
   // The compiler is invoked without a shell there, so this only has to survive
   // being read back by the user.
   return "\"" + argument + "\"";
#else
   std::string quoted = "'";
   for (const char character : argument) {
      if (character == '\'') {
         quoted += "'\\''"; // close the quote, escape one, open it again
      } else {
         quoted += character;
      }
   }
   quoted += "'";
   return quoted;
#endif
}

/**
 * @brief Compile the current ST code
 *
 * Validates the code and, if successful, displays the AST structure
 * in the output panel. Future versions will generate C++ code.
 */
void STApp::compile()
{
   validateAndParse();
   if (!m_errors.empty()) {
      return;
   }

   // Named for what it is: this is the editor's own reading of what is on screen,
   // not the driver's verdict on the file, and the two sit in the same panel with
   // the driver speaking after it.
   addOutput(OutSeverity::Info, "The open buffer parses and analyses without errors.");
   addOutput(OutSeverity::Info, "");
   addOutput(OutSeverity::Info, "// AST Structure:");

   if (m_ast) {
      for (const auto& pou : m_ast->pous) {
         std::string kind;
         switch (pou.kind) {
         case POUKind::PROGRAM:
            kind = "PROGRAM";
            break;
         case POUKind::FUNCTION_BLOCK:
            kind = "FUNCTION_BLOCK";
            break;
         case POUKind::FUNCTION:
            kind = "FUNCTION";
            break;
         }
         addOutput(OutSeverity::Info, "  " + kind + " " + pou.name);

         for (const auto& sec : pou.varSections) {
            std::string vkind;
            switch (sec.kind) {
            case VarKind::VAR:
               vkind = "VAR";
               break;
            case VarKind::INPUT:
               vkind = "VAR_INPUT";
               break;
            case VarKind::OUTPUT:
               vkind = "VAR_OUTPUT";
               break;
            case VarKind::IN_OUT:
               vkind = "VAR_IN_OUT";
               break;
            case VarKind::EXTERNAL:
               vkind = "VAR_EXTERNAL";
               break;
            case VarKind::GLOBAL:
               vkind = "VAR_GLOBAL";
               break;
            case VarKind::TEMP:
               vkind = "VAR_TEMP";
               break;
            }
            addOutput(OutSeverity::Info, "    " + vkind + " (" + std::to_string(sec.decls.size()) + " vars)");
         }

         // Show methods for FUNCTION_BLOCK
         if (!pou.methods.empty()) {
            for (const auto& meth : pou.methods) {
               addOutput(OutSeverity::Info, "    METHOD " + meth.name);
            }
         }
      }

      for (const auto& st : m_ast->structs) {
         addOutput(OutSeverity::Info, "  STRUCT " + st.name + " (" + std::to_string(st.members.size()) + " members)");
      }

      for (const auto& en : m_ast->enums) {
         addOutput(OutSeverity::Info, "  ENUM " + en.name + " (" + std::to_string(en.enumerators.size()) + " enumerators)");
      }
   }

   addOutput(OutSeverity::Info, "");
   runTranspiler();
}

// ============================================================================
//  Transpiler
// ============================================================================

/**
 * @brief Directory the running undoStudio binary lives in
 * @return The directory without a trailing slash, or empty when it cannot be told
 *
 * The transpiler is looked for around here, because the build tree keeps the two
 * next to each other and an install that never reached /usr/local should still
 * work. An empty result leaves the search on PATH alone.
 */
static std::string executableDir()
{
#ifdef __linux__
   char buffer[4096];
   const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
   if (length <= 0) {
      return {};
   }
   buffer[length] = '\0';
   std::string path(buffer);
#elif defined(_WIN32)
   char buffer[MAX_PATH];
   const DWORD length = GetModuleFileNameA(nullptr, buffer, sizeof(buffer));
   if (length == 0 || length >= sizeof(buffer)) {
      return {};
   }
   std::string path(buffer);
#else
   return {};
#endif
   const size_t slash = path.find_last_of("/\\");
   if (slash == std::string::npos) {
      return {};
   }
   if (slash == 0) {
      return "/";
   }
   return path.substr(0, slash);
}


/**
 * @brief Hand the project to st2cpp and put what it says in the Output panel
 *
 * The whole project is compiled rather than the open buffer, since a POU refers
 * to the ones beside it. The working directory is the project root, which is
 * what "--workspace ." is relative to and where --project-style writes its
 * per-POU files.
 *
 * --strict follows the project's own strictness, so that the build agrees with
 * what the editor has been reporting. Severity is read off the line, st2cpp
 * having no field for it.
 */
void STApp::runTranspiler()
{
   const undoStudio::core::ProjectManager& pm = undoStudio::core::ProjectManager::getInstance();
   const std::string projectPath = pm.getProjectPath();
   if (projectPath.empty()) {
      addOutput(OutSeverity::Error, "No project is open.");
      return;
   }

   // PATH first, then a few places around the running binary. command -v answers
   // for a bare name and for an absolute path alike, so one loop covers both, and
   // it only accepts something executable, so a directory of the same name is
   // passed over.
   const std::string home = executableDir();
   std::string candidates = "st2cpp";
   for (const std::string& relative : {"st2cpp/st2cpp", "build/st2cpp/st2cpp", "../build/st2cpp/st2cpp"}) {
      if (!home.empty()) {
         candidates += " '" + home + "/" + relative + "'";
      }
   }

   const std::string strict = (pm.getStrictness() == undoStudio::core::Strictness::On) ? "--strict " : "";
   const std::string cmd = "t2cpp=''; for c in " + candidates + "; do p=$(command -v \"$c\" 2>/dev/null) && "
                           "{ t2cpp=\"$p\"; break; }; done; "
                           "if [ -z \"$t2cpp\" ]; then echo 'st2cpp not found: not on PATH, nor beside undoStudio'; "
                           "exit 127; fi; echo \"st2cpp: $t2cpp\"; "
                           "cd '" + projectPath + "' && \"$t2cpp\" " + strict + "--project-style --workspace . 2>&1";
   addOutput(OutSeverity::Info, "// st2cpp " + strict + "--project-style --workspace .   (in " + projectPath + ")");

   FILE* pipe = popen(cmd.c_str(), "r");
   if (!pipe) {
      addOutput(OutSeverity::Error, "Cannot start a shell to run st2cpp.");
      return;
   }

   char buffer[1024];
   while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
      std::string line(buffer);
      while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
         line.pop_back();
      }
      if (line.empty()) {
         continue;
      }
      // Severity is the word in front of the message. A line carrying neither is
      // the compiler narrating what it did.
      std::string lower = line;
      std::transform(lower.begin(), lower.end(), lower.begin(),
                     [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
      OutSeverity severity = OutSeverity::Info;
      if (lower.find("error") != std::string::npos) {
         severity = OutSeverity::Error;
      } else if (lower.find("warning") != std::string::npos) {
         severity = OutSeverity::Warning;
      }
      addOutput(severity, line);
   }

   // pclose returns the child's status in its high bits, so a non-zero wait is
   // how a failed run shows up.
   const int status = pclose(pipe);
   addOutput(OutSeverity::Info, "");
   if (status == 0) {
      addOutput(OutSeverity::Success, "st2cpp finished.");
   } else {
      addOutput(OutSeverity::Error, "st2cpp failed (exit status " + std::to_string(status) + ").");
   }
}

// ============================================================================
// File and Workspace Management
// ============================================================================

/**
 * @brief Load a workspace from the given path
 * @param path Directory path to load as workspace
 *
 * Sets the workspace path and builds the file tree.
 */
void STApp::loadWorkspace(const std::string& path)
{
   invalidateWorkspaceIndex(path);
   m_workspacePath = path;
   m_rootNode.name = fs::path(path).filename().string();
   m_rootNode.path = path;
   m_rootNode.isDirectory = true;
   m_rootNode.expanded = true;
   buildFileTree(m_rootNode, path);
   std::cout << "[undoApp.ST] Workspace loaded: " << path << std::endl;
}

/**
 * @brief Save the currently open file
 *
 * Generates the full ST content from the two editor sections and writes
 * it to the current file path. After saving, automatic validation is performed.
 */
void STApp::saveCurrentFile()
{
   if (m_currentFilePath.empty()) {
      return;
   }

   std::string content = generateSTFile();

   std::ofstream file(m_currentFilePath);
   if (file.is_open()) {
      file << content;
      file.close();
      m_currentFileContent = content;
      m_isDirty = false;
      // The file on disk changed, so cached cross-file navigation is stale.
      invalidateWorkspaceIndex(m_currentFilePath);
      std::cout << "[undoApp.ST] Saved: " << m_currentFilePath << std::endl;

      // Automatic validation after save
      validateAndParse();
   } else {
      std::cerr << "[undoApp.ST] Failed to save: " << m_currentFilePath << std::endl;
   }
}

/**
 * @brief Create a new file with ST template
 * @param parentPath Directory where to create the file
 * @param name Name of the file (including extension)
 *
 * Creates a new .st file with the appropriate POU template based on
 * m_newPOUType and m_newPOUName.
 */
void STApp::createNewFile(const std::string& parentPath, const std::string& name)
{
   invalidateWorkspaceIndex(parentPath);
   std::string baseName = name;
   size_t dotPos = baseName.find_last_of('.');
   if (dotPos != std::string::npos) {
      baseName = baseName.substr(0, dotPos);
   }

   fs::path newPath = fs::path(parentPath) / name;
   if (fs::exists(newPath)) {
      std::cerr << "[undoApp.ST] File already exists: " << newPath.string() << std::endl;
      return;
   }

   std::stringstream content;
   switch (m_newPOUType) {
   case POUType::Program:
      content << "PROGRAM " << baseName << "\n";
      content << "VAR\n";
      content << "END_VAR\n";
      break;
   case POUType::FunctionBlock:
      content << "FUNCTION_BLOCK " << baseName << "\n";
      content << "VAR_INPUT\n";
      content << "END_VAR\n";
      content << "VAR_OUTPUT\n";
      content << "END_VAR\n";
      content << "VAR_IN_OUT\n";
      content << "END_VAR\n";
      content << "VAR\n";
      content << "END_VAR\n";
      break;
   case POUType::Function:
      content << "FUNCTION " << baseName << " : \n";
      content << "VAR_INPUT\n";
      content << "END_VAR\n";
      content << "VAR_OUTPUT\n";
      content << "END_VAR\n";
      content << "VAR_IN_OUT\n";
      content << "END_VAR\n";
      content << "VAR\n";
      content << "END_VAR\n";
      break;
   }
   content << "\n\n";

   switch (m_newPOUType) {
   case POUType::Program:
      content << "END_PROGRAM\n";
      break;
   case POUType::FunctionBlock:
      content << "END_FUNCTION_BLOCK\n";
      break;
   case POUType::Function:
      content << "END_FUNCTION\n";
      break;
   }

   std::ofstream file(newPath.string());
   if (file.is_open()) {
      file << content.str();
      file.close();
      std::cout << "[undoApp.ST] Created file: " << newPath.string() << std::endl;
      invalidateWorkspaceIndex(newPath.string());
      requestOpenFile(newPath.string());
      if (!m_workspacePath.empty()) {
         buildFileTree(m_rootNode, m_workspacePath);
      }
   } else {
      std::cerr << "[undoApp.ST] Failed to create file: " << newPath.string() << std::endl;
   }
}

/**
 * @brief Create a new folder in the workspace
 * @param parentPath Directory where to create the folder
 * @param name Name of the new folder
 */
void STApp::createNewFolder(const std::string& parentPath, const std::string& name)
{
   invalidateWorkspaceIndex(parentPath);
   fs::path newPath = fs::path(parentPath) / name;
   if (!fs::exists(newPath)) {
      fs::create_directory(newPath);
      std::cout << "[undoApp.ST] Created folder: " << newPath.string() << std::endl;
      if (!m_workspacePath.empty()) {
         buildFileTree(m_rootNode, m_workspacePath);
      }
   }
}

/**
 * @brief Delete a file or folder (shows confirmation popup)
 * @param path Path to delete
 *
 * This method does not delete immediately. It sets up the confirmation popup
 * state and opens the modal dialog. The actual deletion happens in the
 * confirmation popup handler.
 */
void STApp::deleteFile(const std::string& path)
{
   invalidateWorkspaceIndex(path);
   // Show confirmation popup instead of deleting immediately
   m_deletePendingPath = path;
   m_showDeleteConfirmation = true;
   ImGui::OpenPopup("Delete Confirmation");
}

// ============================================================================
// Workspace Dialog
// ============================================================================

/**
 * @brief Open a native folder selection dialog
 *
 * Uses tinyfiledialogs if available, otherwise falls back to zenity
 * on Linux, or a manual input popup.
 */
void STApp::openWorkspaceDialog()
{
#ifdef USE_TINYFILEDIALOGS
   const char* selected = tinyfd_selectFolderDialog("Select Workspace", "");
   if (selected) {
      loadWorkspace(selected);
   }
#else
   std::string cmd = "zenity --file-selection --directory --title='Select Workspace' 2>/dev/null";
   FILE* pipe = popen(cmd.c_str(), "r");
   if (pipe) {
      char buffer[1024];
      std::string result;
      while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
         result += buffer;
      }
      pclose(pipe);

      if (!result.empty() && result.back() == '\n') {
         result.pop_back();
      }

      if (!result.empty()) {
         loadWorkspace(result);
         return;
      }
   }

   static bool showWorkspaceDialog = true;
   static char workspacePath[1024] = "";

   if (!m_workspacePath.empty()) {
      strncpy(workspacePath, m_workspacePath.c_str(), sizeof(workspacePath) - 1);
   }

   ImGui::OpenPopup("Select Workspace");
#endif
}

/**
 * @brief Handle keyboard shortcuts
 * @return true if a shortcut was handled
 *
 * Handles global shortcuts:
 * - Ctrl+S: Save file
 * - Ctrl+N: New POU
 * - Ctrl+W: Close file
 */
bool STApp::handleKeyboardShortcuts()
{
   ImGuiIO& io = ImGui::GetIO();
   bool ctrlPressed = io.KeyCtrl || io.KeySuper;

   if (ctrlPressed && ImGui::IsKeyPressed(ImGuiKey_S)) {
      if (!m_currentFilePath.empty()) {
         saveCurrentFile();
         std::cout << "[undoApp.ST] Saved via Ctrl+S: " << m_currentFilePath << std::endl;
      }
      return true;
   }

   if (ctrlPressed && ImGui::IsKeyPressed(ImGuiKey_O)) {
      return true;
   }

   if (ctrlPressed && ImGui::IsKeyPressed(ImGuiKey_N)) {
      m_showNewPOUPopup = true;
      return true;
   }

   if (ctrlPressed && ImGui::IsKeyPressed(ImGuiKey_W)) {
      if (!m_currentFilePath.empty()) {
         m_currentFilePath.clear();
         m_variablesEditor->SetText("");
         m_bodyEditor->SetText("");
         m_pouName.clear();
         resetMethodState();
         std::cout << "[undoApp.ST] Closed via Ctrl+W" << std::endl;
      }
      return true;
   }

   return false;
}

/**
 * @brief Consume the member completion navigation keys
 *
 * Called before the editors render, not from handleKeyboardShortcuts(). TextEditor
 * processes its own keys inside Render(), so a key handled after that point has
 * already been acted upon by the editor: accepting with Enter would also insert a
 * line break, and Down would move the cursor off the line the list is filtering.
 * Returning true stops the remaining editors from seeing the key.
 *
 * Only the keys a suggestion list owns are taken. Home and End are deliberately
 * left to the editor: they are how a line is navigated, and a list that stole
 * them would make it impossible to fix a line without dismissing the list first.
 *
 * The key taken is returned rather than just a bool, because the caller has to
 * tell the editor it came from. Without that the same press both walks the list
 * and moves the caret, which is the one thing a suggestion list must not do.
 */
ImGuiKey STApp::handleMemberCompletionKeys()
{
   if (m_completionEditor == nullptr || m_completionCandidates.empty()) {
      return ImGuiKey_None;
   }

   if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
      moveMemberCompletion(1);
      return ImGuiKey_DownArrow;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
      moveMemberCompletion(-1);
      return ImGuiKey_UpArrow;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
      moveMemberCompletionByPage(1);
      return ImGuiKey_PageDown;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
      moveMemberCompletionByPage(-1);
      return ImGuiKey_PageUp;
   }
   // Inside a call, Tab belongs to the parameter list rather than to this one. The
   // user is writing an argument, and the names that argument can take are the
   // parameter names of the callee; what the scope offers under a half-typed word
   // is a list of library types, which is not what is being asked for. The
   // parameter list filters on the same word, so nothing is lost by asking it
   // first. The signature handler is the one that answers.
   const bool tabAsksForParameters =
       m_signatureEditor != nullptr && m_signatureEditor == m_completionEditor && !m_completionIsParameterList;
   if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !tabAsksForParameters) {
      acceptMemberCompletion();
      return ImGuiKey_Tab;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter)) {
      acceptMemberCompletion();
      return ImGui::IsKeyPressed(ImGuiKey_Enter) ? ImGuiKey_Enter : ImGuiKey_KeypadEnter;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      dismissMemberCompletion();
      return ImGuiKey_Escape;
   }

   return ImGuiKey_None;
}

/**
 * @brief Consume the signature help keys
 *
 * Escape only. The signature has no navigation of its own: the cursor moving
 * between arguments is what changes it, and the editor handles that already.
 * Only reached when the member list did not take the key, so Escape always
 * closes the overlay nearest the user.
 */
ImGuiKey STApp::handleSignatureHelpKeys()
{
   if (m_signatureEditor == nullptr) {
      return ImGuiKey_None;
   }
   if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      dismissSignatureHelp();
      return ImGuiKey_Escape;
   }
   // Tab inside a call offers its parameters, the way an editor offers the
   // arguments of a function while it is being written. Asked for rather than
   // automatic: a blank argument is an ordinary thing to be typing, and a list
   // appearing on its own would sit over the code being written. While the
   // parameter list is up, Tab is the member handler's business: it accepts the
   // row. A call with no parameters, or one whose parameters none match what has
   // been typed, leaves Tab to the editor.
   if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !m_completionIsParameterList) {
      m_parameterPickerEditor = m_signatureEditor;
      return ImGuiKey_Tab;
   }
   return ImGuiKey_None;
}

// ============================================================================
// METHOD management (FUNCTION_BLOCK only)
// ============================================================================

int STApp::findMethodIndex(const std::string& name) const
{
   for (std::size_t i = 0; i < m_methods.size(); ++i) {
      if (m_methods[i].name == name) {
         return static_cast<int>(i);
      }
   }
   return -1;
}

MethodEditors& STApp::getOrCreateMethodEditors(MethodData& method)
{
   auto it = m_methodEditors.find(method.name);
   if (it != m_methodEditors.end()) {
      return it->second;
   }

   MethodEditors editors;
   editors.variables = std::make_unique<TextEditor>();
   editors.body = std::make_unique<TextEditor>();

   auto stLang = CreateSTLanguageDefinition();
   TextEditor::Palette palette = m_variablesEditor->GetPalette();
   for (auto* ed : {editors.variables.get(), editors.body.get()}) {
      ed->SetPalette(palette);
      ed->SetLanguageDefinition(stLang);
      ed->SetShowWhitespaces(false);
      ed->SetTabSize(3);
   }
   editors.variables->SetText(method.variablesText);
   editors.body->SetText(method.bodyText);

   // Only paint when the pair is actually created. This function is called on
   // every frame for the tab that is open, and a full re-colorization of every
   // editor on every frame made the panel several times slower for no gain.
   auto result = m_methodEditors.emplace(method.name, std::move(editors));
   if (result.second) {
      // A freshly created tab has no semantic spans yet, and its body is only
      // analysed by the last parse pass. The segment map still describes the
      // same generated file, so only the new editor needs painting.
      refreshSemanticHighlighting();
   }
   return result.first->second;
}

void STApp::syncMethodEditorsToData()
{
   for (auto& [name, editors] : m_methodEditors) {
      int idx = findMethodIndex(name);
      if (idx < 0) {
         continue; // method was deleted but its editor pair lingers - skip it
      }
      m_methods[idx].variablesText = editors.variables->GetText();
      m_methods[idx].bodyText = editors.body->GetText();
   }
}

void STApp::addMethod(const std::string& name, const std::string& returnType)
{
   if (name.empty() || findMethodIndex(name) >= 0) {
      return; // empty or duplicate name
   }

   MethodData method;
   method.name = name;
   method.returnType = returnType;
   method.visibility = "PUBLIC";
   m_methods.push_back(method);

   // Switch straight to the new tab, matching CODESYS/TwinCAT behaviour.
   getOrCreateMethodEditors(m_methods.back());
   requestTabSelection(name);

   std::cout << "[undoApp.ST] Added method: " << name << std::endl;
}

void STApp::deleteMethod(const std::string& name)
{
   int idx = findMethodIndex(name);
   if (idx < 0) {
      return;
   }
   m_methods.erase(m_methods.begin() + idx);
   m_methodEditors.erase(name);
   if (m_activeTab == name) {
      requestTabSelection(std::string()); // fall back to the POU tab
   }
   std::cout << "[undoApp.ST] Deleted method: " << name << std::endl;
}

void STApp::resetMethodState()
{
   m_methods.clear();
   m_methodEditors.clear();
   m_activeTab.clear();
}

// ============================================================================
// File Operations
// ============================================================================

/**
 * @brief Generate ST file content from Variables and Body sections
 * @return Complete ST file content as string
 *
 * Reconstructs the full ST file from the two editor sections.
 * Ensures proper POU declaration, VAR sections, and END statements.
 */
std::string STApp::generateSTFile()
{
   std::vector<SourceSegment> ignored;
   return generateSTFileWithMap(ignored);
}

/**
 * @brief Generate the ST file and record where each editor section landed
 * @param outSegments Receives one entry per editor-backed block
 * @return The generated file content
 *
 * Layout is emitted and measured in a single pass, so the segment list can
 * never drift from the text that is actually produced. This matters because
 * st2cpp reports AST and diagnostic positions in generated-file coordinates,
 * while the user edits several independent TextEditor buffers.
 */
std::string STApp::generateSTFileWithMap(std::vector<SourceSegment>& outSegments)
{
   // Pull the latest text out of any currently-open method editors before
   // serializing - m_methods only holds a snapshot from the last sync/open.
   syncMethodEditorsToData();

   outSegments.clear();

   std::stringstream ss;
   int line = 1; // 1-based number of the next line to be written

   auto emit = [&](const std::string& s) {
      ss << s;
      for (char c : s) {
         if (c == '\n') {
            ++line;
         }
      }
   };

   // Emits one editor-backed block, newline-terminating it, and records the
   // range of generated lines it occupies. An empty editor falls back to
   // `fallback`, exactly as the plain serializer always did.
   auto emitBlock = [&](const std::string& text, const std::string& fallback, bool isVariables, const std::string& methodName) {
      SourceSegment seg;
      seg.isVariables = isVariables;
      seg.methodName = methodName;
      seg.fullStart = line;

      if (!text.empty()) {
         emit(text);
         if (text.back() != '\n') {
            emit("\n");
         }
      } else if (!fallback.empty()) {
         emit(fallback);
      }

      seg.fullEnd = line - 1;
      if (seg.fullEnd >= seg.fullStart) {
         outSegments.push_back(seg);
      }
   };

   // Header - POU declaration
   switch (m_pouType) {
   case POUType::Program:
      emit("PROGRAM " + m_pouName + "\n");
      break;
   case POUType::FunctionBlock:
      emit("FUNCTION_BLOCK " + m_pouName + "\n");
      break;
   case POUType::Function:
      emit("FUNCTION " + m_pouName + " : \n");
      break;
   }
   emit("\n");

   // Variables section (POU-level)
   emitBlock(m_variablesEditor->GetText(), "VAR\nEND_VAR\n", true, std::string());
   emit("\n");

   // Methods (FUNCTION_BLOCK only, but harmless if empty for other POU types).
   // Serialized between the POU's own VAR sections and the cyclic body,
   // matching TwinCAT/CODESYS layout.
   for (const auto& method : m_methods) {
      emit("METHOD " + method.name);
      if (!method.returnType.empty()) {
         emit(" : " + method.returnType);
      }
      emit("\n");

      emitBlock(method.variablesText, std::string(), true, method.name);
      if (method.variablesText.empty()) {
         emit("\n");
      }

      emitBlock(method.bodyText, std::string(), false, method.name);
      if (method.bodyText.empty()) {
         emit("\n");
      }

      emit("END_METHOD\n\n");
   }

   // Body section (cyclic code)
   emitBlock(m_bodyEditor->GetText(), "(* Body code *)\n", false, std::string());
   emit("\n");

   // Footer - END statement
   switch (m_pouType) {
   case POUType::Program:
      emit("END_PROGRAM\n");
      break;
   case POUType::FunctionBlock:
      emit("END_FUNCTION_BLOCK\n");
      break;
   case POUType::Function:
      emit("END_FUNCTION\n");
      break;
   }

   return ss.str();
}

/**
 * @brief Find the editor-backed block that owns a generated-file line
 * @param fullLine Line number in the generated file (1-based)
 * @return The owning segment, or nullptr for header/footer lines
 */
const SourceSegment* STApp::segmentForLine(int fullLine) const
{
   for (const auto& seg : m_sourceMap) {
      if (fullLine >= seg.fullStart && fullLine <= seg.fullEnd) {
         return &seg;
      }
   }
   return nullptr;
}

/**
 * @brief Resolve the TextEditor that backs a segment
 * @param segment Segment to resolve
 * @return The editor, or nullptr when the owning method tab is not open
 */
TextEditor* STApp::editorForSegment(const SourceSegment& segment) const
{
   if (segment.methodName.empty()) {
      return segment.isVariables ? m_variablesEditor.get() : m_bodyEditor.get();
   }

   auto it = m_methodEditors.find(segment.methodName);
   if (it == m_methodEditors.end()) {
      return nullptr; // method tab not instantiated yet
   }
   return segment.isVariables ? it->second.variables.get() : it->second.body.get();
}

/**
 * @brief Open a file for editing
 * @param path Path to the file to open
 *
 * Reads the file content and splits it into Variables and Body sections.
 * Uses simple parsing to extract POU type, name, and sections.
 */
void STApp::openFile(const std::string& path)
{
   std::ifstream file(path);
   if (!file.is_open()) {
      std::cerr << "[undoApp.ST] Failed to open file: " << path << std::endl;
      return;
   }

   // SetText below marks the editor as changed; that is our own doing.
   m_ignoreChangeFrames = 1;

   std::stringstream buffer;
   buffer << file.rdbuf();
   std::string content = buffer.str();
   file.close();

   m_variablesEditor->SetText("");
   m_bodyEditor->SetText("");
   m_pouName.clear();
   resetMethodState();

   size_t pos = 0;
   if (content.find("PROGRAM") != std::string::npos) {
      m_pouType = POUType::Program;
      pos = content.find("PROGRAM") + 8;
   } else if (content.find("FUNCTION_BLOCK") != std::string::npos) {
      m_pouType = POUType::FunctionBlock;
      pos = content.find("FUNCTION_BLOCK") + 15;
   } else if (content.find("FUNCTION") != std::string::npos) {
      m_pouType = POUType::Function;
      pos = content.find("FUNCTION") + 9;
   }

   size_t nameStart = content.find_first_not_of(" \t\n", pos);
   size_t headerLineEnd = std::string::npos;
   if (nameStart != std::string::npos) {
      size_t nameEnd = content.find_first_of(" \t\n(", nameStart);
      if (nameEnd != std::string::npos) {
         m_pouName = content.substr(nameStart, nameEnd - nameStart);
      }
      headerLineEnd = content.find('\n', nameStart);
   }

   // Find the matching END_xxx footer for this POU type, then split
   // everything in between via the METHOD-aware scanner.
   std::string endKeyword;
   switch (m_pouType) {
   case POUType::Program:
      endKeyword = "END_PROGRAM";
      break;
   case POUType::FunctionBlock:
      endKeyword = "END_FUNCTION_BLOCK";
      break;
   case POUType::Function:
      endKeyword = "END_FUNCTION";
      break;
   }

   if (headerLineEnd != std::string::npos) {
      size_t footerPos = content.rfind(endKeyword);
      if (footerPos != std::string::npos && footerPos > headerLineEnd) {
         std::string inner = content.substr(headerLineEnd, footerPos - headerLineEnd);
         FBParts parts = splitFunctionBlockBody(inner);
         m_variablesEditor->SetText(parts.pouVarText);
         m_bodyEditor->SetText(parts.cyclicBody);
         m_methods = std::move(parts.methods);
      }
   }

   m_currentFilePath = path;
   m_currentFileContent = content;

   std::cout << "[undoApp.ST] Opened: " << path << std::endl;
   std::cout << "[undoApp.ST] POU: " << m_pouName << " (type: " << static_cast<int>(m_pouType) << ")" << std::endl;

   validateAndParse();
   // The freshly loaded text now matches the file on disk.
   m_isDirty = false;

   // A Ctrl+Click may have queued a jump into this very file.
   applyPendingJump();

   // A tree click may have asked for a specific METHOD tab.
   if (!m_pendingTabAfterOpen.empty()) {
      const std::string method = m_pendingTabAfterOpen;
      m_pendingTabAfterOpen.clear();
      revealMethodTab(method);
   }
}

/**
 * @brief Rename a file or folder
 * @param oldPath Current path
 * @param newName New name for the file/folder
 *
 * Updates the file tree and updates the current file path if needed.
 */
void STApp::renameFile(const std::string& oldPath, const std::string& newName)
{
   invalidateWorkspaceIndex(oldPath);
   try {
      fs::path oldP = oldPath;
      fs::path newP = oldP.parent_path() / newName;

      if (!fs::exists(newP)) {
         fs::rename(oldP, newP);
         std::cout << "[undoApp.ST] Renamed: " << oldPath << " -> " << newP.string() << std::endl;

         if (m_currentFilePath == oldPath) {
            m_currentFilePath = newP.string();
         }

         if (!m_workspacePath.empty()) {
            buildFileTree(m_rootNode, m_workspacePath);
         }
      }
   } catch (const std::exception& e) {
      std::cerr << "[undoApp.ST] Failed to rename: " << e.what() << std::endl;
   }
}

/**
 * @brief Move a file to a destination directory
 * @param sourcePath Path of the file to move
 * @param destDir Destination directory
 *
 * Used primarily for drag and drop operations.
 */
void STApp::moveFile(const std::string& sourcePath, const std::string& destDir)
{
   invalidateWorkspaceIndex(sourcePath);
   try {
      fs::path src = sourcePath;
      fs::path dst = fs::path(destDir) / src.filename();

      if (!fs::exists(dst)) {
         fs::rename(src, dst);
         std::cout << "[undoApp.ST] Moved: " << sourcePath << " -> " << dst.string() << std::endl;

         if (m_currentFilePath == sourcePath) {
            m_currentFilePath = dst.string();
         }

         if (!m_workspacePath.empty()) {
            buildFileTree(m_rootNode, m_workspacePath);
         }
      }
   } catch (const std::exception& e) {
      std::cerr << "[undoApp.ST] Failed to move: " << e.what() << std::endl;
   }
}

// ============================================================================
// Workspace Panel Rendering
// ============================================================================

/**
 * @brief Render the Workspace panel
 *
 * Displays workspace selection, file tree, and create/rename/delete actions.
 * Also handles the delete confirmation popup.
 */
void STApp::renderWorkspacePanel()
{
   if (ImGui::Begin("Workspace", nullptr, ImGuiWindowFlags_NoCollapse)) {
      // Workspace selection buttons
      if (ImGui::Button("Select Workspace")) {
         openWorkspaceDialog();
      }
      ImGui::SameLine();
      if (ImGui::Button("Refresh")) {
         if (!m_workspacePath.empty()) {
            buildFileTree(m_rootNode, m_workspacePath);
         }
      }

      ImGui::Separator();

      if (!m_workspacePath.empty()) {
         ImGui::Text("Workspace: %s", m_workspacePath.c_str());
         ImGui::Separator();

         // New POU and New Folder buttons
         if (ImGui::Button("+ POU")) {
            m_showNewPOUPopup = true;
            m_newItemParent = m_workspacePath;
            m_newPOUName.clear();
            m_newPOUType = POUType::Program;
         }
         ImGui::SameLine();
         if (ImGui::Button("+ Folder")) {
            m_showNewFolderPopup = true;
            m_newItemParent = m_workspacePath;
            m_newItemName.clear();
         }

         // New POU popup
         if (m_showNewPOUPopup) {
               ImGui::OpenPopup("New POU");
         }
         if (ImGui::BeginPopupModal("New POU", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Create new POU in: %s", m_newItemParent.c_str());

            char nameBuf[256] = "";
            strncpy(nameBuf, m_newPOUName.c_str(), sizeof(nameBuf) - 1);
            if (ImGui::InputText("POU Name", nameBuf, sizeof(nameBuf))) {
               m_newPOUName = nameBuf;
            }

            const char* typeItems[] = {"Program", "Function Block", "Function"};
            int currentType = static_cast<int>(m_newPOUType);
            if (ImGui::Combo("Type", &currentType, typeItems, 3)) {
               m_newPOUType = static_cast<POUType>(currentType);
            }

            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), ".st extension will be added automatically");

            if (ImGui::Button("Create")) {
               if (!m_newPOUName.empty()) {
                  std::string fileName = m_newPOUName;
                  if (fileName.size() < 3 || fileName.substr(fileName.size() - 3) != ".st") {
                     fileName += ".st";
                  }
                  createNewFile(m_newItemParent, fileName);
               }
               m_showNewPOUPopup = false;
               ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
               m_showNewPOUPopup = false;
               ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
         }

         // New Folder popup
         if (m_showNewFolderPopup) {
               ImGui::OpenPopup("New Folder");
         }
         if (ImGui::BeginPopupModal("New Folder", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Create new folder in: %s", m_newItemParent.c_str());
            char nameBuf[256] = "";
            strncpy(nameBuf, m_newItemName.c_str(), sizeof(nameBuf) - 1);
            if (ImGui::InputText("Folder Name", nameBuf, sizeof(nameBuf))) {
               m_newItemName = nameBuf;
            }
            if (ImGui::Button("Create")) {
               if (!m_newItemName.empty()) {
                  createNewFolder(m_newItemParent, m_newItemName);
               }
               m_showNewFolderPopup = false;
               ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
               m_showNewFolderPopup = false;
               ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
         }

         // Rename popup
         if (m_showRenamePopup) {
               ImGui::OpenPopup("Rename");
         }
         if (ImGui::BeginPopupModal("Rename", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Rename: %s", fs::path(m_renamePath).filename().string().c_str());
            char nameBuf[256] = "";
            strncpy(nameBuf, m_newItemName.c_str(), sizeof(nameBuf) - 1);
            if (ImGui::InputText("New Name", nameBuf, sizeof(nameBuf))) {
               m_newItemName = nameBuf;
            }
            if (ImGui::Button("Rename")) {
               if (!m_newItemName.empty()) {
                  renameFile(m_renamePath, m_newItemName);
               }
               m_showRenamePopup = false;
               ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
               m_showRenamePopup = false;
               ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
         }

         ImGui::Separator();
         renderFileTree(m_rootNode);
      } else {
         ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No workspace selected");
         ImGui::Text("Click 'Select Workspace' to choose a folder");
      }
   }

   // ================================================================
   // Delete Confirmation Popup (rendered outside the main panel)
   // ================================================================
   if (m_showDeleteConfirmation) {
      ImGui::OpenPopup("Delete Confirmation");
   }

   if (ImGui::BeginPopupModal("Delete Confirmation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Are you sure you want to delete:");
      ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "%s", fs::path(m_deletePendingPath).filename().string().c_str());
      ImGui::Separator();
      ImGui::Text("This action cannot be undone!");

      if (ImGui::Button("Yes, Delete")) {
         // Perform the actual deletion
         try {
            if (fs::is_directory(m_deletePendingPath)) {
               fs::remove_all(m_deletePendingPath);
            } else {
               fs::remove(m_deletePendingPath);
            }
            std::cout << "[undoApp.ST] Deleted: " << m_deletePendingPath << std::endl;

            if (m_currentFilePath == m_deletePendingPath) {
               m_currentFilePath.clear();
               m_variablesEditor->SetText("");
               m_bodyEditor->SetText("");
               m_pouName.clear();
               resetMethodState();
            }

            if (!m_workspacePath.empty()) {
               buildFileTree(m_rootNode, m_workspacePath);
            }
         } catch (const std::exception& e) {
            std::cerr << "[undoApp.ST] Failed to delete: " << e.what() << std::endl;
         }

         m_showDeleteConfirmation = false;
         m_deletePendingPath.clear();
         ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
         m_showDeleteConfirmation = false;
         m_deletePendingPath.clear();
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }

   ImGui::End();
}

// ============================================================================
// File Tree Renderer
// ============================================================================

/**
 * @brief Recursively render the file tree
 * @param node Current node to render
 *
 * Supports drag and drop, context menus, and file opening.
 * Delete key support for file deletion has been removed to prevent
 * accidental deletions - use the context menu instead.
 */
void STApp::renderFileTree(FileNode& node)
{
   if (node.isDirectory) {
      ImGuiTreeNodeFlags flags = node.expanded ? ImGuiTreeNodeFlags_DefaultOpen : 0;

      if (m_isDragging && !m_draggedItemPath.empty()) {
         if (ImGui::IsItemHovered() && m_draggedItemPath != node.path) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.7f, 1.0f, 0.3f));
            flags |= ImGuiTreeNodeFlags_Selected;
         }
      }

      bool isOpen = ImGui::TreeNodeEx(node.name.c_str(), flags);

      if (m_isDragging && ImGui::IsItemHovered() && m_draggedItemPath != node.path) {
         ImGui::PopStyleColor();
      }

      if (m_isDragging && ImGui::IsItemHovered() && ImGui::IsMouseReleased(0)) {
         if (m_draggedItemPath != node.path) {
            moveFile(m_draggedItemPath, node.path);
            m_isDragging = false;
            m_draggedItemPath.clear();
         }
      }

      if (isOpen) {
         node.expanded = true;
         for (auto& child : node.children) {
            renderFileTree(child);
         }
         ImGui::TreePop();
      } else {
         node.expanded = false;
      }

      // Context menu for folder
      if (ImGui::IsItemClicked(1)) {
         ImGui::OpenPopup(("##ctx_" + node.path).c_str());
      }
      if (ImGui::BeginPopup(("##ctx_" + node.path).c_str())) {
         if (ImGui::MenuItem("New POU")) {
            m_showNewPOUPopup = true;
            m_newItemParent = node.path;
            m_newPOUName.clear();
            m_newPOUType = POUType::Program;
         }
         if (ImGui::MenuItem("New Folder")) {
            m_showNewFolderPopup = true;
            m_newItemParent = node.path;
            m_newItemName.clear();
         }
         if (ImGui::MenuItem("Rename")) {
            m_showRenamePopup = true;
            m_renamePath = node.path;
            m_newItemName = node.name;
         }
         if (ImGui::MenuItem("Delete Folder")) {
            deleteFile(node.path); // Shows confirmation popup
         }
         ImGui::EndPopup();
      }

      // Delete key is intentionally not handled here to prevent accidental deletion
   } else {
      // File node
      bool isST = node.name.size() > 3 && node.name.substr(node.name.size() - 3) == ".st";
      bool isSelected = (node.path == m_currentFilePath);
      // Hover state of the file item itself, so the method children below do
      // not inherit the file's double-click and context menu.
      bool fileItemHovered = false;

      // An .st file that owns METHODs becomes a branch, so the structure of the
      // FUNCTION_BLOCK is visible in the tree instead of only in the tab bar.
      ensureWorkspaceIndex();
      auto mit = isST ? m_workspaceMethods.find(node.path) : m_workspaceMethods.end();
      const bool hasMethods = (mit != m_workspaceMethods.end()) && !mit->second.empty();

      if (hasMethods) {
         ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen;
         if (isSelected) {
            flags |= ImGuiTreeNodeFlags_Selected;
         }
         const bool open = ImGui::TreeNodeEx(node.name.c_str(), flags);
         fileItemHovered = ImGui::IsItemHovered();
         if (ImGui::IsItemClicked(0) && !ImGui::IsItemToggledOpen()) {
            requestOpenFile(node.path);
         }
         if (open) {
            for (const auto& method : mit->second) {
               ImGui::PushID(method.name.c_str());
               const bool isMethodOpen = isSelected && m_activeTab == method.name;
               std::string label = method.name;
               if (!method.returnType.empty() && method.returnType != "VOID") {
                  label += " : " + method.returnType;
               }
               ImGui::Selectable(label.c_str(), isMethodOpen);
               if (ImGui::IsItemClicked(0)) {
                  openMethodOf(node.path, method.name);
               }
               ImGui::PopID();
            }
            ImGui::TreePop();
         }
      } else {
         ImGui::Selectable(node.name.c_str(), isSelected);
         fileItemHovered = ImGui::IsItemHovered();

         if (ImGui::IsItemClicked(0)) {
            if (isST) {
               requestOpenFile(node.path);
            }
         }
      }

      // Handle drag start
      if (fileItemHovered && ImGui::IsItemClicked(0) && ImGui::IsMouseDragging(0)) {
         m_isDragging = true;
         m_draggedItemPath = node.path;
      }

      // Handle drop
      if (m_isDragging && ImGui::IsItemHovered() && m_draggedItemPath != node.path) {
         if (ImGui::IsMouseReleased(0)) {
            fs::path targetPath = node.path;
            if (!fs::is_directory(targetPath)) {
               targetPath = targetPath.parent_path();
            }
            moveFile(m_draggedItemPath, targetPath.string());
            m_isDragging = false;
            m_draggedItemPath.clear();
         }
      }

      // Open file on double click
      if (fileItemHovered && ImGui::IsMouseDoubleClicked(0) && isST) {
         requestOpenFile(node.path);
      }

      // Delete key is intentionally not handled here to prevent accidental deletion

      // Context menu for file
      if (fileItemHovered && ImGui::IsItemClicked(1)) {
         ImGui::OpenPopup(("##ctx_" + node.path).c_str());
      }
      if (ImGui::BeginPopup(("##ctx_" + node.path).c_str())) {
         if (ImGui::MenuItem("Open")) {
            if (isST) {
               requestOpenFile(node.path);
            }
         }
         if (ImGui::MenuItem("Rename")) {
            m_showRenamePopup = true;
            m_renamePath = node.path;
            m_newItemName = node.name;
         }
         if (ImGui::MenuItem("Delete")) {
            deleteFile(node.path); // Shows confirmation popup
         }
         ImGui::EndPopup();
      }
   }
}

// ============================================================================
// Outline Panel Rendering
// ============================================================================

/**
 * @brief Render the Outline panel
 *
 * Displays the AST structure of the currently open file:
 * - POU names with their variable sections
 * - Methods for FUNCTION_BLOCK
 * - STRUCT definitions
 * - ENUM definitions
 */
void STApp::renderOutlinePanel()
{
   if (ImGui::Begin("ST Outline", nullptr, ImGuiWindowFlags_NoCollapse)) {
      if (!m_ast) {
         ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No AST available");
         ImGui::Text("Open a valid .st file");
         ImGui::End();
         return;
      }

      // Show POU list
      for (const auto& pou : m_ast->pous) {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.5f, 1.0f, 1.0f));
         if (ImGui::TreeNodeEx(pou.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PopStyleColor();

            // Show variable sections
            for (const auto& sec : pou.varSections) {
               std::string label;
               switch (sec.kind) {
               case VarKind::VAR:
                  label = "VAR";
                  break;
               case VarKind::INPUT:
                  label = "VAR_INPUT";
                  break;
               case VarKind::OUTPUT:
                  label = "VAR_OUTPUT";
                  break;
               case VarKind::IN_OUT:
                  label = "VAR_IN_OUT";
                  break;
               case VarKind::EXTERNAL:
                  label = "VAR_EXTERNAL";
                  break;
               case VarKind::GLOBAL:
                  label = "VAR_GLOBAL";
                  break;
               case VarKind::TEMP:
                  label = "VAR_TEMP";
                  break;
               }
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));
               if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_Leaf)) {
                  ImGui::PopStyleColor();
                  for (const auto& decl : sec.decls) {
                     ImGui::BulletText("%s", decl.name.c_str());
                  }
                  ImGui::TreePop();
               } else {
                  ImGui::PopStyleColor();
               }
            }

            // Show methods for FUNCTION_BLOCK
            if (!pou.methods.empty()) {
               ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
               for (const auto& meth : pou.methods) {
                  ImGui::BulletText("METHOD %s", meth.name.c_str());
               }
               ImGui::PopStyleColor();
            }
            ImGui::TreePop();
         } else {
            ImGui::PopStyleColor();
         }
      }

      // Show structs
      for (const auto& st : m_ast->structs) {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));
         if (ImGui::TreeNodeEx(st.name.c_str(), ImGuiTreeNodeFlags_Leaf)) {
            ImGui::PopStyleColor();
            for (const auto& member : st.members) {
               ImGui::BulletText("%s", member.name.c_str());
            }
            ImGui::TreePop();
         } else {
            ImGui::PopStyleColor();
         }
      }

      // Show enums
      for (const auto& en : m_ast->enums) {
         ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.8f, 0.8f, 1.0f));
         if (ImGui::TreeNodeEx(en.name.c_str(), ImGuiTreeNodeFlags_Leaf)) {
            ImGui::PopStyleColor();
            for (const auto& enumerator : en.enumerators) {
               ImGui::BulletText("%s", enumerator.name.c_str());
            }
            ImGui::TreePop();
         } else {
            ImGui::PopStyleColor();
         }
      }
   }
   ImGui::End();
}

// ============================================================================
// Editor Panel Rendering
// ============================================================================

/**
 * @brief Render the ST Editor panel
 *
 * Displays a two-pane editor with Variables and Body sections,
 * with a thin draggable splitter (like VS Code) to resize both sections.
 * The toolbar with Save, Compile, Validate, and Close buttons is placed
 * at the top, always visible without scrolling.
 */

void STApp::renderSplitEditor(TextEditor& varEditor, TextEditor& bodyEditor, float& splitterPos, const char* varLabel,
                               const char* bodyLabel, const char* varId, const char* bodyId)
{
   ImVec2 avail = ImGui::GetContentRegionAvail();
   float totalEditorHeight = avail.y;
   if (totalEditorHeight < 100.0f) {
      totalEditorHeight = 100.0f;
   }

   float topHeight = totalEditorHeight * splitterPos;
   float bottomHeight = totalEditorHeight * (1.0f - splitterPos);

   float minSectionHeight = 60.0f;
   if (topHeight < minSectionHeight) {
      bottomHeight -= (minSectionHeight - topHeight);
      topHeight = minSectionHeight;
   }
   if (bottomHeight < minSectionHeight) {
      topHeight -= (minSectionHeight - bottomHeight);
      bottomHeight = minSectionHeight;
   }

   // Member completion keys are handled before the editors render: TextEditor
   // acts on its own keys inside Render(), so handling them afterwards would let
   // Enter both accept the member and insert a line break, and let the arrows walk
   // the cursor off the line the list is filtering.
   //
   // Each key taken is then blocked on the editor that owns the overlay, which is
   // what stops the same press from also moving the caret. Blocking the single key
   // is enough, and the editor stays otherwise untouched: muting it for the frame
   // would swallow any character typed in that frame as well, and would stop it
   // raising io.WantTextInput, so the keyboard ownership below would have to be
   // guessed rather than read.
   //
   // The block is cleared again as soon as the editors have rendered, and at the
   // top of every frame, so a key is only ever spent once even if a frame is
   // abandoned part way through.
   varEditor.ClearBlockedKeys();
   bodyEditor.ClearBlockedKeys();

   // Which editor each overlay belongs to, taken before the handlers run. Accepting
   // or dismissing clears the owner pointer, and the key the handler took still has
   // to be taken away from that editor, so the owner has to be known first.
   TextEditor* const memberOwner = m_completionEditor;
   TextEditor* const signatureOwner = m_signatureEditor;
   TextEditor* const snippetOwner = m_snippetEditor;

   // The statement list answers first: it and the member list can be asked about
   // the same word, and what is being typed is a keyword rather than a name.
   const ImGuiKey snippetKey = handleStatementCompletionKeys();
   const ImGuiKey memberKey = (snippetKey == ImGuiKey_None) ? handleMemberCompletionKeys() : ImGuiKey_None;
   // The signature help only answers Escape, and only when neither list took the
   // key first, so Escape always closes the overlay nearest the user.
   const ImGuiKey signatureKey =
       (memberKey == ImGuiKey_None && snippetKey == ImGuiKey_None) ? handleSignatureHelpKeys() : ImGuiKey_None;

   // An overlay belonging to an editor that is not on screen would never be
   // refreshed, since only the open tab renders. It would sit there offering
   // suggestions for a buffer the user is no longer looking at.
   if (m_completionEditor != nullptr && m_completionEditor != &varEditor && m_completionEditor != &bodyEditor) {
      clearMemberCompletion();
   }
   if (m_signatureEditor != nullptr && m_signatureEditor != &varEditor && m_signatureEditor != &bodyEditor) {
      clearSignatureHelp();
   }
   if (m_snippetEditor != nullptr && m_snippetEditor != &varEditor && m_snippetEditor != &bodyEditor) {
      clearStatementCompletion();
   }

   if (snippetKey != ImGuiKey_None && snippetOwner != nullptr) {
      snippetOwner->BlockKey(snippetKey);
   }
   if (memberKey != ImGuiKey_None && memberOwner != nullptr) {
      memberOwner->BlockKey(memberKey);
   }
   if (signatureKey != ImGuiKey_None && signatureOwner != nullptr) {
      signatureOwner->BlockKey(signatureKey);
   }

   ImGui::Text("%s", varLabel);
   varEditor.Render(varId, ImVec2(-1.0f, topHeight));
   // Immediately after Render: the click has just been consumed and the cursor
   // now sits on the clicked glyph. Only the focused editor raises
   // io.WantTextInput, so this is a statement about this editor alone.
   handleNavigationInput(varId, varEditor);
   // Only a focused editor raises io.WantTextInput, and ImGui clears the flag at
   // the start of the frame, so reading it straight after this Render is a
   // statement about this editor alone: the body editor, rendered later in this
   // same frame, has not had its turn yet.
   const bool varHasKeyboard = ImGui::GetIO().WantTextInput;
   updateStatementCompletion(varId, varEditor, varHasKeyboard, true);
   updateMemberCompletion(varId, varEditor, varHasKeyboard);
   renderParameterNameHint(varId, varEditor);
   updateSignatureHelp(varId, varEditor, varHasKeyboard);

   // Splitter (VS Code style)
   ImGui::Spacing();
   float availWidth = ImGui::GetContentRegionAvail().x;
   float splitterHeight = 3.0f;
   ImGui::PushID(varId);
   ImGui::InvisibleButton("splitter", ImVec2(availWidth, splitterHeight));

   ImDrawList* drawList = ImGui::GetWindowDrawList();
   ImVec2 itemMin = ImGui::GetItemRectMin();
   ImVec2 itemMax = ImGui::GetItemRectMax();

   ImU32 lineColor = IM_COL32(60, 70, 90, 255);
   if (ImGui::IsItemHovered()) {
      lineColor = IM_COL32(0, 180, 216, 180);
   }
   if (ImGui::IsItemActive()) {
      lineColor = IM_COL32(0, 180, 216, 255);
   }

   float lineY = (itemMin.y + itemMax.y) * 0.5f;
   drawList->AddLine(ImVec2(itemMin.x, lineY), ImVec2(itemMax.x, lineY), lineColor, 2.0f);

   if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
      float mouseDelta = ImGui::GetMouseDragDelta(0).y;
      float newPos = splitterPos + (mouseDelta / totalEditorHeight);
      splitterPos = std::max(0.1f, std::min(0.9f, newPos));
      ImGui::ResetMouseDragDelta(0);
      m_splitterDirty = true;
   }
   // Written when the drag is over rather than on every frame of it: a write per
   // frame of a drag is a write per frame of the whole run.
   if (m_splitterDirty && !ImGui::IsItemActive()) {
      saveSplitterPos(splitterPos);
      m_splitterDirty = false;
   }

   if (ImGui::IsItemHovered()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
   }
   ImGui::PopID();

   ImGui::Spacing();

   ImGui::Text("%s", bodyLabel);
   bodyEditor.Render(bodyId, ImVec2(-1.0f, bottomHeight));
   // Both editors have had their turn with the blocked keys, so they are handed
   // back: the block was only ever meant for this frame's key press.
   varEditor.ClearBlockedKeys();
   bodyEditor.ClearBlockedKeys();

   handleNavigationInput(bodyId, bodyEditor);
   const bool bodyHasKeyboard = ImGui::GetIO().WantTextInput;
   updateStatementCompletion(bodyId, bodyEditor, bodyHasKeyboard, false);
   updateMemberCompletion(bodyId, bodyEditor, bodyHasKeyboard);
   renderParameterNameHint(bodyId, bodyEditor);
   updateSignatureHelp(bodyId, bodyEditor, bodyHasKeyboard);
}

/**
 * @brief Let the user pick a target when a name is declared in several places
 */
void STApp::renderJumpPopup()
{
   if (!m_showJumpPopup) {
      return;
   }
   ImGui::OpenPopup("Go to declaration");
   if (ImGui::BeginPopupModal("Go to declaration", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("'%s' is declared in several places:", m_jumpCandidateName.c_str());
      ImGui::Separator();

      int chosen = -1;
      for (size_t i = 0; i < m_jumpCandidates.size(); ++i) {
         const WorkspaceDeclaration& c = m_jumpCandidates[i];
         const std::string file = fs::path(c.file).filename().string();
         const std::string where = c.scope.empty() ? file : (file + " / " + c.scope);
         if (ImGui::Selectable(where.c_str(), false, 0, ImVec2(320.0f, 0.0f))) {
            chosen = (int)i;
         }
         if (!c.typeText.empty() && c.typeText != c.pouName) {
            ImGui::SameLine();
            ImGui::TextDisabled(": %s", c.typeText.c_str());
         }
      }

      ImGui::Separator();
      if (chosen >= 0) {
         m_pendingJumpName = m_jumpCandidateName;
         const std::string target = m_jumpCandidates[chosen].file;
         m_showJumpPopup = false;
         m_jumpCandidates.clear();
         ImGui::CloseCurrentPopup();
         if (target != m_currentFilePath) {
            requestOpenFile(target);
         }
      }
      if (ImGui::Button("Cancel")) {
         m_showJumpPopup = false;
         m_jumpCandidates.clear();
         ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
   }
}

void STApp::renderEditorPanel()
{
   handleKeyboardShortcuts();
   updateDirtyState();
   renderDirtyPrompt();
   renderJumpPopup();

   if (ImGui::Begin("ST Editor", nullptr, ImGuiWindowFlags_NoCollapse)) {
      if (m_currentFilePath.empty()) {
         ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No file open");
         ImGui::Text("Select a .st file from the Workspace panel");
         ImGui::End();
         return;
      }

      // File info bar
      ImGui::Text("File: %s", fs::path(m_currentFilePath).filename().string().c_str());
      if (m_isDirty) {
         ImGui::SameLine();
         ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f), "(unsaved changes)");
      }

      // Tooltips are off by default: they get in the way when reading code.
      ImGui::SameLine();
      if (ImGui::Checkbox("Tooltips", &m_showTooltips)) {
      }
      ImGui::SameLine();

      const char* typeStr = "";
      switch (m_pouType) {
      case POUType::Program:
         typeStr = "PROGRAM";
         break;
      case POUType::FunctionBlock:
         typeStr = "FUNCTION_BLOCK";
         break;
      case POUType::Function:
         typeStr = "FUNCTION";
         break;
      }
      ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), "| %s: %s", typeStr, m_pouName.c_str());

      // Toolbar with action buttons (always visible at the top, applies to
      // the whole POU - saving/compiling serializes every method tab too)
      ImGui::Separator();
      if (ImGui::Button("Save")) {
         saveCurrentFile();
      }
      ImGui::SameLine();
      if (ImGui::Button("Compile")) {
         compile();
      }
      ImGui::SameLine();
      if (ImGui::Button("Validate")) {
         validateAndParse();
      }
      ImGui::SameLine();
      if (ImGui::Button("Close")) {
         m_currentFilePath.clear();
         m_variablesEditor->SetText("");
         m_bodyEditor->SetText("");
         m_pouName.clear();
         m_ast.reset();
         m_errors.clear();
         resetMethodState();
      }

      // Add Method popup (name + return type only, as requested)
      //
      // The request can come from this panel (the "+" tab) or from the tree's
      // context menu. A request from the tree is honoured here rather than there
      // on purpose: OpenPopup/BeginPopupModal are resolved per window, so opening
      // the popup in the Workspace window and submitting it in the ST Editor one
      // makes the popup silently never appear. The tree therefore only sets the
      // pending flag, and this is the window that actually opens it.
      if (consumeAddMethodDialogRequest()) {
         ImGui::OpenPopup("Add Method");
      }
      if (m_showAddMethodPopup) {
         ImGui::OpenPopup("Add Method");
      }
      if (ImGui::BeginPopupModal("Add Method", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
         char nameBuf[256] = "";
         strncpy(nameBuf, m_newMethodName.c_str(), sizeof(nameBuf) - 1);
         if (ImGui::InputText("Method Name", nameBuf, sizeof(nameBuf))) {
            m_newMethodName = nameBuf;
         }

         char retBuf[64] = "";
         strncpy(retBuf, m_newMethodReturnType.c_str(), sizeof(retBuf) - 1);
         if (ImGui::InputText("Return Type", retBuf, sizeof(retBuf))) {
            m_newMethodReturnType = retBuf;
         }

         bool nameTaken = !m_newMethodName.empty() && findMethodIndex(m_newMethodName) >= 0;
         if (nameTaken) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "A method with this name already exists");
         }

         ImGui::BeginDisabled(m_newMethodName.empty() || nameTaken);
         if (ImGui::Button("Create")) {
            addMethod(m_newMethodName, m_newMethodReturnType);
            m_newMethodName.clear();
            m_newMethodReturnType.clear();
            // The flag has to be cleared, not just the popup closed: while it is
            // set, OpenPopup is called again on the next frame and the box comes
            // straight back, so it could never be dismissed and, being a modal,
            // it kept the whole application off the pointer.
            m_showAddMethodPopup = false;
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndDisabled();
         ImGui::SameLine();
         if (ImGui::Button("Cancel")) {
            m_newMethodName.clear();
            m_newMethodReturnType.clear();
            m_showAddMethodPopup = false;
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      // Delete Method confirmation
      if (m_showDeleteMethodConfirmation) {
         ImGui::OpenPopup("Delete Method?");
      }
      if (ImGui::BeginPopupModal("Delete Method?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
         ImGui::Text("Delete method '%s'? This cannot be undone.", m_methodToDelete.c_str());
         if (ImGui::Button("Yes, Delete")) {
            deleteMethod(m_methodToDelete);
            m_methodToDelete.clear();
            m_showDeleteMethodConfirmation = false;
            ImGui::CloseCurrentPopup();
         }
         ImGui::SameLine();
         if (ImGui::Button("Cancel")) {
            m_methodToDelete.clear();
            m_showDeleteMethodConfirmation = false;
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }

      // Tab bar: POU tab first, then one tab per method, then a trailing
      // "+" to add a new one (FUNCTION_BLOCK only - PROGRAM/FUNCTION have
      // no methods in ST).
      if (ImGui::BeginTabBar("##EditorTabs", ImGuiTabBarFlags_Reorderable)) {
         // Selection is driven by ImGui, not by us: passing a bool* to
         // BeginTabItem makes ImGui overwrite it from its own SelectedTabId on
         // every frame, so a value we compute ourselves oscillates and the tabs
         // stop responding to clicks. Programmatic switches therefore use
         // ImGuiTabItemFlags_SetSelected, which is the supported trigger.
         ImGuiTabItemFlags pouFlags = m_pendingSelectPOU ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
         bool pouTabOpen = ImGui::BeginTabItem(m_pouName.empty() ? "POU" : m_pouName.c_str(), nullptr, pouFlags);
         if (pouTabOpen) {
            m_activeTab.clear();
            renderSplitEditor(*m_variablesEditor, *m_bodyEditor, m_splitterPos,
                               "Variables (VAR / VAR_INPUT / VAR_OUTPUT / VAR_GLOBAL ...)", "Body (Cyclic code)",
                               "##variables_editor", "##body_editor");
            ImGui::EndTabItem();
         }

         for (auto& method : m_methods) {
            ImGui::PushID(method.name.c_str());
            ImGuiTabItemFlags flags = (m_pendingTabSelection == method.name) ? ImGuiTabItemFlags_SetSelected
                                                                              : ImGuiTabItemFlags_None;
            bool tabOpen = ImGui::BeginTabItem(method.name.c_str(), nullptr, flags);

            if (ImGui::BeginPopupContextItem("##method_ctx")) {
               if (ImGui::MenuItem("Delete Method")) {
                  m_methodToDelete = method.name;
                  m_showDeleteMethodConfirmation = true;
               }
               ImGui::EndPopup();
            }

            if (tabOpen) {
               m_activeTab = method.name;
               MethodEditors& editors = getOrCreateMethodEditors(method);
               std::string varLabel = "Variables (VAR_INPUT / VAR_OUTPUT / VAR_IN_OUT / VAR ...)";
               std::string bodyLabel = "Body";
               renderSplitEditor(*editors.variables, *editors.body, editors.splitterPos, varLabel.c_str(),
                                  bodyLabel.c_str(), "##method_variables", "##method_body");
               ImGui::EndTabItem();
            }
            ImGui::PopID();
         }

         if (m_pouType == POUType::FunctionBlock) {
            if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing)) {
               m_newMethodName.clear();
               m_newMethodReturnType.clear();
               m_showAddMethodPopup = true;
            }
         }

         ImGui::EndTabBar();

         // The request has now been handed to ImGui; do not force it again,
         // otherwise the tab would be pinned and clicking would feel broken.
         m_pendingTabSelection.clear();
         m_pendingSelectPOU = false;
      }
   }
   ImGui::End();

   // Drawn after the main window so neither is clipped by it, and the lists last
   // so they win where the two would overlap: the signature belongs to the call
   // the list is completing, so the list is the one closer to the cursor. The
   // statement list follows for the same reason, and is the last word because it
   // is the one that answered the key press.
   renderSignatureHelp();
   renderMemberCompletion();
   renderStatementCompletion();
}

// ============================================================================
// Output Panel Rendering
// ============================================================================

/**
 * @brief Render the Output panel
 *
 * Displays compilation output, validation results, and error messages.
 * Errors are shown in red, success messages in green.
 */
void STApp::renderOutputPanel()
{
   if (ImGui::Begin("ST Output", nullptr, ImGuiWindowFlags_NoCollapse)) {
      ImGui::BeginChild("OutputLog", ImVec2(-1.0f, -1.0f), true);
      for (const auto& line : m_outputLines) {
         switch (line.severity) {
         case OutSeverity::Error:
            ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "%s", line.text.c_str());
            break;
         case OutSeverity::Warning:
            ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f), "%s", line.text.c_str());
            break;
         case OutSeverity::Success:
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "%s", line.text.c_str());
            break;
         case OutSeverity::Info:
            ImGui::TextColored(ImVec4(0.65f, 0.68f, 0.74f, 1.0f), "%s", line.text.c_str());
            break;
         }
      }
      ImGui::EndChild();
   }
   ImGui::End();
}


} // namespace ST
} // namespace undoApp
