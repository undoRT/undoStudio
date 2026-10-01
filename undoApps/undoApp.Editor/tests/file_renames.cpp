// What happens to a tab when the file behind it is renamed, moved or deleted.
//
// Every part of an open file is filed under its path: the tab's ImGui id, the key its
// stashed text is held under, the path the editor is told to save to, and the entry in
// the recent files list. A rename moves the file and none of those, and each one left
// behind is not a stale label but a second file — a save after a rename wrote
// a copy where the file used to be, and a tab sat there offering to open something that
// could not be opened.
//
// So the checks are about the whole set rather than about the tree: the model follows
// the file, the stashes follow it, the editor on screen is told before anything else,
// and a file that is gone takes its tab with it. The folder case is here because a
// rename of a folder carries every file inside it, and it is the same bookkeeping with
// a prefix.

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define private public
#include <TextEditor.h>
#include "undoAppEditor.hpp"
#undef private

#include "undoStudio/core/RecentFiles.hpp"

using namespace undoApp::Editor;
using undoApp::DocKind;
using undoApp::OpenDocument;
using undoApp::TextApp;
using undoStudio::core::RecentFiles;
using undoApp::ST::STApp;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what)
{
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) {
      ++failures;
   }
}

static void writeFile(const fs::path& p, const std::string& body)
{
   fs::create_directories(p.parent_path());
   std::ofstream out(p);
   out << body;
}

static std::string readFile(const fs::path& p)
{
   std::ifstream in(p);
   return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// @brief The open document at a path, or nullptr
static const OpenDocument* openDocumentAt(EditorApp& app, const std::string& path)
{
   for (const OpenDocument& doc : app.m_open.documents()) {
      if (doc.path == path) {
         return &doc;
      }
   }
   return nullptr;
}

int main()
{
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGui::GetIO().Fonts->AddFontDefault();
   ImGui::GetIO().Fonts->Build();

   // A working directory of its own before anything is written: the recent files list
   // is kept in the state file beside the working directory.
   const fs::path state = fs::temp_directory_path() / "undoStudio-file-renames";
   fs::remove_all(state);
   fs::create_directories(state);
   const fs::path cwd = fs::current_path();
   fs::current_path(state);

   const fs::path dir = fs::temp_directory_path() / "undoStudio-file-renames-files";
   fs::remove_all(dir);
   const fs::path st = dir / "Main.st";
   const fs::path txt = dir / "Notes.txt";
   const fs::path json = dir / "Config.json";
   writeFile(st, "PROGRAM Main\nVAR\n  mv : INT;\nEND_VAR\nmv := 1;\nEND_PROGRAM\n");
   writeFile(txt, "notes\n");
   writeFile(json, "{\"a\": 1}\n");

   auto& app = EditorApp::getInstance();
   check(app.initialize(), "the editor app initializes");
   STApp::getInstance().setupEditors();
   auto& recents = RecentFiles::getInstance();
   recents.clearRecentFiles();

   // --- a rename follows the tab, the stash and the editor ---
   //
   app.openFile(txt.string());
   const fs::path renamed = dir / "Renamed.txt";
   app.renameFile(txt.string(), "Renamed.txt");

   check(fs::exists(renamed), "the file moved on disk");
   check(!fs::exists(txt), "and is gone from the old name");
   check(app.m_open.size() == 1, "the tab is still there, got " + std::to_string(app.m_open.size()));
   check(app.m_open.isOpen(renamed.string()), "and it is filed under the new path");
   check(!app.m_open.isOpen(txt.string()), "the old path is no longer in the model");
   check(app.m_open.active() == renamed.string(), "and it is still the file on screen");
   check(app.m_currentFilePath == renamed.string(), "the editor agrees about where it is, got '" +
                                                          app.m_currentFilePath + "'");

   // The consequence that matters: a save must not write a second file where the
   // original used to be, and what it does write has to be the text on screen rather
   // than the text the file happened to have when it was opened -- the editor adds a
   // newline of its own, so comparing against the original bytes would fail for a
   // reason that has nothing to do with the rename.
   const std::string onScreen = TextApp::getInstance().m_editor->GetText();
   app.saveActiveDocument();
   check(!fs::exists(txt), "a save after the rename does not recreate the old file");
   check(readFile(renamed) == onScreen,
         "and the text on screen is what landed in the renamed file, got '" + readFile(renamed) + "'");

   // --- a stash follows too, which is the same rule one step later ---
   //
   // The text of a document that is not on screen lives in a stash, under the path it
   // was opened with and with that path inside it. Both are the file, so a rename that
   // moved only the tab would hand the old path back to the backend the next time the
   // file was opened, and that save would land on the old name.
   app.openFile(st.string());
   check(app.m_open.size() == 2, "a second file is open, got " + std::to_string(app.m_open.size()));
   const fs::path movedSt = dir / "Moved" / "Main.st";
   fs::create_directories(movedSt.parent_path());
   app.moveFile(st.string(), (dir / "Moved").string());

   check(app.m_open.isOpen(movedSt.string()), "the moved file's tab followed it into the folder");
   check(app.m_open.active() == movedSt.string(), "and it is on screen");
   check(app.m_textStashes.count(st.string()) == 0 && app.m_stStashes.count(st.string()) == 0,
         "nothing is stashed under the old path");
   check(readFile(movedSt).find("PROGRAM Main") != std::string::npos, "the file arrived whole");

   // The stash that was taken when the second file was opened belongs to the first
   // one, so the first one has to have been renamed under it for this to be about
   // anything. Renaming it now and coming back to it proves the path inside the
   // document moved with the key.
   const fs::path renamedBack = dir / "RenamedAgain.txt";
   app.renameFile(renamed.string(), "RenamedAgain.txt");
   check(app.m_open.isOpen(renamedBack.string()), "a stashed file follows its rename, got " +
                                                    std::to_string(app.m_open.size()) + " tabs");
   app.openFile(renamedBack.string());
   check(app.getCurrentFilePath() == renamedBack.string(), "and comes back on screen");
   app.saveActiveDocument();
   check(!fs::exists(renamed), "saving it does not write the pre-rename path either");

   // --- a folder carries its files with it ---
   //
   fs::create_directories(dir / "Sub" / "Deep");
   const fs::path nested = dir / "Sub" / "Deep" / "Leaf.txt";
   writeFile(nested, "leaf\n");
   app.openFile(nested.string());
   app.renameFile((dir / "Sub").string(), "RenamedSub");

   const fs::path nestedAfter = dir / "RenamedSub" / "Deep" / "Leaf.txt";
   check(app.m_open.isOpen(nestedAfter.string()), "a tab inside a renamed folder follows it, got " +
                                                        std::to_string(app.m_open.size()) + " tabs");
   check(!app.m_open.isOpen(nested.string()), "and the old path is gone from the model");

   // --- a file that is deleted takes its tab with it ---
   //
   // A tab for a file that is gone is a row offering to open something that cannot be
   // opened, and it stays in the bar until the session ends.
   const size_t before = app.m_open.size();
   fs::remove(nestedAfter);
   app.closeDocumentsUnder(nestedAfter.string());
   check(app.m_open.size() == before - 1, "deleting a file closes its tab, got " +
                                            std::to_string(app.m_open.size()) + " tabs, was " +
                                            std::to_string(before));
   check(!app.m_open.isOpen(nestedAfter.string()), "and it is no longer in the bar");

   // Deleting a folder takes every file in it, which is the same rule with a prefix.
   const fs::path victim = dir / "Doomed" / "One.txt";
   const fs::path victimTwo = dir / "Doomed" / "Two.txt";
   writeFile(victim, "one\n");
   writeFile(victimTwo, "two\n");
   app.openFile(victim.string());
   app.openFile(victimTwo.string());
   const size_t withBoth = app.m_open.size();
   fs::remove_all(dir / "Doomed");
   app.closeDocumentsUnder((dir / "Doomed").string());
   check(app.m_open.size() == withBoth - 2, "deleting a folder closes the tabs inside it, got " +
                                                 std::to_string(app.m_open.size()) + ", was " +
                                                 std::to_string(withBoth));

   // --- closing everything means no tabs, not tabs without editors ---
   //
   // Closing a project used to leave the bar full of the previous project's files,
   // and clicking one asked a backend for a file that was not there.
   app.openFile(movedSt.string());
   check(app.m_open.size() > 0, "there is something open, got " + std::to_string(app.m_open.size()));
   app.closeProject();
   check(app.m_open.size() == 0, "closing the project clears the tabs, got " +
                                     std::to_string(app.m_open.size()));
   check(app.m_currentFilePath.empty(), "and leaves nothing on screen");

   // --- open as text is a tab like any other ---
   //
   // It used to close the editors and load the text behind their backs: no tab, no
   // name in the bar, and nothing in the list to ask for it again.
   const size_t tabsBeforeJson = app.m_open.size();
   recents.clearRecentFiles();
   app.openFile(json.string());
   check(app.m_open.size() == tabsBeforeJson + 1, "a .json opens as a viewer tab, got " +
                                                      std::to_string(app.m_open.size()));
   const OpenDocument* asJson = openDocumentAt(app, json.string());
   check(asJson != nullptr && asJson->kind == DocKind::JSON, "and it is filed as a JSON document");

   app.openFileAsText(json.string());
   check(app.m_open.size() == tabsBeforeJson + 1, "opening it as text does not add a second tab for one file, got " +
                                                        std::to_string(app.m_open.size()));
   const OpenDocument* asText = openDocumentAt(app, json.string());
   check(asText != nullptr && asText->kind == DocKind::Text, "the tab is the text one now");
   check(app.getCurrentFilePath() == json.string(), "and the text is on screen, got '" +
                                                        app.getCurrentFilePath() + "'");
   check(app.m_currentFileType == FileType::Text, "as a text editor, got " +
                                                     std::to_string(static_cast<int>(app.m_currentFileType)));
   check(app.m_textStashes.count(json.string()) == 0, "with the viewer's stash dropped");

   // And it is a file like any other for the list, which is the half that was missing.
   check(recents.recentFiles().size() == 1 && recents.recentFiles().front() == fs::canonical(json).string(),
         "opening it as text put it in the recent files, got " +
             std::to_string(recents.recentFiles().size()) + " entries");

   // --- a rename of it is tracked like any other file ---
   const fs::path jsonRenamed = dir / "Renamed.json";
   app.renameFile(json.string(), "Renamed.json");
   check(app.m_open.isOpen(jsonRenamed.string()), "a JSON tab follows a rename too");
   check(recents.recentFiles().size() == 1 && recents.recentFiles().front() == fs::canonical(jsonRenamed).string(),
         "and the list points at the new path");

   fs::current_path(cwd);
   fs::remove_all(dir);
   fs::remove_all(state);
   ImGui::DestroyContext();

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}