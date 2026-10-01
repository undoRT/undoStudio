/**
 * @file st_editor_frame.hpp
 * @brief One frame of the Structured Text document, the way the Editor panel draws it
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// The panel itself is opened by ImGuiManager::render() and the ST backend draws
// no window of its own, so a test that wants a frame of the editor has to open
// the panel and call the three halves in the order the panel calls them. Getting
// this wrong is not a small mistake: opening the panel twice nests one Begin
// inside another, which lays the document out in a window a line high and pushes
// everything below the bottom of the frame, and a test that asserted on any of it
// would be asserting on a document that was never really drawn.
//
// A header rather than a function in one test file because six tests need the
// same three calls, and a test that grew its own copy would be a second answer to
// "how is a frame of the ST editor driven".

#ifndef UNDOSTUDIO_ST_EDITOR_FRAME_HPP
#define UNDOSTUDIO_ST_EDITOR_FRAME_HPP

#include <imgui.h>

// undoAppST.hpp is expected to have been included already, by the test, inside
// its `#define private public`. Including it again here is a no-op thanks to the
// include guard, and it is here so that a caller that has not yet included it
// gets a compile error about the class rather than about this function.
#include "undoAppST.hpp"

/// @brief Draw one frame of the Structured Text document inside the Editor panel
inline void stEditorFrame(undoApp::ST::STApp& app) {
   ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoCollapse);
   app.beginEditorFrame();
   app.renderDocument();
   app.endEditorFrame();
   ImGui::End();
}

#endif // UNDOSTUDIO_ST_EDITOR_FRAME_HPP