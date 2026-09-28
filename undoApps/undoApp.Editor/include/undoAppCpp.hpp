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

   /// @brief Write the editor's current text back to m_currentFilePath
   void saveFile();

   /// @brief Clear the editor and forget the current file
   void closeFile();

   /// @brief Render the editor inside the unified "Editor" ImGui window
   void renderEditorPanel();

   /// @brief Render the output panel
   void renderOutputPanel();

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
};

} // namespace undoApp