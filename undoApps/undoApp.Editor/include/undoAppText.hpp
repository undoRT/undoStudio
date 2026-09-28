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

private:
   /// @brief (Re)create the TextEditor widget with the standard dark palette
   void setupEditor();

   bool m_initialized = false;
   std::string m_windowTitle = "Editor";
   std::string m_currentFilePath;
   std::unique_ptr<TextEditor> m_editor;
   std::vector<std::string> m_outputLines;
};

} // namespace undoApp
