/**
 * @file undoAppJSON.hpp
 * @brief Header of the JSON Viewer undoApp
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

// Include ImGui for ImVec4 type
#include <imgui.h>

namespace undoApp {
namespace JSON {

/**
 * @brief Node structure for the JSON tree viewer
 */
struct JSONNode
{
   std::string key;                                 ///< Object key (if part of an object)
   std::string value;                               ///< String representation of the value
   bool isObject = false;                           ///< True if this node is a JSON object
   bool isArray = false;                            ///< True if this node is a JSON array
   std::vector<std::unique_ptr<JSONNode>> children; ///< Child nodes
   bool expanded = false;                           ///< True if node is expanded in the tree
   int depth = 0;                                   ///< Node depth in the tree
};

/**
 * @brief One open JSON file, held while another file is in the viewer
 *
 * The raw text rather than the parsed tree: the tree is what the viewer draws and
 * is rebuilt from the text when the file is shown again, so carrying it would mean
 * a second copy of the truth that has to be kept in step with the text.
 */
struct JSONDocument
{
   std::string path;        ///< Absolute path, where a later save goes
   std::string rawText;     ///< The file as it was read
   std::string prettyPrint; ///< The formatted text, if pretty printing is on
   bool pretty = false;
};

/**
 * @brief Main application class for the JSON Viewer plugin
 *
 * Implements a JSON file viewer with tree visualization, search,
 * expand/collapse, and formatting capabilities.
 */
class JSONApp
{
public:
   /// @brief Get the singleton instance of JSONApp
   static JSONApp& getInstance();

   /// @brief Initialize the JSON undoApp
   bool initialize();

   /// @brief Shutdown the JSON undoApp and release resources
   void shutdown();

   /// @brief Register UI panels with ImGuiManager
   void registerPanels();

   /// @brief Load a JSON file into the viewer
   void loadJSONFile(const std::string& path);

   /// @brief Clear the JSON viewer state
   void closeFile();

   // ============================================================================
   // Panel rendering methods
   // ============================================================================

   /// @brief Render the JSON Viewer panel
   void renderJSONPanel();

   /// @brief Take the open file out whole, to be put back later
   JSONDocument takeDocument();

   /// @brief Show a file taken out by takeDocument()
   /// @param doc The file and its text
   void setDocument(const JSONDocument& doc);

   // ============================================================================
   // JSON tree building and rendering
   // ============================================================================

   /// @brief Build the JSON tree from a string
   void buildJSONTree(const std::string& jsonString);

   /// @brief Recursively render the JSON tree with filtering
   void renderJSONTree(JSONNode* node, const std::string& filter, int depth = 0);

   /// @brief Helper to get color for value type
   static ImVec4 getValueColor(const std::string& value);

private:
   // ============================================================================
   // Member variables
   // ============================================================================

   bool m_initialized = false;             ///< Initialization flag
   std::vector<std::string> m_outputLines; ///< Lines to display in the Output panel

   std::unique_ptr<JSONNode> m_jsonRoot; ///< Root of the JSON tree
   std::string m_jsonSearchFilter;       ///< Filter string for searching
   bool m_jsonSearchActive = false;      ///< True if filtering is active
   std::string m_currentFilePath;        ///< Path of the loaded JSON file
   bool m_showPrettyPrint = false;       ///< True if pretty print is enabled
   std::string m_rawJSONString;          ///< Raw JSON string for pretty print

   // ============================================================================
   // Popup states
   // ============================================================================

};

} // namespace JSON
} // namespace undoApp