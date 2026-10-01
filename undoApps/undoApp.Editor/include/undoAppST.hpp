/**
 * @file undoAppST.hpp
 * @brief Header of the Structured Text editor undoApp
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <unordered_map>

// Include TextEditor
#include <TextEditor.h>

// Include st2cpp headers
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "ast/AST.h"
#include "undoAppSTSemantic.hpp"
#include "undoAppSTOutput.hpp"
#include "undoAppSTSnippet.hpp"
#include "semantic/SemanticAnalyzer.h"
#include "semantic/LibraryDescriptorBuilder.h"
#include "library/LibraryRegistry.h"
#include "semantic/SymbolTable.h"

namespace undoApp {
namespace ST {

using TranslationUnit = ::TranslationUnit;
using ParseError = ::ParseError;

/**
 * @brief Enumeration of POU (Program Organization Unit) types
 */
enum class POUType {
   Program,
   FunctionBlock,
   Function
};

/**
 * @brief Node structure for the file tree representation
 */
struct FileNode
{
    std::string name;               ///< File or directory name
    std::string path;               ///< Full filesystem path
    bool isDirectory;               ///< True if this node represents a directory
    std::vector<FileNode> children; ///< Child nodes (files and subdirectories)
    bool expanded = false;          ///< True if the tree node is expanded in the UI
    bool isDragging = false;        ///< True if this node is being dragged
    int role = 0;               ///< Semantic role within an undoProject (0=Generic)
};

/**
 * @brief Data structure for a method inside a FUNCTION_BLOCK
 */
struct MethodData
{
   std::string name;          ///< Method name
   std::string returnType;    ///< Return type (empty for void)
   std::string variablesText; ///< All VAR sections (VAR_INPUT, VAR_OUTPUT, VAR_TEMP, VAR)
   std::string bodyText;      ///< Method body (statements)
   std::string visibility;    ///< PUBLIC, PROTECTED, PRIVATE (default: PUBLIC)
   bool isAbstract = false;   ///< True if abstract method
   bool isFinal = false;      ///< True if final method
   bool isOverride = false;   ///< True if override method
};

/**
 * @brief Editor pair for a single METHOD tab (created lazily on first open)
 */
struct MethodEditors
{
   std::unique_ptr<TextEditor> variables;
   std::unique_ptr<TextEditor> body;
   float splitterPos = 0.5f;
};

/**
 * @brief One open ST file, held whole so it can be put back
 *
 * Everything openFile() reads out of the file, and nothing that can be worked out
 * again from it: the AST, the semantic analysis and the source map are all
 * derived, and keeping them would mean a snapshot that is stale the moment a
 * character is typed.
 *
 * The editor contents are the point of it. A tab that is still open has to come
 * back showing what was in it, and reading the file off disk would give back the
 * version on disk rather than the one being edited, which is the unsaved half of
 * every change made since it was opened.
 */
struct STDocument
{
   std::string path;                    ///< Absolute path, where a later save goes
   POUType pouType = POUType::Program;  ///< PROGRAM, FUNCTION_BLOCK or FUNCTION
   std::string pouName;                 ///< Name of the POU
   std::string functionReturnType;      ///< Return type of a FUNCTION
   std::string pouVariablesText;        ///< The POU's VAR sections
   std::string pouBodyText;             ///< The POU's body
   std::vector<MethodData> methods;     ///< Its methods, with their texts
   float splitterPos = 0.5f;            ///< Where the two panes were divided
   std::string activeTab;               ///< Which method tab was showing, empty for the POU
};

/**
 * @brief One editor-backed block of the generated ST file
 *
 * Lines are 1-based and inclusive, matching the line numbers st2cpp reports in
 * its AST nodes and diagnostics. The owning editor's line is
 * `fullLine - fullStart + 1`.
 */
struct SourceSegment
{
   int fullStart = 0;        ///< First line of the block in the generated file
   int fullEnd = 0;          ///< Last line of the block (inclusive)
   bool isVariables = false; ///< True for a VAR* block, false for a body block
   std::string methodName;   ///< Empty for the POU itself, else the owning METHOD
};

/**
 * @brief Main application class for the Structured Text editor plugin
 *
 * Implements a two-pane editor (Variables + Body) with workspace management,
 * syntax highlighting, semantic validation, AST outline, and method support.
 */
class STApp
{
public:
   /// @brief Get the singleton instance of STApp
   static STApp& getInstance();

   /// @brief Initialize the ST undoApp
   bool initialize();

   /// @brief Shutdown the ST undoApp and release resources
   void shutdown();

   /// @brief Register UI panels with ImGuiManager
   void registerPanels();

   // ============================================================================
   // Panel rendering methods
   // ============================================================================

   /// @brief Do the ST editor's per-frame work, before any document is drawn
   ///
   /// Keyboard shortcuts, the unsaved-changes state, and the two popups that have
   /// to be opened and closed inside the panel that draws them: ImGui resolves a
   /// popup against the window it is opened in, so one opened anywhere else is
   /// never found again.
   void beginEditorFrame();

   /// @brief Draw the Structured Text document, inside the editor panel
   ///
   /// The panel belongs to EditorApp, which draws the bar of open files and then
   /// asks for whichever backend the file on screen needs. This is the ST one, so
   /// it draws no window of its own: a second Begin here would be a second window
   /// with the panel's name, laid out twice in one frame.
   void renderDocument();

   /// @brief Draw the ST editor's overlays, after the document
   ///
   /// The signature help and the two completion lists are separate windows drawn
   /// after the document so that the editor cannot clip them, and the navigation
   /// keys are claimed last so that the claim exists by the end of the frame.
   void endEditorFrame();

   /// @brief Render the Output panel (validation and compilation messages)
   void renderOutputPanel();

   /// @brief Render the Outline panel (AST structure tree with methods)
   void renderOutlinePanel();

   /// @brief Render the workspace/file-tree panel
   void renderWorkspacePanel();

   /// @brief Render one file-tree node and its children recursively
   void renderFileTree(FileNode& node);

   // ============================================================================
   // Workspace and file management
   // ============================================================================

   /// @brief Load a workspace directory into the file tree
   void loadWorkspace(const std::string& path);

   /// @brief Open a folder-picker dialog and load the chosen workspace
   void openWorkspaceDialog();

   /// @brief Create a new ST file under parentPath
   void createNewFile(const std::string& parentPath, const std::string& name);

   /// @brief Create a new folder under parentPath
   void createNewFolder(const std::string& parentPath, const std::string& name);

   /// @brief Delete the file or folder at path
   void deleteFile(const std::string& path);

   /// @brief Rename the entry at oldPath to newName
   void renameFile(const std::string& oldPath, const std::string& newName);

   /// @brief Move sourcePath into destDir
   void moveFile(const std::string& sourcePath, const std::string& destDir);

   /// @brief Record that the file this editor holds has moved on disk
   ///
   /// Only the path, never the panes: the editor is still showing the same file, and
   /// without this a save after a rename writes a second copy at the old path. The
   /// workspace index is invalidated for both paths, because it is keyed by path and
   /// the entry that described the file now answers to the other one.
   /// @param path Where the file now is
   void renameFileTo(const std::string& path);

   /// @brief Save the currently open file
   void saveCurrentFile();

   /**
    * @brief Whether the file has edits that are not on disk
    *
    * Kept here rather than asked of the editors, because the ST file is not one
    * buffer: it is the panes, and what they hold together is not what any single
    * one of them can answer.
    */
   bool hasUnsavedChanges() const { return m_isDirty; }

   /// @brief Open a file for editing (with method extraction)
   void openFile(const std::string& path);

   /// @brief Close the currently open ST file and clear the editor state
   void closeFile();

/// @brief Handle keyboard shortcuts
   bool handleKeyboardShortcuts();

   // ============================================================================
   // Editor and parsing methods
   // ============================================================================

   /// @brief Generate the full ST file content from the editor sections and methods
   std::string generateSTFile();

   /**
    * @brief Generate the ST file and record where each editor section landed
    * @param outSegments Receives one entry per editor-backed block
    * @return The generated file content
    *
    * This is the single source of truth for the generated layout: the segment
    * list is filled while the text is emitted, so it can never drift from what
    * generateSTFile() actually produces. Diagnostic and semantic mapping both
    * consume it instead of re-deriving offsets by hand.
    */
   std::string generateSTFileWithMap(std::vector<SourceSegment>& outSegments);

   /// @brief Re-run the st2cpp analyzer and push resolved symbol colors to the editors
   void refreshSemanticHighlighting();

   /// @brief Map a generated-file line to the editor that owns it
   const SourceSegment* segmentForLine(int fullLine) const;

   // ============================================================================
   // Go-to-declaration
   // ============================================================================

   /// @brief Extract the identifier surrounding a position in an editor
   static bool identifierAt(const TextEditor& editor, int line, int col, std::string& outName);

   /// @brief Resolve a name to the declaration it should navigate to
   const Declaration* findDeclaration(const std::string& name, const std::string& scope) const;

   /// @brief Column of a name inside a generated line, or -1 when absent
   static int columnOfName(const std::vector<std::string>& srcLines, int line, const std::string& name);

   /// @brief Move the editor to a generated-file line, switching METHOD tabs
   void revealGeneratedLine(int fullLine, int col, int tokenLength = 0);

   /**
    * @brief Take the open file out whole, to be put back later
    *
    * The other end of switching tabs. Everything the editors hold is read out,
    * including unsaved changes, because that is the only copy of them: reading the
    * file off disk instead would hand back the saved version and lose the rest.
    *
    * The AST, the semantic analysis and the source map are left out on purpose.
    * They are derived from the text and are rebuilt when the file is shown again,
    * so keeping them would mean carrying a second, stale copy of the truth.
    */
   STDocument takeDocument();

   /**
    * @brief Show a file taken out by takeDocument()
    * @param doc The file and its contents
    *
    * Refuses a document that is not a POU, rather than showing an empty editor:
    * a file that parses as nothing is a file worth looking at, and taking its
    * content on the way in is what it is worth looking at for.
    */
   void setDocument(const STDocument& doc);

   /// @brief Switch to a METHOD tab, materializing it if needed
   bool revealMethodTab(const std::string& methodName);

   /// @brief Ask the tab bar to show a tab on the next frame
   /// @param tabName METHOD name, or empty for the POU tab
   void requestTabSelection(const std::string& tabName);

   /// @brief Ask the ST document to show the "Add Method" dialog
   ///
   /// Meant for callers outside the Editor panel, such as the tree's context
   /// menu: the popup lives in the window the document is drawn in and a popup
   /// opened in another window is never submitted, so the request is carried over
   /// and opened there.
   void requestAddMethodDialog();

   /// @brief Take the pending "Add Method" request, if there is one
   /// @return true once per request, and clear the fields for the dialog
   bool consumeAddMethodDialogRequest();

   /// @brief Reveal a declaration, handling METHOD headers and parameters
   bool revealDeclaration(const Declaration& decl);

   /// @brief Handle Ctrl+Click and hover on one editor
   void handleNavigationInput(const char* id, TextEditor& editor);
   /// @brief Resolve a name queued by a cross-file Ctrl+Click and reveal it
   void applyPendingJump();

   /// @brief Show the resolved declaration of the identifier under the mouse
   void showIdentifierTooltip(TextEditor& editor);

   // ============================================================================
   // Unsaved-changes handling
   // ============================================================================

   /// @brief Poll the editors and update the dirty flag
   void updateDirtyState();

   /// @brief Open a file, asking first when there are unsaved changes
   void requestOpenFile(const std::string& path);

   /// @brief Open a file and land on one of its METHOD tabs
   void openMethodOf(const std::string& filePath, const std::string& methodName);

   /// @brief Render the save / discard confirmation
   void renderDirtyPrompt();

   /// @brief Let the user pick a target when a name has several declarations
   void renderJumpPopup();

   // ============================================================================
   // Workspace declaration index (cross-file navigation)
   // ============================================================================

   /// @brief A METHOD of a file in the workspace, for the file tree
   struct WorkspaceMethod
   {
      std::string name;
      std::string returnType;
      int line = 0;
   };

   /// @brief A declaration found somewhere in the workspace
   struct WorkspaceDeclaration
   {
      std::string file;  ///< Absolute path of the declaring file
      int line = 0;      ///< 1-based line in that file
      std::string scope; ///< Empty for POU level, else the METHOD name
      SymCategory category = SymCategory::Unresolved;
      std::string typeText;
      std::string kindText;
      std::string pouName;
   };

   /// @brief Build (or reuse) the index of declarations across the workspace
   void ensureWorkspaceIndex();

   /// @brief Build (or reuse) the library registry describing the sibling files
   /// @details The analyzer resolves an unknown named type by consulting its
   /// external scope, so exporting every other .st file as a semantic-only
   /// library is what makes a FUNCTION_BLOCK declared in a sibling file usable
   /// from the open one. Cached: parsing every file on each keystroke is not
   /// affordable.
   void ensureProjectRegistry();

   /// @brief Invalidate the workspace index after a file changed
   void invalidateWorkspaceIndex(const std::string& path);

   /// @brief All declarations of a name across the workspace
   std::vector<WorkspaceDeclaration> findWorkspaceDeclarations(const std::string& name) const;

   /// @brief Index the declarations of one .st file
   void indexSTFile(const std::string& path,
                    std::unordered_map<std::string, std::vector<WorkspaceDeclaration>>& outDecls) const;

   /// @brief Setup and configure the TextEditor widgets
   void setupEditors();

   /// @brief Validate and parse the current ST code (includes semantic analysis)
   void validateAndParse();

   /// @brief Compile the current ST code
   void compile();

   // ============================================================================
   // Error mapping helpers
   // ============================================================================

   /// @brief Count the number of lines in a text string
   static int countLines(const std::string& text);

   /// @brief Append a tagged line to the Output panel
   void addOutput(OutSeverity severity, std::string text);

   /// @brief Set error markers on the appropriate editors based on m_errors
   void setErrorMarkers();

   // ============================================================================
   // METHOD management (FUNCTION_BLOCK only)
   // ============================================================================

   /// @brief Render one Variables/Body split editor (shared by the POU tab and every method tab)
   void renderSplitEditor(TextEditor& varEditor,
                          TextEditor& bodyEditor,
                          float& splitterPos,
                          const char* varLabel,
                          const char* bodyLabel,
                          const char* varId,
                          const char* bodyId);

   /// @brief Get (creating on first use) the editor pair for a method tab
   MethodEditors& getOrCreateMethodEditors(MethodData& method);

   /// @brief Pull the live text out of every currently-open method editor back into m_methods
   void syncMethodEditorsToData();

   /// @brief Add a new, empty method and switch to its tab
   void addMethod(const std::string& name, const std::string& returnType);

   /// @brief Delete a method by name
   void deleteMethod(const std::string& name);

   /// @brief Find the index of a method by name, or -1 if not found
   int findMethodIndex(const std::string& name) const;

   /// @brief Reset all method-related state (called whenever the open file changes/closes)
   void resetMethodState();

private:
   // ============================================================================
   // Generated-source layout
   // ============================================================================

   /// @brief Resolve the TextEditor that backs a segment, or nullptr if closed
   TextEditor* editorForSegment(const SourceSegment& segment) const;
   // ============================================================================
   // Line mapping for methods
   // ============================================================================
   // Superseded by SourceSegment/m_sourceMap, which is filled while the file is
   // generated and therefore cannot drift from the text the parser saw.
   struct MethodLineInfo
   {
      std::string name;
      int startLine;
      int endLine;
   };

   // ============================================================================
   // Member variables
   // ============================================================================

   bool m_initialized = false;             ///< Initialization flag
   std::string m_workspacePath;            ///< Current workspace path
   FileNode m_rootNode;                    ///< Root of the file tree
   std::string m_currentFilePath;          ///< Path of the currently open file
   std::string m_currentFileContent;       ///< Content of the currently open file
   std::vector<OutputLine> m_outputLines; ///< Lines to display in the Output panel
   /// Whether the Output panel dumps the generated ST in full. Off by default: it
   /// is hundreds of lines and it buried the diagnostics under it.
   bool m_showGeneratedDump = false;
   /// Which severities the Output panel shows. A member, not a local of
   /// renderOutputPanel(), because a local is rebuilt on every frame and the
   /// checkbox that writes it would be writing into a copy that dies at the end of
   /// the frame: the box would flip, the panel would ignore it, and nothing would
   /// say so.
   OutputFilter m_outputFilter;
   /// What the Output panel is filtered by, as the user typed it. A member, and a
   /// fixed array rather than a std::string because ImGui 1.92.9's InputText takes
   /// a char buffer and this tree does not link imgui_stdlib, which is the wrapper
   /// that takes a std::string. A function-local static, which is what this was,
   /// also held its text and so appeared to work: it survived because it was static,
   /// not because the panel owned it, so a query typed for one project was still
   /// filtering the next one's log to nothing.
   char m_outputSearch[256] = {};
   /// Last line count the Output panel drew, so it can tell new output from old.
   size_t m_outputLinesShown = 0;

   std::unique_ptr<TextEditor> m_variablesEditor; ///< Editor for the Variables section
   std::unique_ptr<TextEditor> m_bodyEditor;      ///< Editor for the Body section

   std::string m_pouName;                ///< Name of the current POU
   POUType m_pouType = POUType::Program; ///< Type of the current POU

   std::unique_ptr<TranslationUnit> m_ast; ///< AST of the current file
   std::vector<ParseError> m_errors;       ///< Parsing errors

   std::vector<SourceSegment> m_sourceMap; ///< Layout of the last generated file

   std::vector<std::string> m_srcLines;     ///< Generated source, 1-based via index+1

   /// Cached analysis of the current AST, rebuilt by validateAndParse()
   std::unique_ptr<st2cpp::semantic::SemanticInfo> m_semantic;

   /// Declarations of the open file, keyed by normalized name
   DeclarationIndex m_declarations;

   // ============================================================================
   // Unsaved-changes state
   // ============================================================================

   bool m_showTooltips = false;   ///< Hover tooltips are opt-in: they interrupt reading
   bool m_isDirty = false;        ///< Editor text differs from the file on disk
   int m_ignoreChangeFrames = 0;  ///< Frames to skip after a programmatic load
   bool m_showDirtyPrompt = false;///< True while the confirmation popup is open
   std::string m_pendingOpenPath; ///< File queued to open once the user decides

   // ============================================================================
   // Workspace declaration index
   // ============================================================================

   std::unordered_map<std::string, std::vector<WorkspaceDeclaration>> m_workspaceDecls;
   bool m_workspaceIndexValid = false;

   /// Declarations of the sibling files, as the analyzer's external scope
   std::unique_ptr<st2cpp::library::LibraryRegistry> m_projectRegistry;

   /// METHODS of each indexed .st file, so the tree can show them
   mutable std::unordered_map<std::string, std::vector<WorkspaceMethod>> m_workspaceMethods;

   /// Tab to select once a pending file switch has completed
   std::string m_pendingTabAfterOpen;

   // ============================================================================
   // Pending cross-file navigation
   // ============================================================================

   std::string m_pendingJumpName; ///< Identifier to reveal after a file switch
   std::vector<WorkspaceDeclaration> m_jumpCandidates;
   std::string m_jumpCandidateName;
   bool m_showJumpPopup = false;  ///< True when a name is declared more than once

    // ============================================================================
    // METHOD management
    // ============================================================================

   std::vector<MethodData> m_methods; ///< List of methods
   std::string m_currentContext;      ///< Current context: "POU" or "METHOD:name"
   std::string m_pouVariablesText;    ///< Variables text for the main POU
   std::string m_pouBodyText;         ///< Body text for the main POU

   std::string m_activeTab;                                        ///< "" = POU tab (Variables/Body), otherwise the open method's name

   /// One-shot request to select a tab, honoured via ImGuiTabItemFlags_SetSelected
   std::string m_pendingTabSelection;                              ///< METHOD to select, empty for none
   bool m_pendingSelectPOU = false;                                ///< Request to select the POU tab
   std::unordered_map<std::string, MethodEditors> m_methodEditors; ///< Lazily-created editors, keyed by method name

   // ============================================================================
   // Popup states
   // ============================================================================

   bool m_showNewFilePopup = false;
   bool m_showNewFolderPopup = false;
   bool m_showRenamePopup = false;
   bool m_showNewPOUPopup = false;
   bool m_showAddMethodPopup = false;           ///< True if add method popup is visible
   bool m_addMethodDialogPending = false;       ///< Asked for from another panel; the document opens the popup

   // ============================================================================
   //  Member completion
   //
   //  Triggered by typing '.' after an identifier that denotes a function block
   //  instance. Typing keeps going to the editor, so the list filters itself as
   //  the prefix grows; only the navigation and confirmation keys are captured.
   // ============================================================================

   /// The editor the completion belongs to; the pointer is stable because
   /// editors are held by unique_ptr and never moved.
   TextEditor* m_completionEditor = nullptr;

   /// Candidates the pattern still matches, best match first
   std::vector<MemberAccess> m_completionCandidates;

   int m_completionSelected = 0;   ///< Index into m_completionCandidates
   std::string m_completionPrefix;  ///< Prefix the candidates were filtered with
   std::string m_completionObject;  ///< Instance name the '.' was typed after

   /// Whether the editor owning the list still holds the keyboard.
   ///
   /// Where the list was dismissed, so Escape keeps it closed instead of
   /// letting the next frame put it straight back where it was
   struct CompletionSpot
   {
      int line = -1;
      int column = -1;
   };
   /// Set for one frame when Tab asks for the parameters of the call being written.
  /// The editor it was asked for, since both are visited every frame and only the
  /// one holding the keyboard may answer it.
  TextEditor* m_parameterPickerEditor = nullptr;
  /// True while the open list is that parameter list rather than a member or
  /// identifier list, which decides what a key press means
  bool m_completionIsParameterList = false;
  CompletionSpot m_completionDismissed;
   bool m_completionDismissedValid = false;
   /// Line and column span the completion would replace, in editor coordinates
   int m_completionLine = -1;
   int m_completionColStart = 0;
   int m_completionColEnd = 0;

   /// Screen rectangle of the editor child that owns the list, plus where the
   /// cursor sits inside it, so the list opens under the glyph being typed
   /// rather than at the top-left of the pane
   /// Screen rectangle the list took this frame, set once the window exists and is
  /// really the size ImGui gave it. Empty corners mean it drew nothing, which a
  /// window left over from an earlier frame cannot tell you.
  ImVec2 m_completionRectMin = ImVec2(0.0f, 0.0f);
  ImVec2 m_completionRectMax = ImVec2(0.0f, 0.0f);
  ImVec2 m_completionEditorMin = ImVec2(0.0f, 0.0f);
   ImVec2 m_completionEditorMax = ImVec2(0.0f, 0.0f);
   ImVec2 m_completionCursorScreen = ImVec2(0.0f, 0.0f);

   /// First candidate row on screen. Kept in step with the highlight while
   /// arrowing, and moved by the wheel.
   int m_completionScroll = 0;

   /// Rows the last frame had room for, so navigation knows a page
   int m_completionPageSize = 8;

   void updateMemberCompletion(const char* id, TextEditor& editor, bool editorHasKeyboard);
   void clearMemberCompletion();
   void dismissMemberCompletion();
   void moveMemberCompletion(int delta);
   void moveMemberCompletionByPage(int pages);
   void acceptMemberCompletion();
   void renderMemberCompletion();
   /// @return the key the list took, or ImGuiKey_None
   ImGuiKey handleMemberCompletionKeys();

   /// The METHOD whose body a line sits in, empty for POU-level code
   std::string methodScopeAtLine(int line) const;

   /// The scope an identifier on a line resolves from, 0 when nothing is
   st2cpp::semantic::ScopeId scopeIdForLine(int line) const;
   // ============================================================================
   //  Signature help
   //
   //  The parameter list of the call the cursor is inside, with the argument
   //  being written highlighted. Recomputed from the text like the member list,
   //  so it follows the cursor across a multi-line call and closes at the ')'.
   // ============================================================================

   TextEditor* m_signatureEditor = nullptr;
   CallSignature m_signature;   ///< The callee the cursor is inside
   CallSite m_signatureCall;    ///< Where that call is, for its label
   int m_signatureArgument = 0; ///< Index of the argument the cursor is in

   CompletionSpot m_signatureDismissed;
   bool m_signatureDismissedValid = false;

   /// Screen rectangle the signature help took this frame, so a list anchored to
  /// the same cursor can step around it instead of covering it. Two corners
  /// rather than an ImRect, which lives in an internal header this one does not
  /// need. Equal corners mean nothing was on screen.
  ImVec2 m_signatureRectMin = ImVec2(0.0f, 0.0f);
  ImVec2 m_signatureRectMax = ImVec2(0.0f, 0.0f);
  ImVec2 m_signatureCursorScreen = ImVec2(0.0f, 0.0f);
   ImVec2 m_signatureEditorMin = ImVec2(0.0f, 0.0f);
   ImVec2 m_signatureEditorMax = ImVec2(0.0f, 0.0f);

   /// True when the cursor sits at an empty argument, which is what the inline
   /// parameter-name hint is drawn for
   bool m_signatureArgumentEmpty = false;

   void updateSignatureHelp(const char* id, TextEditor& editor, bool editorHasKeyboard);
   void clearSignatureHelp();
   void dismissSignatureHelp();
   void renderSignatureHelp();
   /// @return the key the signature took, or ImGuiKey_None
   ImGuiKey handleSignatureHelpKeys();

   /// Draw the parameter name and type in place after the cursor, the way an
   /// editor hints at the next thing to type
   void renderParameterNameHint(const char* id, TextEditor& editor);

   /// @brief Run st2cpp over the open project and report what it says
   void runTranspiler();

   // ============================================================================
   //  Statement completion
   //
   //  Typing the first letters of a statement offers the statements that start
   //  that way, and accepting one writes the whole skeleton with the caret on the
   //  part still to be filled in.
   //
   //  The state is kept apart from the member completion's. The two can be
   //  answering about the same word and only one of them can be on screen, so
   //  each has to be able to tell that the other is up.
   // ============================================================================

   TextEditor* m_snippetEditor = nullptr;   ///< Editor the list belongs to

   std::vector<StatementSnippet> m_snippetCandidates; ///< Matching skeletons, best first

   /// One row per candidate, a byte per label character, 1 where the pattern matched
   std::vector<std::vector<int>> m_snippetMatchMask;

   int m_snippetSelected = 0;         ///< Index into m_snippetCandidates
   int m_snippetScroll = 0;           ///< First candidate row on screen
   std::string m_snippetPrefix;       ///< What the candidates were filtered with
   bool m_snippetInVariables = false; ///< Pane the list was built for

   /// Line and column span the list would replace, in editor coordinates
   int m_snippetLine = -1;
   int m_snippetColStart = 0;
   int m_snippetColEnd = 0;

   CompletionSpot m_snippetDismissed;
   bool m_snippetDismissedValid = false;

   ImVec2 m_snippetRectMin = ImVec2(0.0f, 0.0f);
   ImVec2 m_snippetRectMax = ImVec2(0.0f, 0.0f);
   ImVec2 m_snippetCursorScreen = ImVec2(0.0f, 0.0f);
   ImVec2 m_snippetEditorMin = ImVec2(0.0f, 0.0f);
   ImVec2 m_snippetEditorMax = ImVec2(0.0f, 0.0f);

   void updateStatementCompletion(const char* id, TextEditor& editor, bool editorHasKeyboard, bool inVariables);
   void clearStatementCompletion();
   void dismissStatementCompletion();
   void moveStatementCompletion(int delta);
   void moveStatementCompletionByPage(int pages);
   void acceptStatementCompletion();
   void renderStatementCompletion();
   /// @return the key the list took, or ImGuiKey_None
   ImGuiKey handleStatementCompletionKeys();

   /// @brief True while a statement list is up, which the member list defers to
   bool statementCompletionOpen() const { return m_snippetEditor != nullptr && !m_snippetCandidates.empty(); }

   /// @brief Drop the semantic spans the editors are painting
   void invalidateSemanticTokens();

   /// Window flags for the member completion list.
   ///
   /// The list is drawn as a plain overlay rather than a real popup, and it
   /// never takes the keyboard: the editor keeps it and keeps receiving the
   /// characters that filter the list, which is what makes the list feel like
   /// part of the text rather than a dialog that stole it. NoFocusOnAppearing
   /// and NoNavFocus are what keep that true — the editors are BeginChild
   /// windows, and TextEditor::HandleKeyboardInputs only claims
   /// io.WantTextInput when its own child is focused, so a list that took focus
   /// the moment it opened would strip the keyboard from the editor underneath.
   ///
   /// The overlay is therefore given a border and a background explicitly, since
   /// a popup-window background would be drawn only for a window the user is
   /// interacting with.
   static constexpr ImGuiWindowFlags kMemberCompletionWindowFlags =
       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNavFocus |
       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

   /// Same flags for the signature help, which is the same kind of overlay
   static constexpr ImGuiWindowFlags kSignatureHelpWindowFlags = kMemberCompletionWindowFlags;

  /// @brief Claim the navigation keys for whichever overlay is open
  ///
  /// A suggestion list owns the arrow keys and the two page keys while it is up,
  /// and ImGui's keyboard navigation has to be blind to them for the same period:
  /// it turns an arrow press into a request to move the focus to another window,
  /// and the window it lands on is scrolled into view, which drags the panel out
  /// from under the popup.
  ///
  /// Ownership is the mechanism rather than clearing ImGuiConfigFlags_NavEnableKeyboard,
  /// which is a flag on the whole application and not this editor's to stand down:
  /// the keys are taken one at a time, and only while a list is up. NavProcessKey()
  /// asks for them with ImGuiKeyOwner_NoOwner, so a key that has an owner is not
  /// seen by it at all, while the list's own IsKeyPressed() — which asks with
  /// ImGuiKeyOwner_Any — keeps working.
  ///
  /// Nothing is released here. Ownership is given up by ImGui on the frame after
  /// the key comes up, and claiming a key the list is no longer using would take
  /// it from whatever does.
  void claimOverlayNavigationKeys();

  /// @brief True while any of the three overlays is on screen
  bool anyOverlayOpen() const;

  /// @brief Fill the completion list with the parameters of the call at the caret
  /// @return True when there is such a call and it has parameters to choose from
  ///
  /// The one thing the list can offer that the text at the caret cannot: what the
  /// callee is called, written out as names. The parameters already written to the
  /// left of the caret are marked and pushed down, so Tab walks the call in the
  /// order it was declared rather than in the order the user remembers it.
  bool buildParameterPicker(TextEditor& editor, const std::vector<std::string>& lines,
                            const TextEditor::Coordinates& cursor);

   bool m_showDeleteMethodConfirmation = false; ///< True if delete method confirmation is visible
   POUType m_newPOUType = POUType::Program;
   std::string m_newPOUName;
   std::string m_newItemName;
   std::string m_newItemParent;
   std::string m_renamePath;
   float m_splitterPos = 0.5f;      ///< Where the Variables and Body sections are divided
   bool m_splitterDirty = false;     ///< It has been dragged and not yet written out
   std::string m_functionReturnType;

   // Method creation popup state
   std::string m_newMethodName;
   std::string m_newMethodReturnType;
   std::string m_newMethodVisibility;
   std::string m_methodToDelete;

   // ============================================================================
   // Delete confirmation state
   // ============================================================================

   bool m_showDeleteConfirmation = false; ///< True if delete confirmation popup is visible

   /// Fallback workspace picker, for when neither tinyfiledialogs nor zenity is
   /// available. A modal of the app's own, so it has to be drawn by the panel.
   bool m_showWorkspaceDialog = false;
   char m_workspaceDialogPath[1024] = {};
   std::string m_deletePendingPath;       ///< Path of the file/folder pending deletion

   // ============================================================================
   // Drag and drop state
   // ============================================================================

   std::string m_draggedItemPath;
   bool m_isDragging = false;
   std::string m_dropTargetPath;
   bool m_showDropIndicator = false;
};

} // namespace ST
} // namespace undoApp