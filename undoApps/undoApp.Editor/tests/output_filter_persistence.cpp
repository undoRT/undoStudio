// The Output panel's switches, clicked.
//
// output_filter.cpp proves the filter does the right thing to a list of lines.
// That was never the bug, and it could never have caught it: the bug was that
// nothing kept the answer. renderOutputPanel() built its OutputFilter as a local,
// handed its address to ImGui::Checkbox, and let it die at the end of the frame.
// The box toggled, the log never changed, and the switch looked wired to something
// it was not — harder to report than a missing control, and invisible to a test
// that never renders a frame.
//
// So this one drives the real panel over real frames, with a real mouse, and reads
// the state back afterwards. The click is the point: writing app.m_outputFilter
// directly would pass against the broken code too, because the member would be
// written by the test rather than by the checkbox.

#include <imgui.h>
#include <imgui_internal.h>

#include <TextEditor.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#define private public
#include "undoAppST.hpp"
#undef private

using namespace undoApp::ST;
using undoApp::ST::OutSeverity;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

/// One frame of the Output panel, at a size where every switch fits on one line.
static void frameOutput(STApp& app) {
   ImGui::NewFrame();
   ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
   ImGui::SetNextWindowSize(ImVec2(1200.0f, 800.0f));
   app.renderOutputPanel();
   ImGui::EndFrame();
}

/// The centre of every switch on the panel's first row, left to right.
///
/// ImGui has no way to ask for the rectangle of an item it is not currently
/// drawing, and no map from label to rect survives a frame. What does survive is
/// the draw list, and a checkbox is a filled square exactly one frame height on a
/// side, which nothing else on that row is. Finding them by that shape rather than
/// by their label means the test hard-codes no pixel that a font change would move,
/// and ordering them left to right puts them in the order they are declared: Errors,
/// Warnings, Success, Detail, Generated ST.
///
/// The panel's own frame is the one measured, and that is not a detail. An earlier
/// version of this drew its own row of five checkboxes to measure instead, and got
/// a tidy five squares in a tidy order — in the wrong places. The real toolbar puts
/// a count beside each switch and a search field under the row, so a click landed
/// on whatever was at those coordinates in the real panel, which was sometimes a
/// count, sometimes nothing. Worse, the switch that shares a row with the generated
/// dump is wired to a different flag from the other four, so a click that hit it
/// proved only that the click worked: the check it satisfied was already true.
static std::vector<ImVec2> switchCentres(STApp& app) {
   std::vector<ImVec2> centres;
   float frameHeight = 0.0f;

   ImDrawList* drawList = nullptr;
   ImGui::NewFrame();
   ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
   ImGui::SetNextWindowSize(ImVec2(1200.0f, 800.0f));
   app.renderOutputPanel();
   // The window outlives its Begin/End pair: it stays in the context until the frame
   // is over, which is what makes reading its draw list here rather than after
   // EndFrame() possible at all.
   for (int i = 0; i < ImGui::GetCurrentContext()->Windows.Size; ++i) {
      ImGuiWindow* w = ImGui::GetCurrentContext()->Windows[i];
      if (std::string(w->Name) == "ST Output") {
         drawList = w->DrawList;
         // What ImGui::GetFrameHeight() computes, taken from the window's own font
         // size because there is no current window to ask once the panel has closed.
         frameHeight = w->FontRefSize + ImGui::GetStyle().FramePadding.y * 2.0f;
         break;
      }
   }
   ImGui::EndFrame();

   if (!drawList) {
      return centres;
   }

   for (int i = 0; i + 3 < drawList->VtxBuffer.Size; ++i) {
      const ImVec2* quad[4] = {&drawList->VtxBuffer[i + 0].pos, &drawList->VtxBuffer[i + 1].pos,
                               &drawList->VtxBuffer[i + 2].pos, &drawList->VtxBuffer[i + 3].pos};
      float minX = (*quad[0]).x, maxX = minX, minY = (*quad[0]).y, maxY = minY;
      for (int k = 1; k < 4; ++k) {
         minX = ImMin(minX, (*quad[k]).x);
         maxX = ImMax(maxX, (*quad[k]).x);
         minY = ImMin(minY, (*quad[k]).y);
         maxY = ImMax(maxY, (*quad[k]).y);
      }
      const float w = maxX - minX;
      const float h = maxY - minY;
      // A checkbox is a square; a glyph is not, and a button is wider than it is tall.
      if (std::abs(w - frameHeight) < 1.0f && std::abs(h - frameHeight) < 1.0f) {
         centres.push_back(ImVec2((minX + maxX) * 0.5f, (minY + maxY) * 0.5f));
      }
   }

   std::sort(centres.begin(), centres.end(), [](const ImVec2& a, const ImVec2& b) { return a.x < b.x; });

   // Consecutive quads of one box can both match; keep one per position.
   centres.erase(std::unique(centres.begin(), centres.end(),
                             [](const ImVec2& a, const ImVec2& b) {
                                return std::abs(a.x - b.x) < 2.0f && std::abs(a.y - b.y) < 2.0f;
                             }),
                 centres.end());
   return centres;
}

/// The log's own window, whose content height is what the filter changes.
static ImGuiWindow* findLogWindow() {
   for (int i = 0; i < ImGui::GetCurrentContext()->Windows.Size; ++i) {
      ImGuiWindow* w = ImGui::GetCurrentContext()->Windows[i];
      if (std::string(w->Name).find("OutputLog") != std::string::npos) {
         return w;
      }
   }
   return nullptr;
}

/// Move the mouse, press, release: a click ImGui believes in.
///
/// Every frame here draws the panel, and that is not incidental. ImGui decides what
/// was clicked by hit-testing each item as it is submitted, so a frame that queues a
/// mouse event but draws nothing has nothing to hit: the event is consumed, no item
/// is hovered, and the press lands on the window behind. The three frames are the
/// three halves of a click — position, press, release — and a switch only toggles on
/// the release.
static void click(STApp& app, ImVec2 at) {
   ImGuiIO& io = ImGui::GetIO();
   io.AddMousePosEvent(at.x, at.y);
   frameOutput(app);
   io.AddMouseButtonEvent(0, true);
   frameOutput(app);
   io.AddMouseButtonEvent(0, false);
   frameOutput(app);
}

int main() {
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.DisplaySize = ImVec2(1280.0f, 800.0f);
   io.Fonts->AddFontDefault();
   io.Fonts->Build();

   STApp app;
   app.setupEditors();
   app.m_outputLines = {
      {OutSeverity::Error, "  line 12: undeclared identifier 'counter'"},
      {OutSeverity::Error, "  line 30: type mismatch"},
      {OutSeverity::Warning, "  unused variable 'tmp'"},
      {OutSeverity::Success, "Parsing successful: 1 POU(s) found"},
      {OutSeverity::Info, "// Generated ST file:"},
      {OutSeverity::Info, "//   PROGRAM Main"},
   };

   // The panel is drawn before anything is measured, so the switches exist and the
   // log has its content by the time the switch under test is clicked.
   for (int f = 0; f < 3; ++f) {
      frameOutput(app);
   }
   check(app.m_outputFilter.showErrors, "every severity is shown to begin with");
   check(app.m_outputFilter.showInfo, "including the generated dump");

   const std::vector<ImVec2> centres = switchCentres(app);
   if (centres.size() != 5) {
      std::printf("  FAIL found %zu switch squares, wanted 5; the test cannot tell which is which\n",
                  centres.size());
      ++failures;
   } else {
      // --- the switches are connected to something -----------------------------
      //
      // In declared order: Errors, Warnings, Success, Detail, Generated ST. The
      // first four are the filter and the fifth is the dump, and they are checked
      // apart because they are stored apart: a click on the fifth that appeared to
      // work could be a click that did nothing, the flag it would have changed
      // already being on. The fourth starts on, so turning it off is a change in
      // one direction only.
      app.m_outputFilter = OutputFilter();
      frameOutput(app);
      click(app, centres[3]);  // Detail
      check(!app.m_outputFilter.showInfo, "clicking Detail turned the detail lines off");
      check(app.m_outputFilter.showErrors && app.m_outputFilter.showWarnings,
            "and left the errors and warnings alone");

      // --- and it survives the next frame --------------------------------------
      //
      // This is the half a single-frame check cannot see. A value that reached the
      // member but was overwritten at the top of the next call would pass
      // everything above and filter nothing.
      frameOutput(app);
      check(!app.m_outputFilter.showInfo, "still off after the next frame");
      frameOutput(app);
      check(!app.m_outputFilter.showInfo, "and the one after that");

      // --- and the log is actually shorter for it ------------------------------
      //
      // The last link in the chain: the flag is kept, and the flag is used. Without
      // this the test would pass for a panel that remembered the click and ignored
      // it, which is the other half of what "inert switch" means.
      //
      // Read after a second frame, because ContentSize is the size the window had
      // during the *previous* frame: ImGui only learns how tall the content is once
      // it has been laid out, and publishes it to the next frame. Read straight after
      // changing the filter, this measures the filter that was in place before the
      // change and the two directions come out equal, which reads as "the filter
      // does nothing" and is the opposite of the truth.
      app.m_outputFilter.showInfo = true;
      frameOutput(app);
      frameOutput(app);
      ImGuiWindow* log = findLogWindow();
      const float withDump = log ? log->ContentSize.y : -1.0f;
      check(withDump > 0.0f, "the log has content with the dump on");

      app.m_outputFilter.showInfo = false;
      frameOutput(app);
      frameOutput(app);
      const float withoutDump = log ? log->ContentSize.y : -1.0f;
      check(withoutDump < withDump,
            "hiding the dump shortens the log: " + std::to_string(static_cast<int>(withDump)) +
               "px with it, " + std::to_string(static_cast<int>(withoutDump)) + "px without");

      // --- every switch is wired, not just the one that was checked first -------
      app.m_outputFilter = OutputFilter();
      frameOutput(app);
      click(app, centres[0]);  // Errors
      check(!app.m_outputFilter.showErrors, "clicking Errors turned the errors off");
      check(app.m_outputFilter.showInfo, "and left the detail lines alone");

      app.m_outputFilter = OutputFilter();
      frameOutput(app);
      click(app, centres[1]);  // Warnings
      check(!app.m_outputFilter.showWarnings, "clicking Warnings turned the warnings off");

      app.m_outputFilter = OutputFilter();
      frameOutput(app);
      click(app, centres[2]);  // Success
      check(!app.m_outputFilter.showSuccess, "clicking Success turned the success lines off");
   }

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
   } else {
      std::printf("RESULT: %d check(s) failed\n", failures);
   }
   ImGui::DestroyContext();
   return failures == 0 ? 0 : 1;
}
