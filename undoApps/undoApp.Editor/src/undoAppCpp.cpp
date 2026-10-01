/**
 * @file undoAppCpp.cpp
 * @brief Implementation of the C/C++ editor backend
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 *
 * The C/C++ backend used by undoApp.Editor for files with extensions:
 * .c, .cpp, .cc, .cxx (C++ source)
 * .h, .hpp, .hh, .hxx (C/C++ header)
 *
 * Uses the built-in C++ language definition from ImGuiColorTextEdit
 * for syntax highlighting.
 */

#include "undoAppCpp.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"

#include <imgui.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace undoApp {

// ============================================================================
// Singleton Instance
// ============================================================================

CppApp& CppApp::getInstance()
{
   static CppApp instance;
   return instance;
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

bool CppApp::initialize()
{
   if (m_initialized) {
      return true;
   }
   std::cout << "[undoApp.Cpp] Initializing..." << std::endl;
   setupEditor();
   m_initialized = true;
   std::cout << "[undoApp.Cpp] Initialization complete" << std::endl;
   return true;
}

void CppApp::shutdown()
{
   if (!m_initialized) {
      return;
   }
   std::cout << "[undoApp.Cpp] Shutting down..." << std::endl;
   m_editor.reset();
   m_currentFilePath.clear();
   m_initialized = false;
   std::cout << "[undoApp.Cpp] Shutdown complete" << std::endl;
}

// ============================================================================
// File operations
// ============================================================================

void CppApp::openFile(const std::string& path)
{
   if (!m_editor) {
      setupEditor();
   }

   std::ifstream file(path);
   if (!file.is_open()) {
      std::cerr << "[undoApp.Cpp] Failed to open file: " << path << std::endl;
      m_outputLines.push_back("Error: could not open " + path);
      return;
   }

   std::stringstream buffer;
   buffer << file.rdbuf();
   std::string content = buffer.str();
   file.close();

   m_editor->SetText(content);
   m_currentFilePath = path;
   m_editor->SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
   m_editor->SetText(m_editor->GetText());
   // What is on screen is what the file holds, so the tab's unsaved mark goes with
   // the load rather than waiting for the next edit to contradict it.
   m_isDirty = false;

   m_outputLines.push_back("Loaded: " + path);
   std::cout << "[undoApp.Cpp] Opened: " << path << std::endl;
}

void CppApp::renameFileTo(const std::string& path)
{
   if (path.empty() || path == m_currentFilePath) {
      return;
   }
   m_currentFilePath = path;
}

void CppApp::saveFile()
{
   if (m_currentFilePath.empty()) {
      m_outputLines.push_back("Error: no file is open");
      return;
   }
   if (!m_editor) {
      m_outputLines.push_back("Error: editor not initialized");
      return;
   }

   std::string text = m_editor->GetText();
   std::ofstream file(m_currentFilePath);
   if (!file.is_open()) {
      std::cerr << "[undoApp.Cpp] Failed to write: " << m_currentFilePath << std::endl;
      m_outputLines.push_back("Error: could not write " + m_currentFilePath);
      return;
   }
   file << text;
   file.close();
   // Written, so the two agree again. Only after the file opened: a save that failed
   // leaves the mark alone.
   m_isDirty = false;
   m_outputLines.push_back("Saved: " + m_currentFilePath);
   std::cout << "[undoApp.Cpp] Saved: " << m_currentFilePath << std::endl;
}

void CppApp::closeFile()
{
   m_currentFilePath.clear();
   m_isDirty = false;
   if (m_editor) {
      m_editor->SetText("");
   }
   m_outputLines.push_back("Closed current file");
}

void CppApp::setDocument(const std::string& path, const std::string& text)
{
   if (!m_editor) {
      setupEditor();
   }
   m_currentFilePath = path;
   // SetText marks the editor as changed, which is our own doing here: the text
   // came from this editor on the way out and is going back into it.
   if (m_editor) {
      m_editor->SetText(text);
   }
}

void CppApp::document(std::string& path, std::string& text) const
{
   path = m_currentFilePath;
   text = m_editor ? m_editor->GetText() : std::string();
}

// ============================================================================
// Editor setup
// ============================================================================

std::string CppApp::getLanguageLabel(const std::string& path) const
{
   std::string ext = fs::path(path).extension().string();
   if (ext == ".c" || ext == ".h") {
      return "C";
   } else if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".hpp" || ext == ".hh" || ext == ".hxx") {
      return "C++";
   }
   return "C/C++";
}

void CppApp::setupEditor()
{
   m_editor = std::make_unique<TextEditor>();

   // Use the built-in C++ language definition for syntax highlighting
   auto cppLang = TextEditor::LanguageDefinition::CPlusPlus();
   m_editor->SetLanguageDefinition(cppLang);

   // Setup dark palette matching undoStudio theme
   TextEditor::Palette palette;
   palette[(int) TextEditor::PaletteIndex::Default] = IM_COL32(230, 230, 230, 255);
   palette[(int) TextEditor::PaletteIndex::Keyword] = IM_COL32(204, 128, 255, 255);
   palette[(int) TextEditor::PaletteIndex::Number] = IM_COL32(255, 179, 51, 255);
   palette[(int) TextEditor::PaletteIndex::String] = IM_COL32(153, 255, 153, 255);
   palette[(int) TextEditor::PaletteIndex::CharLiteral] = IM_COL32(153, 255, 153, 255);
   palette[(int) TextEditor::PaletteIndex::Punctuation] = IM_COL32(255, 255, 255, 255);
   palette[(int) TextEditor::PaletteIndex::Preprocessor] = IM_COL32(179, 179, 179, 255);
   palette[(int) TextEditor::PaletteIndex::Identifier] = IM_COL32(204, 230, 255, 255);
   palette[(int) TextEditor::PaletteIndex::KnownIdentifier] = IM_COL32(204, 230, 255, 255);
   palette[(int) TextEditor::PaletteIndex::Comment] = IM_COL32(102, 179, 102, 255);
   palette[(int) TextEditor::PaletteIndex::MultiLineComment] = IM_COL32(102, 179, 102, 255);
   palette[(int) TextEditor::PaletteIndex::Background] = IM_COL32(13, 20, 38, 255);
   palette[(int) TextEditor::PaletteIndex::Cursor] = IM_COL32(0, 204, 255, 255);
   palette[(int) TextEditor::PaletteIndex::Selection] = IM_COL32(51, 77, 128, 204);
   palette[(int) TextEditor::PaletteIndex::ErrorMarker] = IM_COL32(255, 51, 51, 255);
   palette[(int) TextEditor::PaletteIndex::LineNumber] = IM_COL32(128, 140, 160, 255);
   palette[(int) TextEditor::PaletteIndex::CurrentLineFill] = IM_COL32(0, 180, 216, 40);
   palette[(int) TextEditor::PaletteIndex::CurrentLineEdge] = IM_COL32(0, 180, 216, 80);
   palette[(int) TextEditor::PaletteIndex::PreprocIdentifier] = IM_COL32(179, 179, 179, 255);
   palette[(int) TextEditor::PaletteIndex::Breakpoint] = IM_COL32(255, 0, 0, 255);
   palette[(int) TextEditor::PaletteIndex::CurrentLineFillInactive] = IM_COL32(0, 0, 0, 0);

   m_editor->SetPalette(palette);
   m_editor->SetShowWhitespaces(false);
   m_editor->SetTabSize(3);
}

// ============================================================================
// Panel rendering
// ============================================================================

void CppApp::renderEditorPanel()
{
   if (m_currentFilePath.empty()) {
      ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No file open");
      return;
   }

   // Toolbar
   ImGui::Text("File: %s", fs::path(m_currentFilePath).filename().string().c_str());
   ImGui::SameLine();
   ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), "| %s", getLanguageLabel(m_currentFilePath).c_str());

   ImGui::Separator();

   if (ImGui::Button("Save")) {
      saveFile();
   }
   ImGui::SameLine();
   if (ImGui::Button("Close")) {
      closeFile();
   }
   ImGui::Separator();

   if (m_editor) {
      m_editor->Render("##cppEditor");
      // Sampled after the render, because the editor raises its flag while it draws
      // and clears it at the start of the next one.
if (m_editor->IsTextChanged()) {
         m_isDirty = true;
      }
   }
}

} // namespace undoApp