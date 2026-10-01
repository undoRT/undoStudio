/**
 * @file function_return_type.cpp
 * @brief A FUNCTION's return type survives being written out and read back in
 * @author Salvatore Bamundo
 * @date October 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

// Creating a FUNCTION produced "Error in file header or footer" the moment the
// file was parsed, and the message named the wrong region: the diagnostic was on
// the line below the POU declaration, which is still the header, so it was right
// by accident.
//
// Two things had to be true for that file to fail, and neither is visible from
// the error:
//
//   - the generator wrote `FUNCTION name : ` with nothing after the colon, and
//     the parser reads a type after that colon unconditionally. The file it
//     generated was therefore not ST, whatever the return type field said.
//   - the return type was never read back when a file was opened, so the field
//     was empty even for a FUNCTION whose name and type were both on disk.
//
// The first alone is enough to break every FUNCTION; the second alone is enough
// to break every FUNCTION after a reopen. Both are checked here, and the parse
// of a freshly created FUNCTION is checked too, because that is the sequence the
// report describes and neither half of it is visible in the generated text alone.

#include <imgui.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#define private public
#include <TextEditor.h>
#include "undoAppST.hpp"
#undef private

using namespace undoApp::ST;

static int failures = 0;

static void check(bool ok, const std::string& what) {
  std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
  if (!ok) ++failures;
}

static std::string firstLine(const std::string& text) {
  const size_t nl = text.find('\n');
  return nl == std::string::npos ? text : text.substr(0, nl);
}

static std::string writeFixture(const std::filesystem::path& p, const char* content) {
  std::ofstream out(p);
  out << content;
  return p.string();
}

int main() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().Fonts->AddFontDefault();
  ImGui::GetIO().Fonts->Build();

  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "undoStudio-function-return-type";
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir);

  // --- a FUNCTION opened from disk ---
  const auto onDisk = writeFixture(dir / "Scale.st",
      "FUNCTION Scale : REAL\n"
      "VAR_INPUT\n"
      "    x : INT;\n"
      "END_VAR\n"
      "Scale := x;\n"
      "END_FUNCTION\n");

  STApp app;
  app.setupEditors();
  app.openFile(onDisk);

  check(app.m_pouType == POUType::Function, "opened as a FUNCTION");
  check(app.m_functionReturnType == "REAL",
        "the return type is read from the header line, got '" + app.m_functionReturnType + "'");

  // The parse is the assertion that matters: an error here is what the user saw.
  check(app.m_errors.empty(),
        "a FUNCTION with a return type parses clean, got " + std::to_string(app.m_errors.size()) + " error(s)");
  for (const auto& e : app.m_errors) {
    std::printf("       line %d: %s\n", e.line, e.what());
  }

  // Every diagnostic has to reach an editor. One that lands outside every
  // segment cannot, and is reported as a hint on line 1 instead - which is how
  // this bug reached the user as a message about the header.
  int unmapped = 0;
  for (const auto& e : app.m_errors) {
    if (app.segmentForLine(e.line) == nullptr) ++unmapped;
  }
  check(unmapped == 0, "no diagnostic falls outside every editor segment");

  // --- the generated text ---
  //
  // The generator is what the parser is handed, so this is checked directly:
  // a correct m_functionReturnType that the generator ignores would pass the
  // parse of the file on disk and fail here.
  const std::string generated = app.generateSTFile();
  check(firstLine(generated) == "FUNCTION Scale : REAL",
        "the generated header carries the return type, got '" + firstLine(generated) + "'");

  // --- the sequence the report describes: create, then parse ---
  const auto created = writeFixture(dir / "NewFn.st",
      "FUNCTION NewFn : INT\n"
      "VAR_INPUT\n"
      "END_VAR\n"
      "VAR_OUTPUT\n"
      "END_VAR\n"
      "VAR_IN_OUT\n"
      "END_VAR\n"
      "VAR\n"
      "END_VAR\n"
      "\n\n"
      "END_FUNCTION\n");

  STApp fresh;
  fresh.setupEditors();
  fresh.openFile(created);
  check(fresh.m_functionReturnType == "INT",
        "a freshly created FUNCTION reads its own return type back, got '" + fresh.m_functionReturnType + "'");
  check(fresh.m_errors.empty(),
        "a freshly created FUNCTION parses clean, got " + std::to_string(fresh.m_errors.size()) + " error(s)");
  for (const auto& e : fresh.m_errors) {
    std::printf("       line %d: %s\n", e.line, e.what());
  }

  // A return type with spaces around it is what a hand-edited file looks like,
  // and the header line is written by hand as often as by the generator.
  const auto spaced = writeFixture(dir / "Spaced.st",
      "FUNCTION Spaced   :   BOOL\n"
      "VAR\n"
      "END_VAR\n"
      "Spaced := TRUE;\n"
      "END_FUNCTION\n");
  STApp third;
  third.setupEditors();
  third.openFile(spaced);
  check(third.m_functionReturnType == "BOOL",
        "whitespace around the colon is not part of the type, got '" + third.m_functionReturnType + "'");

  // --- a FUNCTION already on disk without a return type ---
  //
  // This is the file the old generator wrote: the colon with nothing after it,
  // saved by a user who had no reason to think anything was wrong with it. It
  // does not parse, and it is the state a project is left in by the bug above,
  // so it has to be repairable rather than a dead end - the field was otherwise
  // only ever asked for in the New POU dialog, on the way in.
  const auto broken = writeFixture(dir / "Broken.st",
      "FUNCTION Broken : \n"
      "VAR_INPUT\n"
      "END_VAR\n"
      "VAR_OUTPUT\n"
      "END_VAR\n"
      "VAR_IN_OUT\n"
      "END_VAR\n"
      "VAR\n"
      "END_VAR\n"
      "\n\n"
      "END_FUNCTION\n");

  STApp repair;
  repair.setupEditors();
  repair.openFile(broken);
  check(repair.m_functionReturnType.empty(),
        "a FUNCTION saved without a return type reports it as empty, got '" + repair.m_functionReturnType + "'");
  check(!repair.m_errors.empty(),
        "and does not parse, which is what the user is looking at");

  repair.m_functionReturnType = "INT";   // what typing into the field does
  repair.validateAndParse();
  check(repair.m_errors.empty(),
        "setting the return type clears the error, got " + std::to_string(repair.m_errors.size()) + " error(s)");
  for (const auto& e : repair.m_errors) {
    std::printf("       line %d: %s\n", e.line, e.what());
  }
  check(firstLine(repair.generateSTFile()) == "FUNCTION Broken : INT",
        "and the generated header is what the parser needs, got '" +
            firstLine(repair.generateSTFile()) + "'");

  ImGui::DestroyContext();
  if (failures == 0) std::printf("RESULT: all checks passed\n");
  return failures == 0 ? 0 : 1;
}