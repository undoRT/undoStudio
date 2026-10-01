// Several files open at once, and none of them loses what was typed in it.
//
// This is the whole feature end to end: open two files, edit both, switch between
// them, and each comes back showing its own text. Opening a second file used to
// close the first, so there was nothing to switch between.
//
// The edits are made through the editors, because that is where a user's edits
// live and nowhere else: m_methods and the file on disk are only written from the
// editors on save, so a snapshot taken from either would hand back the saved
// version and lose the rest. Each file is given a distinctive marker and the
// assertion is that the marker comes back with the right file and not the other.

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#define private public
#include <TextEditor.h>
#include "undoAppEditor.hpp"
#undef private

using namespace undoApp::Editor;
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

int main() {
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::GetIO().Fonts->AddFontDefault();
   ImGui::GetIO().Fonts->Build();

   const fs::path dir = fs::temp_directory_path() / "undoStudio-multi-file";
   std::error_code ec;
   fs::remove_all(dir, ec);
   fs::create_directories(dir);

   const fs::path a = dir / "Alpha.st";
   const fs::path b = dir / "Beta.st";
   const fs::path c = dir / "Gamma.cpp";

   writeFile(a, "FUNCTION_BLOCK Alpha\nVAR\n  av : INT;\nEND_VAR\nav := 1;\nEND_FUNCTION_BLOCK\n");
   writeFile(b, "PROGRAM Beta\nVAR\n  bv : INT;\nEND_VAR\nbv := 2;\nEND_PROGRAM\n");
   writeFile(c, "// gamma source\nint gamma(void) { return 3; }\n");

   auto& app = EditorApp::getInstance();
   // The real entry point. The singleton holds editor pointers that are null until
   // this has run, and a tab switch goes through all of them.
   check(app.initialize(), "the editor app initializes");
   STApp::getInstance().setupEditors();

   // --- open two ST files ---
   app.openFile(a.string(), /*pin=*/false);
   check(app.m_open.size() == 1, "one file open, got " + std::to_string(app.m_open.size()));
   check(app.getCurrentFilePath() == a.string(), "and it is the first one");

   // Browsing, which is now something a caller asks for rather than the default: a
   // click in the tree keeps its tab, so the preview slot is only what a route that
   // means "I am looking through these" gets.
   app.openFile(b.string(), /*pin=*/false);
   check(app.m_open.size() == 1,
         "browsing past a preview does not add a tab, got " + std::to_string(app.m_open.size()));
   check(app.getCurrentFilePath() == b.string(), "and the second is now on screen");
   check(!app.m_open.isOpen(a.string()), "the file passed over is not left open");

   // Pinned: browsing past it should keep it.
   app.openFile(a.string(), /*pin=*/true);
   app.m_open.pin(a.string());
   app.openFile(b.string(), /*pin=*/false);
   check(app.m_open.size() == 2, "a pinned file stays open beside the preview, got " +
         std::to_string(app.m_open.size()));
   check(app.getCurrentFilePath() == b.string(), "with the newest on screen");

   // --- edit both, through the editors ---
   STApp& st = STApp::getInstance();
   check(st.m_pouName == "Beta", "Beta is the file on screen, got '" + st.m_pouName + "'");
   std::string betaVars = st.m_variablesEditor->GetText();
   const size_t endVar = betaVars.rfind("END_VAR");
   check(endVar != std::string::npos, "its variables end with END_VAR");
   betaVars.insert(endVar, "  betaExtra : INT; // marker-beta\n");
   st.m_variablesEditor->SetText(betaVars);

   // Switch to Alpha and edit it too.
   app.switchToTab(a.string());
   check(st.m_pouName == "Alpha", "Alpha is on screen, got '" + st.m_pouName + "'");
   check(st.m_variablesEditor->GetText().find("marker-beta") == std::string::npos,
         "and it is showing its own variables, not Beta's");
   std::string alphaVars = st.m_variablesEditor->GetText();
   alphaVars.insert(alphaVars.rfind("END_VAR"), "  alphaExtra : INT; // marker-alpha\n");
   st.m_variablesEditor->SetText(alphaVars);

   // --- switch back and forth ---
   app.switchToTab(b.string());
   check(st.m_pouName == "Beta", "back on Beta, got '" + st.m_pouName + "'");
   check(st.m_variablesEditor->GetText().find("marker-beta") != std::string::npos,
         "and its unsaved edit is still there");
   check(st.m_variablesEditor->GetText().find("marker-alpha") == std::string::npos,
         "and it has not picked up Alpha's edit");

   app.switchToTab(a.string());
   check(st.m_pouName == "Alpha", "and back on Alpha, got '" + st.m_pouName + "'");
   check(st.m_variablesEditor->GetText().find("marker-alpha") != std::string::npos,
         "with its own unsaved edit still there");

   // --- a file of another kind, in the same set ---
   app.openFile(c.string());
   check(app.m_open.size() == 3, "a C++ file opens beside them, got " + std::to_string(app.m_open.size()));
   check(app.getCurrentFileType() == FileType::Cpp, "and is dispatched to the C++ editor");

   app.switchToTab(a.string());
   check(st.m_pouName == "Alpha", "coming back to an ST file from a C++ one still works, got '" +
         st.m_pouName + "'");
   check(st.m_variablesEditor->GetText().find("marker-alpha") != std::string::npos,
         "with its unsaved edit intact across two other files");

   // --- an ST file after a non-ST one, which is the order a tree produces ---
   //
   // Opening a .cpp then a .st is what happens when the tree is browsed across
   // folders, and it puts a different backend in each state: the C++ editor holds
   // one file and the ST editor another. The stash of the file being left has to
   // land in the stash belonging to its own kind.
   app.closeTab("");
   app.m_open.closeAll();
   app.m_stStashes.clear();
   app.m_textStashes.clear();
   app.m_cppStashes.clear();
   app.m_jsonStashes.clear();
   app.openFile(c.string());
   check(app.getCurrentFileType() == FileType::Cpp, "a .cpp opens, got type " +
         std::to_string((int)app.getCurrentFileType()));
   app.m_open.pin(c.string());
   app.openFile(a.string());
   check(app.getCurrentFileType() == FileType::ST, "then a .st opens as ST, got type " +
         std::to_string((int)app.getCurrentFileType()));
   check(st.m_pouName == "Alpha", "and the ST editor shows it, got '" + st.m_pouName + "'");
   app.switchToTab(c.string());
   check(app.getCurrentFileType() == FileType::Cpp, "back to the .cpp, got type " +
         std::to_string((int)app.getCurrentFileType()));
   app.switchToTab(a.string());
   check(app.getCurrentFileType() == FileType::ST, "and to the .st again, got type " +
         std::to_string((int)app.getCurrentFileType()));
   check(st.m_pouName == "Alpha", "which still shows it, got '" + st.m_pouName + "'");

   // --- a preview with edits is pinned rather than thrown away ---
   //
   // The case that would cost somebody an afternoon: browsing past a file that has
   // changes in it must not be able to discard them.
   app.openFile(dir / "Delta.st");
   {
      std::string d = fs::path(app.getCurrentFilePath()).string();
      writeFile(d, "PROGRAM Delta\nVAR\n  dv : INT;\nEND_VAR\ndv := 4;\nEND_PROGRAM\n");
   }
   app.openFile(dir / "Delta.st");
   app.switchToTab(app.getCurrentFilePath());
   check(app.m_open.size() >= 1, "a new file opens");
   {
      STApp& s2 = STApp::getInstance();
      if (s2.m_variablesEditor) {
         std::string v = s2.m_variablesEditor->GetText();
         v.insert(v.rfind("END_VAR"), "  dv2 : INT; // marker-delta\n");
         s2.m_variablesEditor->SetText(v);
         app.m_open.setDirty(app.getCurrentFilePath(), true);
      }
      const std::string deltaPath = app.getCurrentFilePath();
      app.openFile(dir / "Epsilon.st");
      {
         std::string d = fs::path(app.getCurrentFilePath()).string();
         writeFile(d, "PROGRAM Epsilon\nVAR\n  ev : INT;\nEND_VAR\nev := 5;\nEND_PROGRAM\n");
      }
      app.openFile(dir / "Epsilon.st");
      check(app.m_open.isOpen(deltaPath), "the edited file is still open after browsing past it");
      app.switchToTab(deltaPath);
      check(STApp::getInstance().m_variablesEditor->GetText().find("marker-delta") != std::string::npos,
            "and still has what was typed in it");
   }

   // --- closing ---
   const size_t before = app.m_open.size();
   app.closeTab(app.getCurrentFilePath());
   check(app.m_open.size() == before - 1, "closing a tab takes one off, got " + std::to_string(app.m_open.size()));

   // --- the set survives a long browse ---
   app.closeTab("");
   app.m_open.closeAll();
   check(app.m_open.size() == 0, "closing everything leaves nothing open");
   check(app.m_open.active().empty(), "and nothing active");

   fs::remove_all(dir, ec);
   ImGui::DestroyContext();

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}