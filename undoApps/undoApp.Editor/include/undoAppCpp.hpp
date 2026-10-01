/**
 * @file undoAppCpp.hpp
 * @brief Header of the C/C++ editor backend
 * @ingroup undoapps
 *
 * The C/C++ backend used by undoApp.Editor for files with extensions:
 * .c, .cpp, .cc, .cxx (C++ source)
 * .h, .hpp, .hh, .hxx (C/C++ header)
 *
 * Uses the built-in C++ language definition from ImGuiColorTextEdit
 * for syntax highlighting.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <string>
#include <vector>
#include <memory>

// Include TextEditor
#include <TextEditor.h>

namespace undoApp {

/**
 * @brief Main application class for the C/C++ editor backend
 *
 * This is one of the backends owned by undoApp::Editor::EditorApp.
 * It is not registered as an ImGui panel on its own: the EditorApp
 * routes its single "Editor" panel here when the current file is
 * a C or C++ source or header file.
 */
class CppApp
{
public:
   /// @brief Get the singleton instance of CppApp
   static CppApp& getInstance();

   /// @brief Initialize the C/C++ editor (idempotent)
   bool initialize();

   /// @brief Shutdown the C/C++ editor
   void shutdown();

   /// @brief Open a file and load it into the editor
   void openFile(const std::string& path);

   /// @brief Record that the file this editor holds has moved on disk
   ///
   /// Only the path, never the text: the editor is still showing the same file, and
   /// without this a save after a rename writes a second copy at the old path.
   /// @param path Where the file now is
   void renameFileTo(const std::string& path);

   /// @brief Write the editor's current text back to m_currentFilePath
   void saveFile();

   /// @brief Clear the editor and forget the current file
   void closeFile();

   /// @brief Render the editor inside the unified "Editor" ImGui window
   void renderEditorPanel();

   /// @brief Render the output panel
   void renderOutputPanel();

   /**
    * @brief Put what is on screen back into an editor, under a new file name
    *
    * The two ends of switching tabs: a backend holds one file, so several open
    * means taking the text out before the next is loaded and putting it back when
    * this one is shown again. The path comes separately from the text because it
    * is where a later save goes, and re-reading it from disk would lose anything
    * not written yet.
    *
    * @param path The file the text belongs to
    * @param text The text as it is now, unsaved changes included
    */
   void setDocument(const std::string& path, const std::string& text);

   /// @brief The editor's text and the file it belongs to
   void document(std::string& path, std::string& text) const;

   /**
    * @brief Whether the text on screen differs from the file on disk
    *
    * Sticky for the same reason as the text backend's: the editor's own flag is
    * cleared by the next Render(), and an answer that expires cannot be what a
    * tab's unsaved mark is drawn from.
    */
   bool hasUnsavedChanges() const { return m_isDirty; }

private:
   /// @brief (Re)create the TextEditor widget with C++ language definition
   void setupEditor();

   /// @brief Get the language label based on file extension
   std::string getLanguageLabel(const std::string& path) const;

   bool m_initialized = false;
   std::string m_windowTitle = "Editor";
   std::string m_currentFilePath;
   std::unique_ptr<TextEditor> m_editor;
   std::vector<std::string> m_outputLines;
   bool m_isDirty = false;  ///< Editor text differs from the file on disk
};

} // namespace undoApp