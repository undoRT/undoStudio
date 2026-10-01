/**
 * @file workspace_fixture.hpp
 * @brief A self-contained workspace for the tests that need one
 * @author Salvatore Bamundo
 * @date September 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

//
// The navigation and reveal tests used to point at directories that only existed
// on the machine that wrote them (/tmp/opencode/ws2 and an absolute path under
// ~/Desktop). On any other checkout they indexed nothing, and reveal_selection
// dereferenced the null declaration it got back and segfaulted. A fixture the
// test writes itself makes them reproducible and explains the layout they assert
// on, instead of leaving it implicit in a path nobody can read.
#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace undoApp::tests {

/// Writes a two-file workspace under a temporary directory and returns its path.
///
/// Lib.st declares the FUNCTION_BLOCK Helper with the method Twice and the
/// parameter seed. User.st declares h : Helper and n : INT, so opening it
/// exercises both same-file and cross-file lookups.
inline std::filesystem::path writeWorkspaceFixture(const std::string& dirName)
{
   namespace fs = std::filesystem;
   const fs::path dir = fs::temp_directory_path() / dirName;
   std::error_code ec;
   fs::remove_all(dir, ec);
   fs::create_directories(dir);

   {
      std::ofstream out(dir / "Lib.st");
      // The declaration has to sit on line 1: the tests assert on that line.
      out << "FUNCTION_BLOCK Helper\n"
             "VAR_INPUT\n"
             "    seed : INT;\n"
             "END_VAR\n"
             "METHOD PUBLIC Twice : INT\n"
             "    Twice := seed * 2;\n"
             "END_METHOD\n"
             "END_FUNCTION_BLOCK\n";
   }
   {
      std::ofstream out(dir / "User.st");
      out << "PROGRAM User\n"
             "VAR\n"
             "    h : Helper;\n"
             "    n : INT;\n"
             "END_VAR\n"
             "END_PROGRAM\n";
   }
   return dir;
}

/// Writes undoFB.st / undoPRG.st, the pair the FB tab and cross-file tests need.
///
/// undoFB keeps the shape the assertions depend on: the POU variable sits on
/// line 10 and the method variable on line 16, so the two undoVar declarations
/// shadow each other across scopes. undoPRG instantiates undoFB as undoFB_, which
/// only resolves because the block is indexed from the sibling file.
inline std::filesystem::path writeBlockFixture(const std::string& dirName)
{
   namespace fs = std::filesystem;
   const fs::path dir = fs::temp_directory_path() / dirName;
   std::error_code ec;
   fs::remove_all(dir, ec);
   fs::create_directories(dir);

   {
      std::ofstream out(dir / "undoFB.st");
      out << "FUNCTION_BLOCK undoFB\n"
             "\n"
             "VAR_INPUT\n"
             "END_VAR\n"
             "VAR_OUTPUT\n"
             "END_VAR\n"
             "VAR_IN_OUT\n"
             "END_VAR\n"
             "VAR\n"
             "\tundoVar : INT;\n"
             "END_VAR\n"
             "\n"
             "\n"
             "METHOD undoMeth : INT\n"
             "VAR\n"
             "\tundoVar :  INT;\n"
             "END_VAR\n"
             "VAR_INPUT\n"
             "\tundoVarIn : INT;\n"
             "END_VAR\n"
             "VAR_OUTPUT\n"
             "\tundoVarOut : INT;\n"
             "END_VAR\n"
             "\n"
             "undoMeth := undoVar;\n"
             "END_METHOD\n"
             // A second method, for the tab-switching test. It sits after undoMeth
             // on purpose: method_tab asserts the POU and method undoVar line
             // numbers, which any method placed before them would shift.
             "METHOD meth2 : INT\n"
             "meth2 := undoVar * 2;\n"
             "END_METHOD\n"
             "END_FUNCTION_BLOCK\n";
   }
   {
      std::ofstream out(dir / "undoPRG.st");
      out << "PROGRAM undoPRG\n"
             "\n"
             "VAR\n"
             "   undoVar1 : INT;\n"
             "   undoVar2: REAL;\n"
             "   undoVar3 : STRING;\n"
             "   undoFB_ : undoFB;\n"
             "END_VAR\n"
             "\n"
             "\n"
             "undoVar1 := undoVar1 + 1;\n";
   }
   return dir;
}

} // namespace undoApp::tests
