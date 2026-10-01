// The recent files list as it is drawn: which row a click acts on, and what a pick
// asks for.
//
// This drives ImGuiManager::renderRecentFiles() itself rather than the model
// underneath it, because the model is already covered by recent_files.cpp and the
// part that can go wrong here is the part only ImGui sees. Two rows of the list are
// laid out next to each other with a button on each, and a row that acts on the
// wrong file is not a cosmetic error: it opens somebody's other file, or drops a
// file they wanted out of their history, without saying which.
//
// The clicks are delivered by position, not by calling the handler, so what is
// checked is where the rows ended up and what they act on. The rows are found as the
// remove button each of them is drawn with, because ImGui keeps no rectangle per item
// across frames: there is no label-to-rect map to ask, and the one that existed during
// the frame that drew the row is gone by the time the click is delivered.
//
// The draw list is read during the frame that drew it. After EndFrame the window is
// gone and its pointer is not one to dereference, which is the crash this reaches for
// if it is read too late.

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define private public
#include "undoStudio/ui/ImGuiManager.hpp"
#undef private

#include "undoStudio/core/RecentFiles.hpp"

using undoStudio::ui::ImGuiManager;
using undoStudio::core::RecentFiles;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

/// A point recovered from the draw list.
struct Point {
   float x, y;
};

/// The popup's own window.
///
/// Not looked up by name, because BeginPopup does not name the window after the
/// string it was given: it hashes that string into an id and names the window after
/// *that*, as "##Popup_<id in hex>". Both halves are reproduced the way ImGui
/// computes them, from the window that was current when the popup was asked for —
/// which is the same window this is called from, because ImGui restores it when the
/// popup ends.
///
/// Read by name and not by id, because the id BeginPopup hashes and the id the window
/// is then given are not the same number: Begin names the window from the first and
/// derives the second from that name.
static ImGuiWindow* popupWindow() {
   ImGuiContext& g = *ImGui::GetCurrentContext();
   if (g.CurrentWindow == nullptr) {
      return nullptr;
   }
   char name[32];
   ImFormatString(name, IM_COUNTOF(name), "##Popup_%08x",
                  g.CurrentWindow->GetID(ImGuiManager::kRecentFilesPopup));
   return ImGui::FindWindowByName(name);
}

/// Where each row is, read as the remove button that row is drawn with.
///
/// The button is found by the colour the popup stacks onto it, which is the one
/// colour in the list that is there to say "this one is not like the others". Read
/// that way rather than by shape, because the shape is not distinguishing: the button
/// is drawn a little under a frame high, the glyphs inside it are the same width and
/// near enough the same place, and the glyphs of the row's name sit to the right of it
/// at the same height. Every one of those is a rectangle a row tall and close to the
/// left edge, and picking the wrong one would put the click on the button of the row
/// below or on the label of the one above.
///
/// The draw list's vertices are scanned directly, four at a time, rather than through
/// its draw commands: ImGui indexes a command's vertices indirectly, through an offset
/// into buffers handed to the backend rather than the ones held here, so a command's
/// extent is not computable from this draw list. A filled rectangle reaches a draw
/// list as four vertices in the first place.
///
/// The rows found are compared by the caller against how many files are remembered: a
/// search that missed one would otherwise hand the rest of the test a shorter list to
/// click on and pass without ever clicking a row.
static std::vector<Point> rowButtons(ImGuiManager& mgr) {
   std::vector<Point> centres;

   // The colour renderRecentFiles() pushes onto the button, spelled the same way it
   // is there. Read from the popup rather than from a constant of this file, so that
   // changing the button's colour leaves this failing rather than quietly finding
   // nothing and passing on an empty list.
   const ImU32 removeColour = ImGui::ColorConvertFloat4ToU32(ImVec4(0.6f, 0.25f, 0.25f, 1.0f));

   // Drawn a few times before it is read, because a popup is sized to the frame
   // before: the one it appears on is pinned to the size asked for, and only the
   // frames after it fit its content. Read on the frame it appears and every row but
   // the first is laid out outside a window one line high — which is what a first
   // attempt at this did, and it found one row in a list of two and reported the list
   // as having one row rather than as being too short to read.
   //
   // The mouse is parked away first, and left there for the frame that is read: a
   // button under the cursor is drawn in its hovered colour, not the one looked for
   // here, so a row left hovered is a row this cannot see. That is not a corner case
   // — the frame before this one ends with a click that left the cursor on a button,
   // and after the list shrinks that button is on the row above.
   ImGuiIO& io = ImGui::GetIO();
   for (int warm = 0; warm < 3; ++warm) {
      io.AddMousePosEvent(5.0f, 5.0f);
      ImGui::NewFrame();
      mgr.openRecentFiles();
      mgr.renderRecentFiles();
      ImGui::EndFrame();
   }

   io.AddMousePosEvent(5.0f, 5.0f);
   ImGui::NewFrame();
   mgr.openRecentFiles();
   mgr.renderRecentFiles();

   ImGuiWindow* win = popupWindow();
   if (win != nullptr && win->DrawList != nullptr) {
      ImDrawList* drawList = win->DrawList;
      for (int i = 0; i + 3 < drawList->VtxBuffer.Size; ++i) {
         if (drawList->VtxBuffer[i].col != removeColour) {
            continue;
         }
         // All four vertices, so that a glyph of the button's own label — which is
         // drawn in white over the red, and so cannot carry it — is not counted and a
         // quad that is not the button cannot pass on one corner.
         bool allRed = true;
         for (int k = 0; k < 4; ++k) {
            allRed = allRed && drawList->VtxBuffer[i + k].col == removeColour;
         }
         if (!allRed) {
            continue;
         }
         const ImVec2* quad[4] = {&drawList->VtxBuffer[i + 0].pos, &drawList->VtxBuffer[i + 1].pos,
                                  &drawList->VtxBuffer[i + 2].pos, &drawList->VtxBuffer[i + 3].pos};
         float minX = (*quad[0]).x, maxX = minX, minY = (*quad[0]).y, maxY = minY;
         for (int k = 1; k < 4; ++k) {
            minX = ImMin(minX, (*quad[k]).x);
            maxX = ImMax(maxX, (*quad[k]).x);
            minY = ImMin(minY, (*quad[k]).y);
            maxY = ImMax(maxY, (*quad[k]).y);
         }
         if (maxX > minX && maxY > minY) {
            centres.push_back(Point{(minX + maxX) * 0.5f, (minY + maxY) * 0.5f});
         }
         i += 3;
      }
   }

   ImGui::EndFrame();

   // Rows are laid out downwards, and the draw list records them in the order the
   // list holds them, which is newest first. Sorted anyway: the position in the list
   // is what the next checks are about, so it is read rather than assumed.
   std::sort(centres.begin(), centres.end(), [](const Point& a, const Point& b) { return a.y < b.y; });
   return centres;
}

/// A click at one point: three frames, because ImGui hit-tests each item as it is
/// submitted and an item only acts on the release.
///
/// The list is drawn in every frame of the click. A frame that queued the press and
/// drew nothing has nothing to hit, and the press lands on the window behind.
static void click(ImGuiManager& mgr, float x, float y) {
   ImGuiIO& io = ImGui::GetIO();
   io.DisplaySize = ImVec2(1280, 800);

   io.AddMousePosEvent(x, y);
   ImGui::NewFrame();
   mgr.openRecentFiles();
   mgr.renderRecentFiles();
   ImGui::EndFrame();

   io.AddMouseButtonEvent(0, true);
   ImGui::NewFrame();
   mgr.openRecentFiles();
   mgr.renderRecentFiles();
   ImGui::EndFrame();

   io.AddMouseButtonEvent(0, false);
   ImGui::NewFrame();
   mgr.openRecentFiles();
   mgr.renderRecentFiles();
   ImGui::EndFrame();
}

/// The path a pick asked the editor to open, or an empty string.
static std::string takeRequest(ImGuiManager& mgr) {
   std::string path;
   const std::string* next = mgr.consumeOpenFileRequest(path);
   return next == nullptr ? std::string() : *next;
}

static void writeFile(const fs::path& path) {
   fs::create_directories(path.parent_path());
   std::ofstream out(path);
   out << "METHOD FB : nome\nEND_PROGRAM\n";
}

int main() {
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.DisplaySize = ImVec2(1280, 800);
   io.Fonts->AddFontDefault();
   io.Fonts->Build();

   // A working directory of its own: the list is written relative to it, and one that
   // picked up the developer's own would be testing their session rather than the code.
   const fs::path root = fs::temp_directory_path() / "undoStudio-recent-files-ui";
   fs::remove_all(root);
   fs::create_directories(root);
   fs::current_path(root);

   // Two files with the same name in different folders. The name is what the row shows,
   // so it cannot be the id: both rows would be the same widget, and a click on one
   // would act on the other.
   const fs::path alpha = root / "uno" / "MAIN.st";
   const fs::path beta = root / "due" / "MAIN.st";
   writeFile(alpha);
   writeFile(beta);

   auto& recents = RecentFiles::getInstance();
   auto& mgr = ImGuiManager::getInstance();

   recents.clearRecentFiles();
   recents.setMaxRecentFiles(10);
   recents.rememberFile(alpha.string());
   recents.rememberFile(beta.string());
   check(recents.recentFiles().size() == 2, "two files are remembered, got " +
                                               std::to_string(recents.recentFiles().size()));
   check(!recents.recentFiles().empty() && recents.recentFiles().front() == fs::canonical(beta).string(),
         "the most recent is first");

   // --- the rows are where the list says they are ---
   const std::vector<Point> rows = rowButtons(mgr);
   check(rows.size() == recents.recentFiles().size(),
         "one row is drawn per remembered file: " + std::to_string(rows.size()) + " drawn, " +
             std::to_string(recents.recentFiles().size()) + " remembered");
   check(rows.size() == 2 && rows[0].y < rows[1].y,
         "and they are in the order the list holds them, newest first");

   // --- a pick asks for the file under the cursor, not its neighbour ---
   //
   // The older file is the lower row. The point clicked is to the right of that row's
   // button, where its name is drawn: the button and the name share the line, so the
   // row is a full line wide and this lands on it. A list whose rows acted on the
   // wrong entry would answer with the other file, which is the defect this is here for.
   if (rows.size() == 2) {
      click(mgr, rows[1].x + 60.0f, rows[1].y);
   }
   {
      const std::string asked = takeRequest(mgr);
      check(asked == fs::canonical(alpha).string(), "clicking the older row asks for that file, got '" + asked + "'");
   }

   // --- and it is asked for once ---
   check(takeRequest(mgr).empty(), "the request is not left in the queue");

   // --- the remove button drops that row and only that row ---
   //
   // The top row's button, clicked on its own centre. The top row is the newest, so
   // what is left is the older one. The rows are read again first because the one that
   // goes away leaves the other where it was: a click at the position of a row that is
   // no longer there would hit whichever row moved into it.
   const std::vector<Point> before = rowButtons(mgr);
   check(before.size() == 2, "both rows are drawn before removing one, got " + std::to_string(before.size()));
   if (before.size() == 2) {
      click(mgr, before[0].x, before[0].y);
   }
   check(recents.recentFiles().size() == 1, "the remove button of a row removes that row, got " +
                                                 std::to_string(recents.recentFiles().size()));
   check(!recents.recentFiles().empty() && recents.recentFiles().front() == fs::canonical(alpha).string(),
         "and it is the one under the button that went, got '" +
             (recents.recentFiles().empty() ? std::string("none") : recents.recentFiles().front()) + "'");
   {
      const std::vector<Point> after = rowButtons(mgr);
      check(after.size() == 1, "one row is left drawn, got " + std::to_string(after.size()));
   }

   // --- the list draws when there is nothing in it ---
   recents.clearRecentFiles();
   check(rowButtons(mgr).empty(), "an empty list draws no rows");
   check(takeRequest(mgr).empty(), "and asking for nothing picks nothing");

   fs::current_path(fs::temp_directory_path());
   fs::remove_all(root);

   ImGui::DestroyContext();

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}