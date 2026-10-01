/**
 * @file undoAppText.hpp
 * @brief Header of the plain-text editor undoApp
 * @ingroup undoapps
 *
 * The fallback backend used by undoApp.Editor when a file has an
 * extension that is neither `.st` nor `.json`. Provides a single
 * TextEditor with no syntax highlighting and a Save button.
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
 * @brief Main application class for the plain-text editor backend
 *
 * This is one of three backends owned by undoApp::Editor::EditorApp.
 * It is not registered as an ImGui panel on its own: the EditorApp
 * routes its single "Editor" panel here when the current file is
 * plain text.
 */
class TextApp
{
public:
   /// @brief Get the singleton instance of TextApp
   static TextApp& getInstance();

   /// @brief Initialize the plain-text editor (idempotent)
   bool initialize();

   /// @brief Shutdown the plain-text editor
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

   /// @brief Window title used by the unified "Editor" panel
   const char* getWindowTitle() const { return m_windowTitle.c_str(); }

   /// @brief Render the editor inside the unified "Editor" ImGui window
   void renderEditorPanel();

   /// @brief Render the (currently unused) output panel
   void renderOutputPanel();

   /**
    * @brief Put what is on screen back into an editor, under a new file name
    *
    * The two ends of switching tabs. A backend holds one file, so several open
    * means taking the text out of this one before the next is loaded and putting
    * it back when this one is shown again — otherwise a tab that is still open
    * would come back showing the file that replaced it.
    *
    * The path is separate from the text because it is where a later save goes,
    * and reading it back off disk would lose anything not written yet.
    *
    * @param path   The file the text belongs to
    * @param text   The text as it is now, unsaved changes included
    */
   void setDocument(const std::string& path, const std::string& text);

   /// @brief The editor's text and the file it belongs to
   void document(std::string& path, std::string& text) const;

   /**
    * @brief Whether the text on screen differs from the file on disk
    *
    * Sticky rather than read from the editor each time it is asked, because
    * TextEditor::IsTextChanged() is a one-shot flag that the next Render() clears.
    * That is what a tab's unsaved mark is answered from, so an answer that expired
    * before the mark was drawn would be an answer that changes on its own.
    */
   bool hasUnsavedChanges() const { return m_isDirty; }

private:
   /// @brief (Re)create the TextEditor widget with the standard dark palette
   void setupEditor();

   bool m_initialized = false;
   std::string m_windowTitle = "Editor";
   std::string m_currentFilePath;
   std::unique_ptr<TextEditor> m_editor;
   std::vector<std::string> m_outputLines;
   bool m_isDirty = false;  ///< Editor text differs from the file on disk
};

} // namespace undoApp
