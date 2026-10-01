// Which file lands in which tab, and what happens to the one that was there.
//
// Opening a file used to close the previous one, so a single click in the tree
// threw away whatever you had been reading. The fix is the preview tab: a file
// opened by browsing takes a slot that the next one replaces, and a tab becomes
// permanent once it has earned it.
//
// What is checked here is the rule and, above all, the case where being wrong
// costs somebody an afternoon of work. A preview with unsaved changes must never
// be dropped, because being dropped means being discarded, and the gesture that
// drops it — clicking the next file in a tree — is one nobody thinks of as
// destructive.

#include <cstdio>
#include <string>
#include <vector>

#include "undoAppOpenDocuments.hpp"

using undoApp::DocKind;
using undoApp::OpenDocuments;

static int failures = 0;

static void check(bool ok, const std::string& what) {
   std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
   if (!ok) ++failures;
}

int main() {
   // --- browsing: one preview slot ---
   //
   // Three files opened by clicking through a tree. The list does not grow: that
   // is the whole point, and a list that did grow would be the old behaviour with
   // tabs drawn on it.
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      check(docs.size() == 1, "one file is open, got " + std::to_string(docs.size()));
      docs.open("/w/b.st", DocKind::ST);
      check(docs.size() == 1, "opening a second file does not add a tab, got " + std::to_string(docs.size()));
      check(docs.active() == "/w/b.st", "and the new one is the active tab");
      docs.open("/w/c.st", DocKind::ST);
      check(docs.size() == 1 && docs.active() == "/w/c.st", "and again, still one tab, on the newest");
      check(!docs.isOpen("/w/a.st"), "the files passed over are not open");
   }

   // --- opening a file that is already open focuses it, never duplicates ---
   //
   // Two tabs on one file would make the tab bar a worse answer than no tab bar:
   // the question it exists for is "what do I have open".
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      docs.pin("/w/a.st");
      docs.open("/w/b.st", DocKind::ST);
      docs.pin("/w/b.st");
      docs.open("/w/c.st", DocKind::ST);
      check(docs.size() == 3, "three files open, got " + std::to_string(docs.size()));

      const auto again = docs.open("/w/a.st", DocKind::ST);
      check(again.alreadyOpen, "reopening an open file reports it as already open");
      check(docs.size() == 3, "and does not add a tab, got " + std::to_string(docs.size()));
      check(docs.active() == "/w/a.st", "it becomes the active one instead");
      check(docs.isOpen("/w/b.st") && docs.isOpen("/w/c.st"), "and the others are still open");
   }

   // --- pinning: what earns a tab its place ---
   //
   // There is one preview slot and it is never two. Pinning a, opening b and then
   // opening c leaves a and c: b was the preview and c took its place, which is
   // the difference between browsing a tree and accumulating tabs.
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      docs.pin("/w/a.st");
      docs.open("/w/b.st", DocKind::ST);
      check(docs.size() == 2, "a pinned tab is not replaced, got " + std::to_string(docs.size()));
      docs.open("/w/c.st", DocKind::ST);
      check(docs.size() == 2, "and the next file takes the preview slot beside it, not a third tab, got " +
            std::to_string(docs.size()));
      check(docs.isOpen("/w/a.st") && docs.isOpen("/w/c.st"),
            "leaving the pinned file and the newest one open");
      check(!docs.isOpen("/w/b.st"), "and not the one that was only ever a preview");
      check(docs.pinnedCount() == 1, "exactly one of the two is pinned, got " + std::to_string(docs.pinnedCount()));
   }

   // --- a preview with unsaved changes survives ---
   //
   // The case that must not be got wrong. Replacing it would discard the changes,
   // and it happens on an ordinary click in a file tree. It survives because
   // editing pins the tab at once, not because opening something checks: by the
   // time a second file is opened the first is no longer in the slot.
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      docs.setDirty("/w/a.st", true);

      const auto outcome = docs.open("/w/b.st", DocKind::ST);
      check(docs.isOpen("/w/a.st"), "a file with unsaved changes is still open");
      check(docs.size() == 2, "so both are open, got " + std::to_string(docs.size()));
      check(docs.pinnedCount() == 1, "and the one holding the changes is the pinned one");
      check(docs.active() == "/w/b.st", "while the new file is the active tab");
      // It was pinned by being edited, so opening something found nothing in the
      // slot and there was no preview to report dropping.
      check(!outcome.droppedPreview, "nothing was dropped from the slot, because a held file was never in it");
   }

   // --- many edited previews all survive, which is the case that would hurt ---
   {
      OpenDocuments docs;
      for (int i = 0; i < 12; ++i) {
         docs.open("/w/f" + std::to_string(i) + ".st", DocKind::ST);
         docs.setDirty("/w/f" + std::to_string(i) + ".st", true);
      }
      check(docs.size() == 12, "twelve files opened and edited are all still open, got " +
            std::to_string(docs.size()));
      check(docs.pinnedCount() == 12, "and all of them are pinned, got " + std::to_string(docs.pinnedCount()));
   }

   // --- a clean preview is dropped, and the caller is told which ---
   //
   // The caller owns the editors, so it has to be told which file just went away:
   // that is the one whose state can be thrown away, and guessing which one from
   // the model would be a second place to get it wrong.
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      const auto outcome = docs.open("/w/b.st", DocKind::ST);
      check(outcome.droppedPreview, "a clean preview reports that the slot was reused");
      check(outcome.droppedPath == "/w/a.st",
            "and names the file that went away, got '" + outcome.droppedPath + "'");
   }

   // --- editing pins on its own ---
   //
   // Nobody opens a file deliberately in order to edit nothing in it. The first
   // change should give the tab its place without being asked.
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      check(docs.pinnedCount() == 0, "a freshly browsed file is not pinned");
      docs.setDirty("/w/a.st", true);
      check(docs.pinnedCount() == 1, "editing it pins it, got " + std::to_string(docs.pinnedCount()));
   }

   // --- saving takes the dirty mark off but leaves the tab ---
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      docs.setDirty("/w/a.st", true);
      docs.setClean("/w/a.st");
      check(!docs.documents().front().dirty, "saving clears the unsaved mark");
      check(docs.pinnedCount() == 1, "and the tab stays: it was opened to be worked in");
   }

   // --- closing ---
   {
      OpenDocuments docs;
      for (const char* p : {"/w/a.st", "/w/b.st", "/w/c.st"}) {
         docs.open(p, DocKind::ST);
         docs.pin(p);
      }
      check(docs.active() == "/w/c.st", "the last opened is active");

      const auto closedMiddle = docs.close("/w/b.st");
      check(closedMiddle.closed, "closing a document that is not active works");
      check(docs.size() == 2 && !closedMiddle.activeWasClosed, "and leaves the active one alone");
      check(docs.active() == "/w/c.st", "which is still the same tab");

      const auto closedActive = docs.close("/w/c.st");
      check(closedActive.activeWasClosed, "closing the active one says so");
      check(closedActive.nextActive == "/w/a.st",
            "and moves to the one beside it, not to the end of the list, got '" + closedActive.nextActive + "'");

      check(!docs.close("/w/nope.st").closed, "closing something that is not open does nothing");
   }

   // --- closing the last one leaves nothing active, rather than a stale path ---
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      docs.pin("/w/a.st");
      const auto outcome = docs.close("/w/a.st");
      check(docs.size() == 0, "the last document closes, got " + std::to_string(docs.size()));
      check(outcome.nextActive.empty(), "and nothing is left active");
      check(docs.active().empty(), "rather than a path to a file that is gone");
   }

   // --- closing everything, and knowing how much was unsaved ---
   //
   // The count of unsaved work is what a prompt before quitting is built from, so
   // it has to be right: a prompt that says "nothing to save" while two files have
   // changes in them is the worst version of this.
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      docs.setDirty("/w/a.st", true);
      docs.open("/w/b.st", DocKind::Text);
      docs.pin("/w/b.st");
      docs.open("/w/c.st", DocKind::Text);
      const auto summary = docs.closeAll();
      check(summary.closed == 3, "everything closes, got " + std::to_string(summary.closed));
      check(summary.dirty == 1, "and one of them had unsaved changes, got " + std::to_string(summary.dirty));
      check(docs.size() == 0 && docs.active().empty(), "nothing is left open or active");
   }

   // --- Ctrl+Tab cycles, and wraps ---
   //
   // Cycling that stops at the end makes the shortcut useless in the one case it
   // is wanted: coming back round to where you started.
   {
      OpenDocuments docs;
      for (const char* p : {"/w/a.st", "/w/b.st", "/w/c.st"}) {
         docs.open(p, DocKind::ST);
         docs.pin(p);
      }
      const std::string* next = docs.cycle(1);
      check(next != nullptr && *next == "/w/a.st",
            "cycling forward from the last tab wraps to the first, got '" +
                (next ? *next : std::string("none")) + "'");
      docs.activate("/w/a.st");
      next = docs.cycle(1);
      check(next != nullptr && *next == "/w/b.st", "and from the first it goes to the second");
      next = docs.cycle(-1);
      check(next != nullptr && *next == "/w/a.st", "backwards from the second it wraps to the first");
      check(docs.cycle(1) != nullptr, "cycling something always returns something");
   }

   {
      OpenDocuments docs;
      check(docs.cycle(1) == nullptr, "cycling an empty list gives nothing");
   }

   // --- the kinds are carried, because that is what picks the editor ---
   {
      OpenDocuments docs;
      docs.open("/w/a.st", DocKind::ST);
      docs.pin("/w/a.st");
      docs.open("/w/b.json", DocKind::JSON);
      docs.pin("/w/b.json");
      docs.open("/w/c.cpp", DocKind::Cpp);
      check(docs.documents().size() == 3, "three documents open");
      check(docs.documents()[0].kind == DocKind::ST, "the first is ST");
      check(docs.documents()[1].kind == DocKind::JSON, "the second is JSON");
      check(docs.documents()[2].kind == DocKind::Cpp, "the third is C++");
   }

   // --- an empty path is not a document ---
   {
      OpenDocuments docs;
      const auto outcome = docs.open("", DocKind::Text);
      check(docs.size() == 0, "opening nothing opens nothing, got " + std::to_string(docs.size()));
      check(!outcome.alreadyOpen && !outcome.droppedPreview,
            "and does not disturb the preview slot: a failed open must not evict anything");
      docs.open("/w/a.st", DocKind::ST);
      docs.open("", DocKind::Text);
      check(docs.size() == 1 && docs.active() == "/w/a.st",
            "and leaves what was open alone, got " + std::to_string(docs.size()));
   }

   // --- many files, which is the case that made this worth doing ---
   {
      OpenDocuments docs;
      for (int i = 0; i < 40; ++i) {
         docs.open("/w/f" + std::to_string(i) + ".st", DocKind::ST);
         docs.pin("/w/f" + std::to_string(i) + ".st");
      }
      check(docs.size() == 40, "forty pinned files stay open, got " + std::to_string(docs.size()));
      const std::string* cycled = docs.cycle(1);
      check(cycled != nullptr && *cycled == "/w/f0.st",
            "and cycling reaches the first of them, got '" + (cycled ? *cycled : std::string("none")) + "'");
   }

   if (failures == 0) {
      std::printf("RESULT: all checks passed\n");
      return 0;
   }
   std::printf("RESULT: %d check(s) failed\n", failures);
   return 1;
}