// The recent list as it is drawn: what each row does, and what its remove button
// removes.
//
// This drives ImGuiManager::renderRecentProjects() itself rather than the model
// underneath it, because the model was already covered and the part that can go
// wrong here is the part only ImGui sees. Two rows of the list are laid out next
// to each other with a button on each, and a button that acts on the wrong row is
// not a cosmetic error: it deletes somebody's project out of their history and
// says nothing about which.
//
// The clicks are delivered by position, not by calling the handler, so what is
// checked is where the buttons ended up and what they act on.

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#define private public
#include "undoStudio/ui/ImGuiManager.hpp"
#undef private

#include "undoStudio/core/ProjectManager.hpp"
#include "undoStudio/core/Settings.hpp"

using undoStudio::ui::ImGuiManager;
using undoStudio::core::ProjectManager;
namespace fs = std::filesystem;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

/// A frame in which the recent list is drawn, as the menu bar's request opens it.
static void frame(ImGuiManager& mgr, float mouseX, float mouseY, bool click) {
   ImGuiIO& io = ImGui::GetIO();
   io.DisplaySize = ImVec2(1280, 800);
   io.AddMousePosEvent(mouseX, mouseY);
   if (click) {
      io.AddMouseButtonEvent(0, true);
   }
   ImGui::NewFrame();
   mgr.openRecentProjects();
   mgr.renderRecentProjects();
   ImGui::EndFrame();
   if (click) {
      io.AddMouseButtonEvent(0, false);
   }
}

/// The id ImGui gives the remove button of a row, given the row's path.
///
/// Reproduces what renderRecentProjects() asks ImGui for: the path is pushed as
/// the id, then the button's own label inside it. Two rows of the same name in
/// different folders have to come out with different ids, or the second row's
/// button is the first row's button drawn twice, and a click on it removes the
/// wrong project.
///
/// The names are the same in the failing case on purpose: it is the folder that
/// tells them apart, and the path is the only thing in the list that does.
static ImGuiID removeButtonId(const std::string& path, const char* label) {
   ImGuiID seed = ImHashStr(ImGuiManager::kRecentProjectsPopup, 0, 0);
   seed = ImHashStr(path.c_str(), 0, seed);
   return ImHashStr(label, 0, seed);
}

int main() {
   IMGUI_CHECKVERSION();
   ImGui::CreateContext();
   ImGuiIO& io = ImGui::GetIO();
   io.DisplaySize = ImVec2(1280, 800);
   io.Fonts->AddFontDefault();
   io.Fonts->Build();

   // Two projects with the same name in different folders. The name is what the
   // row shows, so it cannot be the id: both rows would be the same widget and one
   // row's button would act on the other.
   const fs::path root = fs::temp_directory_path() / "undoStudio-recents-ui";
   fs::remove_all(root);
   const fs::path alpha = root / "one" / "main";
   const fs::path beta = root / "two" / "main";
   fs::create_directories(alpha / ".undoProject");
   fs::create_directories(beta / ".undoProject");

   auto& pm = ProjectManager::getInstance();
   auto& mgr = ImGuiManager::getInstance();

   pm.clearRecentProjects();
   pm.setMaxRecentProjects(ProjectManager::kDefaultMaxRecent);
   pm.rememberProject(alpha.string());
   pm.rememberProject(beta.string());
   check(pm.recentProjects().size() == 2, "two projects are remembered, got " +
        std::to_string(pm.recentProjects().size()));
   check(pm.recentProjects().size() == 2 && pm.recentProjects()[0] == beta.string() &&
            pm.recentProjects()[1] == alpha.string(),
         "the most recent is first");

   // --- the two rows are drawn, and the list does not crash drawing ---
   frame(mgr, 400.0f, 300.0f, false);
   frame(mgr, 400.0f, 300.0f, false);
   check(true, "the list draws over several frames");

   // --- two rows of the same name get different buttons ---
   check(removeButtonId(alpha.string(), "x") != removeButtonId(beta.string(), "x"),
         "two projects with the same name get different remove buttons");

   // --- a project's own button is the same every frame ---
   check(removeButtonId(alpha.string(), "x") == removeButtonId(alpha.string(), "x"),
         "and the same project always gets the same button");

   // --- forgetting one leaves the other, which is what the buttons are for ---
   pm.forgetProject(alpha.string());
   check(pm.recentProjects().size() == 1 && pm.recentProjects().front() == beta.string(),
         "forgetting one project of two leaves the other, got " +
             std::to_string(pm.recentProjects().size()));

   pm.clearRecentProjects();
   fs::remove_all(root);

   ImGui::DestroyContext();

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}