/**
 * @file undoAppJSON.cpp
 * @brief Implementation of the JSON Viewer undoApp
 * @ingroup undoapps
 *
 * This file implements a JSON file viewer plugin for undoStudio.
 * It provides:
 * - Tree-based visualization of JSON data
 * - Expand/collapse all nodes
 * - Search/filter functionality
 * - Color-coded values (strings, numbers, booleans, null)
 * - Pretty print and raw view toggle
 * - Native file browser integration
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#include "undoAppJSON.hpp"
#include "undoStudio/ui/ImGuiManager.hpp"
#include "undoStudio/core/Application.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <functional>
#include <chrono>

// Include nlohmann/json - it's header-only
#include <nlohmann/json.hpp>

#ifdef USE_TINYFILEDIALOGS
#include <tinyfiledialogs.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace undoApp {
namespace JSON {

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Recursively count the number of nodes in a JSON tree
 * @param node The node to count
 * @return Total number of nodes in the subtree
 */
static int countNodes(const JSONNode* node)
{
   if (!node) {
      return 0;
   }
   int count = 1;
   for (const auto& child : node->children) {
      count += countNodes(child.get());
   }
   return count;
}

/**
 * @brief Build a JSON tree from a nlohmann::json object
 * @param j The JSON value to process
 * @param node The node to populate
 * @param depth Current depth in the tree
 */
static void buildJSONTreeFromJson(const json& j, std::unique_ptr<JSONNode>& node, int depth = 0)
{
   node = std::make_unique<JSONNode>();
   node->isObject = j.is_object();
   node->isArray = j.is_array();
   node->depth = depth;
   node->expanded = depth < 2; // Auto-expand first two levels

   if (j.is_object()) {
      for (auto& [key, value] : j.items()) {
         auto child = std::make_unique<JSONNode>();
         child->key = key;
         child->depth = depth + 1;
         child->expanded = child->depth < 2;

         if (value.is_object()) {
            child->isObject = true;
            child->value = "{" + std::to_string(value.size()) + "}";
            buildJSONTreeFromJson(value, child, depth + 1);
         } else if (value.is_array()) {
            child->isArray = true;
            child->value = "[" + std::to_string(value.size()) + "]";
            buildJSONTreeFromJson(value, child, depth + 1);
         } else if (value.is_string()) {
            child->value = "\"" + value.get<std::string>() + "\"";
         } else if (value.is_number_integer()) {
            child->value = std::to_string(value.get<int>());
         } else if (value.is_number_float()) {
            child->value = std::to_string(value.get<double>());
         } else if (value.is_number()) {
            child->value = std::to_string(value.get<double>());
         } else if (value.is_boolean()) {
            child->value = value.get<bool>() ? "true" : "false";
         } else if (value.is_null()) {
            child->value = "null";
         } else {
            child->value = "?";
         }
         node->children.push_back(std::move(child));
      }
   } else if (j.is_array()) {
      int idx = 0;
      for (const auto& value : j) {
         auto child = std::make_unique<JSONNode>();
         child->key = "[" + std::to_string(idx++) + "]";
         child->depth = depth + 1;
         child->expanded = child->depth < 2;

         if (value.is_object()) {
            child->isObject = true;
            child->value = "{" + std::to_string(value.size()) + "}";
            buildJSONTreeFromJson(value, child, depth + 1);
         } else if (value.is_array()) {
            child->isArray = true;
            child->value = "[" + std::to_string(value.size()) + "]";
            buildJSONTreeFromJson(value, child, depth + 1);
         } else if (value.is_string()) {
            child->value = "\"" + value.get<std::string>() + "\"";
         } else if (value.is_number_integer()) {
            child->value = std::to_string(value.get<int>());
         } else if (value.is_number_float()) {
            child->value = std::to_string(value.get<double>());
         } else if (value.is_number()) {
            child->value = std::to_string(value.get<double>());
         } else if (value.is_boolean()) {
            child->value = value.get<bool>() ? "true" : "false";
         } else if (value.is_null()) {
            child->value = "null";
         } else {
            child->value = "?";
         }
         node->children.push_back(std::move(child));
      }
   } else {
      // Primitive at root (should not happen often)
      node->value = j.dump();
   }
}

// ============================================================================
// Singleton Instance
// ============================================================================

JSONApp& JSONApp::getInstance()
{
   static JSONApp instance;
   return instance;
}

// ============================================================================
// Initialization / Shutdown
// ============================================================================

bool JSONApp::initialize()
{
    if (m_initialized) {
       return true;
    }

    std::cout << "[undoApp.JSON] Initializing..." << std::endl;
    registerPanels();
    m_initialized = true;
    std::cout << "[undoApp.JSON] Initialization complete" << std::endl;
    return true;
}

void JSONApp::shutdown()
{
   if (!m_initialized) {
      return;
   }
   std::cout << "[undoApp.JSON] Shutting down..." << std::endl;
   m_initialized = false;
   std::cout << "[undoApp.JSON] Shutdown complete" << std::endl;
}

void JSONApp::registerPanels()
{
   std::cout << "[undoApp.JSON] Panels registration skipped (using unified Editor panel)" << std::endl;
}

// ============================================================================
// JSON Tree Building
// ============================================================================

void JSONApp::buildJSONTree(const std::string& jsonString)
{
   m_jsonRoot.reset();

   try {
      json j = json::parse(jsonString);
      buildJSONTreeFromJson(j, m_jsonRoot);
      m_rawJSONString = jsonString;
      m_outputLines.push_back("JSON parsed successfully");
   } catch (const json::parse_error& e) {
      m_outputLines.push_back("Error parsing JSON: " + std::string(e.what()));
   } catch (const std::exception& e) {
      m_outputLines.push_back("Error parsing JSON: " + std::string(e.what()));
   }
}

// ============================================================================
// JSON Tree Rendering
// ============================================================================

ImVec4 JSONApp::getValueColor(const std::string& value)
{
   if (value.empty()) {
      return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
   }

   // String: green
   if (value.front() == '"' && value.back() == '"') {
      return ImVec4(0.6f, 0.9f, 0.6f, 1.0f);
   }

   // Boolean: purple
   if (value == "true" || value == "false") {
      return ImVec4(0.8f, 0.6f, 1.0f, 1.0f);
   }

   // Null: gray
   if (value == "null") {
      return ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
   }

   // Number: orange
   if (!value.empty() && (std::isdigit(value[0]) || value[0] == '-' || value[0] == '.')) {
      // Check if it's a number (allow dots and scientific notation)
      bool isNumber = true;
      for (char c : value) {
         if (!std::isdigit(c) && c != '.' && c != '-' && c != 'e' && c != 'E' && c != '+') {
            isNumber = false;
            break;
         }
      }
      if (isNumber) {
         return ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
      }
   }

   // Default: white
   return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
}

void JSONApp::renderJSONTree(JSONNode* node, const std::string& filter, int depth)
{
   if (!node) {
      return;
   }

   // If filter is active, check if this node or any child matches
   bool matchesFilter = false;
   if (!filter.empty()) {
      std::string text = node->key + " " + node->value;
      std::string lowerText = text;
      std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::tolower);
      std::string lowerFilter = filter;
      std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);

      if (lowerText.find(lowerFilter) != std::string::npos) {
         matchesFilter = true;
      } else {
         // Check children
         for (const auto& child : node->children) {
            std::string childText = child->key + " " + child->value;
            std::string lowerChildText = childText;
            std::transform(lowerChildText.begin(), lowerChildText.end(), lowerChildText.begin(), ::tolower);
            if (lowerChildText.find(lowerFilter) != std::string::npos) {
               matchesFilter = true;
               break;
            }
         }
      }
   } else {
      matchesFilter = true;
   }

   if (!matchesFilter) {
      return;
   }

   // Determine if this node is a container (object or array)
   bool isContainer = node->isObject || node->isArray;

   // Build label with indentation
   std::string label;
   if (!node->key.empty()) {
      label = node->key;
      if (isContainer) {
         label += " : " + node->value;
      } else {
         label += " : ";
      }
   }

   if (isContainer) {
      // Container node: TreeNode with open/close
      ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
      if (node->expanded) {
         flags |= ImGuiTreeNodeFlags_DefaultOpen;
      }

      // Color for object/array keys
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 1.0f, 1.0f));

      // Add count indicator
      std::string displayLabel = label;
      if (!node->children.empty()) {
         displayLabel += " (" + std::to_string(node->children.size()) + " items)";
      }

      bool open = ImGui::TreeNodeEx(displayLabel.c_str(), flags);
      ImGui::PopStyleColor();

      if (ImGui::IsItemClicked()) {
         node->expanded = !node->expanded;
      }

      if (open) {
         for (const auto& child : node->children) {
            renderJSONTree(child.get(), filter, depth + 1);
         }
         ImGui::TreePop();
      }
   } else {
      // Leaf node: display on a single line with color coding
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.8f, 1.0f));
      if (!node->key.empty()) {
         ImGui::TextUnformatted((node->key + ": ").c_str());
      }
      ImGui::PopStyleColor();

      if (!node->key.empty()) {
         ImGui::SameLine(0.0f, 0.0f);
      }

      // Show value with appropriate color
      ImVec4 color = getValueColor(node->value);
      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::TextUnformatted(node->value.c_str());
      ImGui::PopStyleColor();

      // Tooltip with full value
      if (ImGui::IsItemHovered()) {
         ImGui::SetTooltip("%s", node->value.c_str());
      }
   }
}

// ============================================================================
// JSON Panel Rendering
// ============================================================================

void JSONApp::renderJSONPanel()
{
   if (ImGui::Begin("Editor")) {
      // Toolbar
      if (ImGui::Button("Open File")) {
#ifdef USE_TINYFILEDIALOGS
         const char* selected = tinyfd_openFileDialog("Open JSON File", "", 0, NULL, NULL, 0);
         if (selected) {
            loadJSONFile(selected);
         }
#else
         m_showOpenFilePopup = true;
#endif
      }

      if (!m_jsonRoot) {
         ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No JSON loaded");
         ImGui::Text("Open a .json file using the 'Open File' button");
         ImGui::End();
         return;
      }

      ImGui::SameLine();

      if (ImGui::Button("Expand All")) {
         if (m_jsonRoot) {
            std::function<void(JSONNode*)> expand = [&](JSONNode* n) {
               n->expanded = true;
               for (auto& child : n->children) {
                  expand(child.get());
               }
            };
            expand(m_jsonRoot.get());
         }
      }
      ImGui::SameLine();

      if (ImGui::Button("Collapse All")) {
         if (m_jsonRoot) {
            std::function<void(JSONNode*)> collapse = [&](JSONNode* n) {
               n->expanded = false;
               for (auto& child : n->children) {
                  collapse(child.get());
               }
            };
            collapse(m_jsonRoot.get());
         }
      }
      ImGui::SameLine();

      ImGui::Text("Search:");
      ImGui::SameLine();
      char searchBuf[256] = "";
      strncpy(searchBuf, m_jsonSearchFilter.c_str(), sizeof(searchBuf) - 1);
      if (ImGui::InputText("##jsonSearch", searchBuf, sizeof(searchBuf))) {
         m_jsonSearchFilter = searchBuf;
         m_jsonSearchActive = !m_jsonSearchFilter.empty();
      }

      ImGui::SameLine();
      ImGui::Checkbox("Pretty Print", &m_showPrettyPrint);

      ImGui::Separator();

      if (!m_jsonRoot) {
         ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No JSON loaded");
         ImGui::Text("Open a .json file using the 'Open File' button");
         ImGui::End();
         return;
      }

      // Show file info
      ImGui::Text("File: %s", fs::path(m_currentFilePath).filename().string().c_str());
      ImGui::Text("Nodes: %d", countNodes(m_jsonRoot.get()));
      ImGui::Separator();

      // Render the tree in a child window for scrolling
      ImGui::BeginChild("JSONTree", ImVec2(0, 0), true);

      if (m_showPrettyPrint && !m_rawJSONString.empty()) {
         try {
            json j = json::parse(m_rawJSONString);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.9f, 0.8f, 1.0f));
            ImGui::TextUnformatted(j.dump(2).c_str());
            ImGui::PopStyleColor();
         } catch (...) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error formatting JSON");
         }
      } else {
         // Show tree view
         renderJSONTree(m_jsonRoot.get(), m_jsonSearchFilter);
      }

      ImGui::EndChild();

      // Open file popup (fallback when tinyfiledialogs is not available)
      if (m_showOpenFilePopup) {
         ImGui::OpenPopup("Open JSON File");
      }
      if (ImGui::BeginPopupModal("Open JSON File", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
         ImGui::Text("Enter path to JSON file:");

         // Browse button
         if (ImGui::Button("Browse...")) {
#ifdef USE_TINYFILEDIALOGS
            const char* selected = tinyfd_openFileDialog("Select JSON File", "", 0, NULL, NULL, 0);
            if (selected) {
               strncpy(m_filePathBuffer, selected, sizeof(m_filePathBuffer) - 1);
            }
#endif
         }
         ImGui::SameLine();
         ImGui::InputText("##jsonPath", m_filePathBuffer, sizeof(m_filePathBuffer));

         if (ImGui::Button("Load")) {
            if (strlen(m_filePathBuffer) > 0) {
               loadJSONFile(m_filePathBuffer);
               m_showOpenFilePopup = false;
               ImGui::CloseCurrentPopup();
            }
         }
         ImGui::SameLine();
         if (ImGui::Button("Cancel")) {
            m_showOpenFilePopup = false;
            ImGui::CloseCurrentPopup();
         }
         ImGui::EndPopup();
      }
   }
   ImGui::End();
}

// ============================================================================
// File Loading
// ============================================================================

void JSONApp::loadJSONFile(const std::string& path)
{
   std::ifstream file(path);
   if (!file.is_open()) {
      m_outputLines.push_back("Error: Could not open JSON file: " + path);
      return;
   }

   std::stringstream buffer;
   buffer << file.rdbuf();
   std::string content = buffer.str();
   file.close();

   m_currentFilePath = path;
   buildJSONTree(content);
   m_jsonSearchFilter.clear();
   m_jsonSearchActive = false;
   m_outputLines.push_back("Loaded JSON file: " + path);
}

void JSONApp::closeFile()
{
   m_jsonRoot.reset();
   m_currentFilePath.clear();
   m_jsonSearchFilter.clear();
   m_jsonSearchActive = false;
   m_rawJSONString.clear();
   m_outputLines.clear();
   std::cout << "[undoApp.JSON] Closed file" << std::endl;
}

} // namespace JSON
} // namespace undoApp