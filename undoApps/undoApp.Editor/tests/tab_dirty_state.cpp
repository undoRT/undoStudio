// The unsaved mark on a tab, and what puts it there.
//
// The mark used to be a lie in both directions at once, and neither half was visible
// in the code that drew it: switching tabs went through stashActiveDocument(true),
// which meant "this has unsaved changes", so every file that was ever looked at twice
// carried an asterisk — and nothing in the plugin ever took it off, because no save
// path said the file was clean again. The mark was read from one flag that was being
// set by tab switches and cleared by nothing.
//
// So what it takes for a tab to show a mark is asked here from the editors, the same
// way the runtime asks: put text into a file that is on screen, draw a frame, and see
// whether the tab claims it has changes. A file nobody typed into is the case that was
// broken and the one worth a check of its own, because it is the one a user sees on a
// freshly opened file.
//
// The rule that the conflation was standing in for is checked too. It was there for a
// reason: browsing past a preview must not throw away what was typed into it. Nothing
// sets that flag any more on the way to a stash, so if nothing pins a preview holding
// edits, the next click destroys them silently — which is worse than a wrong asterisk.

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#define private public
#include <TextEditor.h>
#include "undoAppEditor.hpp"
#undef private

#include "undoAppCpp.hpp"
#include "undoAppST.hpp"
#include "undoAppText.hpp"

using namespace undoApp;
using namespace undoApp::Editor;
using undoApp::CppApp;
using undoApp::TextApp;
using undoApp::ST::STApp;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

static void writeFile(const fs::path& p, const std::string& body) {
   std::ofstream out(p);
   out << body;
}

static std::string readFile(const fs::path& p) {
   std::ifstream in(p);
   return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Whether the bar drew an unsaved mark in the last frameWithBar().
///
/// Found in the draw list, because ImGui keeps no rectangle for an item once the frame
/// is over: a mark that was drawn is the only evidence that it was. The asterisk is a
/// glyph in the unsaved colour, so it is looked for as one — the tab buttons are drawn
/// in the panel's own colours and cannot pass for it.
static bool asteriskWasDrawn = false;

static void scanForMark() {
   asteriskWasDrawn = false;
   for (int i = 0; i < ImGui::GetCurrentContext()->Windows.Size; ++i) {
      ImGuiWindow* w = ImGui::GetCurrentContext()->Windows[i];
      // The child is registered as "ST Editor/##fileTabs_<hash>", so it is matched
      // by the id rather than by the whole name: the parent is in there too, and a
      // check that never looks at the right window is a check that cannot fail.
      if (std::string(w->Name).find("##fileTabs") == std::string::npos || w->DrawList == nullptr) {
         continue;
      }
      const ImDrawList* drawList = w->DrawList;
      for (int v = 0; v + 3 < drawList->VtxBuffer.Size; ++v) {
         // The unsaved colour is (1.0, 0.8, 0.3): red and green full, blue under a
         // third. All four vertices, because a single corner is not a mark — a quad
         // of the bar's background shares one.
         bool allMark = true;
         for (int k = 0; k < 4; ++k) {
            const unsigned int c = drawList->VtxBuffer[v + k].col;
            const int r = static_cast<int>((c >> IM_COL32_R_SHIFT) & 0xFF);
            const int g = static_cast<int>((c >> IM_COL32_G_SHIFT) & 0xFF);
            const int b = static_cast<int>((c >> IM_COL32_B_SHIFT) & 0xFF);
            allMark = allMark && r > 240 && g > 190 && g < 220 && b > 60 && b < 90;
         }
         if (allMark) {
            asteriskWasDrawn = true;
            return;
         }
      }
   }
}

/// One frame with the bar drawn, which is also the frame that refreshes the marks.
static void frameWithBar(EditorApp& app) {
   ImGui::NewFrame();
   ImGui::Begin("ST Editor", nullptr, ImGuiWindowFlags_NoCollapse);
   app.renderFileTabs();
   ImGui::End();
   // The window outlives its Begin/End pair until the frame is over, which is what
   // makes reading its draw list here rather than after EndFrame() possible at all.
   scanForMark();
   ImGui::EndFrame();
}

/// One frame with the text editor drawn, which is where it reports having changed.
static void frameWithTextEditor() {
   ImGui::NewFrame();
   ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
   ImGui::SetNextWindowSize(ImVec2(1200.0f, 600.0f));
   ImGui::Begin("Editor");
   TextApp::getInstance().renderEditorPanel();
   ImGui::End();
   ImGui::EndFrame();
}

/// Put the cursor inside the editor, the way a user does before typing.
///
/// The editor draws itself in a child window and only takes the keyboard when that
/// child is focused, which happens when it is clicked. Three frames because a click is
/// three events: ImGui hit-tests each item as it is submitted, so the position, the
/// press and the release each need a frame of their own.
static void clickIntoTextEditor() {
   ImGuiIO& io = ImGui::GetIO();
   io.AddMousePosEvent(400.0f, 300.0f);
   frameWithTextEditor();
   io.AddMousePosEvent(400.0f, 300.0f);
   io.AddMouseButtonEvent(0, true);
   frameWithTextEditor();
   io.AddMousePosEvent(400.0f, 300.0f);
   io.AddMouseButtonEvent(0, false);
   frameWithTextEditor();
}

/// Characters typed into the editor, the way they arrive in a running IDE.
///
/// Not SetText: the editor only raises "the text changed" for input it handled while
/// being drawn, and a load also uses SetText. A test that typed with SetText would
/// prove nothing about the thing under test — that a keystroke marks a tab — and would
/// in fact pass against the bug this suite is about, because the mark would have been
/// set by the load rather than by the edit.
static void typeIntoTextEditor(const std::string& text) {
   clickIntoTextEditor();
   for (const char c : text) {
      ImGui::GetIO().AddInputCharacter(static_cast<unsigned int>(c));
   }
   frameWithTextEditor();
}

static bool markedDirty(EditorApp& app, const std::string& path) {
   for (const OpenDocument& doc : app.m_open.documents()) {
      if (doc.path == path) {
         return doc.dirty;
      }
   }
   return false;
}

int main() {
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.DisplaySize = ImVec2(1600, 900);
   io.Fonts->AddFontDefault();
   io.Fonts->Build();

   const fs::path dir = fs::temp_directory_path() / "undoStudio-tab-dirty";
   std::error_code ec;
   fs::remove_all(dir, ec);
   fs::create_directories(dir);

   const fs::path a = dir / "Alpha.st";
   const fs::path b = dir / "Beta.st";
   const fs::path t = dir / "notes.txt";
   writeFile(a, "PROGRAM Alpha\nVAR\n av : INT;\nEND_VAR\nav := 1;\nEND_PROGRAM\n");
   writeFile(b, "PROGRAM Beta\nVAR\n bv : INT;\nEND_VAR\nbv := 2;\nEND_PROGRAM\n");
   writeFile(t, "first line\n");

   auto& app = EditorApp::getInstance();
   check(app.initialize(), "the editor app initializes");
   STApp::getInstance().setupEditors();
   check(TextApp::getInstance().initialize(), "the text backend initializes");

   // --- looking at files is not editing them ---
   //
   // Two ST files, pinned, and a switch back and forth several times. Nothing here is
   // typed into, so nothing should be marked: this is the case that came out with an
   // asterisk on every tab.
   app.openFile(a.string(), /*pin=*/true);
   app.openFile(b.string(), /*pin=*/true);
   for (int i = 0; i < 3; ++i) {
      app.switchToTab(a.string());
      app.switchToTab(b.string());
   }
   frameWithBar(app);
   check(!markedDirty(app, a.string()), "switching away from a file does not mark it, got marked");
   check(!markedDirty(app, b.string()), "nor the one switched to and from, got marked");
   frameWithBar(app);
   check(!asteriskWasDrawn, "and the bar draws no mark on either");

   // --- typing marks it, saving takes the mark off ---
   app.openFile(t.string(), /*pin=*/true);
   frameWithTextEditor();
   check(!markedDirty(app, t.string()), "a file just opened is not marked");
   typeIntoTextEditor("second line\n");
   frameWithBar(app);
   check(markedDirty(app, t.string()), "text typed into the file marks the tab");

   app.saveActiveDocument();
   frameWithBar(app);
   check(!markedDirty(app, t.string()), "saving takes the mark off");
   check(readFile(t).find("second line") != std::string::npos, "and the file on disk has what was typed");

   // --- a save that could not happen keeps the mark ---
   //
   // Read-only file, so the write fails. The mark is the only thing standing between
   // somebody and a lost edit, and it must not be the thing that gives up first.
   const fs::path ro = dir / "readonly.txt";
   writeFile(ro, "before\n");
   app.openFile(ro.string(), /*pin=*/true);
   frameWithTextEditor();
   typeIntoTextEditor("after\n");
   frameWithBar(app);
   check(markedDirty(app, ro.string()), "the edit to a read-only file is marked");
   fs::permissions(ro, fs::perms::owner_read);
   app.saveActiveDocument();
   frameWithBar(app);
   check(markedDirty(app, ro.string()), "a save that failed leaves the mark in place");
   check(readFile(ro) == "before\n", "and the file is untouched");
   fs::permissions(ro, fs::perms::owner_all);

   // --- an ST file saves through the same route ---
   //
   // The save key used to be handled where the text backends are dispatched from, and
   // an .st file is not drawn there at all: it is drawn in the ST editor's own panel.
   // So this is what the shortcut could not reach.
   app.openFile(a.string(), /*pin=*/true);
   frameWithBar(app);
   check(!markedDirty(app, a.string()), "an ST file just opened is not marked");

   // The backend's own answer is what the tab is drawn from. Set directly, because
   // that ST editor detects edits among its panes is navigation_dirty's subject and
   // driving the whole ST panel to type into one of them would test that instead.
   STApp::getInstance().m_isDirty = true;
   frameWithBar(app);
   check(markedDirty(app, a.string()), "an ST file the editor says is edited is marked");

   app.saveActiveDocument();
   frameWithBar(app);
   check(!STApp::getInstance().hasUnsavedChanges(), "saving an ST file clears what the editor knew");
   check(!markedDirty(app, a.string()), "and the tab stops claiming it");
   check(readFile(a).find("PROGRAM Alpha") != std::string::npos, "and the .st file on disk was written");

   // --- a preview holding edits is pinned, so browsing cannot discard them ---
   //
   // The rule the removed flag was standing in for. A browse opens into the preview
   // slot, typing into it makes it somebody's work, and the next browse must not be
   // able to throw that away. A click does not reach the preview slot any more, so
   // the route is spelled out rather than being what the tree happens to do.
   const fs::path p = dir / "preview.txt";
   const fs::path q = dir / "next.txt";
   writeFile(p, "preview\n");
   writeFile(q, "next\n");
   app.openFile(p.string(), /*pin=*/false);  // browsing: the preview slot
   frameWithTextEditor();
   check(app.m_open.pinnedCount() == 4, "a preview nobody typed into is not pinned, got " +
                                            std::to_string(app.m_open.pinnedCount()) + " of 5 open");
   typeIntoTextEditor("typed into the preview\n");
   frameWithBar(app);
   check(app.m_open.pinnedCount() == 5, "a preview with edits in it is pinned, got " +
                                            std::to_string(app.m_open.pinnedCount()) + " of 5 open");
   app.openFile(q.string(), /*pin=*/false);
   check(app.m_open.isOpen(p.string()), "so browsing past it keeps it open");
   check(app.m_open.isOpen(q.string()), "and the new file is on screen");

   fs::remove_all(dir);
   ImGui::DestroyContext();

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}