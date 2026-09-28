/**
 * @file undoAppText.cpp
 * @brief Implementation of the plain-text editor undoApp
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

#include "undoAppText.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"

#include <imgui.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace undoApp {

// ============================================================================
// Singleton Instance
// ============================================================================

TextApp& TextApp::getInstance()
{
   static TextApp instance;
   return instance;
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

bool TextApp::initialize()
{
   if (m_initialized) {
      return true;
   }
   std::cout << "[undoApp.Text] Initializing..." << std::endl;
   setupEditor();
   m_initialized = true;
   std::cout << "[undoApp.Text] Initialization complete" << std::endl;
   return true;
}

void TextApp::shutdown()
{
   if (!m_initialized) {
      return;
   }
   std::cout << "[undoApp.Text] Shutting down..." << std::endl;
   m_editor.reset();
   m_currentFilePath.clear();
   m_initialized = false;
   std::cout << "[undoApp.Text] Shutdown complete" << std::endl;
}

// ============================================================================
// File operations
// ============================================================================

void TextApp::openFile(const std::string& path)
{
   if (!m_editor) {
      setupEditor();
   }

   std::ifstream file(path);
   if (!file.is_open()) {
      std::cerr << "[undoApp.Text] Failed to open file: " << path << std::endl;
      m_outputLines.push_back("Error: could not open " + path);
      return;
   }

   std::stringstream buffer;
   buffer << file.rdbuf();
   std::string content = buffer.str();
   file.close();

   m_editor->SetText(content);
   m_currentFilePath = path;
   m_outputLines.push_back("Loaded: " + path);

   std::cout << "[undoApp.Text] Opened: " << path << std::endl;
}

void TextApp::saveFile()
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
      std::cerr << "[undoApp.Text] Failed to write: " << m_currentFilePath << std::endl;
      m_outputLines.push_back("Error: could not write " + m_currentFilePath);
      return;
   }
   file << text;
   file.close();
   m_outputLines.push_back("Saved: " + m_currentFilePath);
   std::cout << "[undoApp.Text] Saved: " << m_currentFilePath << std::endl;
}

void TextApp::closeFile()
{
   m_currentFilePath.clear();
   if (m_editor) {
      m_editor->SetText("");
   }
   m_outputLines.push_back("Closed current file");
}

// ============================================================================
// Editor setup
// ============================================================================

void TextApp::setupEditor()
{
   m_editor = std::make_unique<TextEditor>();

   // Setup dark palette with light text (matching undoStudio theme)
   TextEditor::Palette palette;
   palette[(int) TextEditor::PaletteIndex::Default] = IM_COL32(230, 230, 230, 255); // Light gray text
   palette[(int) TextEditor::PaletteIndex::Keyword] = IM_COL32(204, 128, 255, 255); // Purple
   palette[(int) TextEditor::PaletteIndex::Number] = IM_COL32(255, 179, 51, 255);   // Orange
   palette[(int) TextEditor::PaletteIndex::String] = IM_COL32(153, 255, 153, 255);  // Light green
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
   palette[(int) TextEditor::PaletteIndex::ErrorMarker] = IM_COL32(255, 51, 51, 255);    // Red
   palette[(int) TextEditor::PaletteIndex::LineNumber] = IM_COL32(128, 140, 160, 255);   // Grey-blue
   palette[(int) TextEditor::PaletteIndex::CurrentLineFill] = IM_COL32(0, 180, 216, 40); // Transparent cyan
   palette[(int) TextEditor::PaletteIndex::CurrentLineEdge] = IM_COL32(0, 180, 216, 80); // Slightly more visible

   m_editor->SetPalette(palette);
   m_editor->SetShowWhitespaces(false);
   m_editor->SetTabSize(3);
}

// ============================================================================
// Panel rendering
// ============================================================================

void TextApp::renderEditorPanel()
{
   if (!ImGui::Begin(m_windowTitle.c_str())) {
      ImGui::End();
      return;
   }

   if (m_currentFilePath.empty()) {
      ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No file open");
      ImGui::End();
      return;
   }

   // Toolbar
   ImGui::Text("File: %s", fs::path(m_currentFilePath).filename().string().c_str());
   ImGui::SameLine();
   ImGui::TextColored(ImVec4(0.0f, 0.7f, 1.0f, 1.0f), "| %s", fs::path(m_currentFilePath).extension().string().c_str());

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
      m_editor->Render("##textEditor");
   }

   ImGui::End();
}

void TextApp::renderOutputPanel()
{
   if (!ImGui::Begin(m_windowTitle.c_str())) {
      ImGui::End();
      return;
   }
   ImGui::BeginChild("TextOutputLog", ImVec2(-1.0f, -1.0f), true);
   for (const auto& line : m_outputLines) {
      if (line.find("Error") != std::string::npos) {
         ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", line.c_str());
      } else if (line.find("Saved") != std::string::npos || line.find("Loaded") != std::string::npos) {
         ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", line.c_str());
      } else {
         ImGui::Text("%s", line.c_str());
      }
   }
   ImGui::EndChild();
   ImGui::End();
}

} // namespace undoApp
