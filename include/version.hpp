/**
 * @file version.hpp
 * @brief Version information for undoStudio compiler
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2025-2026 Salvatore Bamundo
 */

#pragma once

#include <string>

// Version numbers following Semantic Versioning (semver.org)
#define STUDIO_VERSION_MAJOR  0
#define STUDIO_VERSION_MINOR  1
#define STUDIO_VERSION_PATCH  0
#define STUDIO_VERSION_PREREL ""

// Helper macros for stringification (workaround for MSVC)
#define STUDIO_STRINGIFY_IMPL(x) #x
#define STUDIO_STRINGIFY(x)      STUDIO_STRINGIFY_IMPL(x)

// Version string for display
#define STUDIO_VERSION_STRING \
   STUDIO_STRINGIFY(STUDIO_VERSION_MAJOR) \
   "." STUDIO_STRINGIFY(STUDIO_VERSION_MINOR) "." STUDIO_STRINGIFY(STUDIO_VERSION_PATCH) STUDIO_VERSION_PREREL

// Build date (automatically updated by compiler)
#define STUDIO_BUILD_DATE __DATE__ " " __TIME__

// Compiler information
#ifdef __clang__
#define STUDIO_COMPILER "Clang " __clang_version__
#elif defined(__GNUC__)
#define STUDIO_COMPILER "GCC " __VERSION__
#elif defined(_MSC_VER)
// For MSVC, _MSC_VER is a number, need to stringify it
#define STUDIO_COMPILER "MSVC " STUDIO_STRINGIFY(_MSC_VER)
#else
#define STUDIO_COMPILER "Unknown"
#endif

/**
 * @brief Get version as a string
 * @return Version string (e.g., "1.0.0")
 */
inline std::string getVersion()
{
   return STUDIO_VERSION_STRING;
}

/**
 * @brief Get full version information
 * @return Detailed version string with compiler and build date
 */
inline std::string getFullVersion()
{
   return std::string("undoStudio version ") + STUDIO_VERSION_STRING + " (" + STUDIO_COMPILER + ", built " + STUDIO_BUILD_DATE + ")";
}

/**
 * @brief Get version numbers as tuple
 * @return Struct with major, minor, patch
 */
struct Version
{
   int major = STUDIO_VERSION_MAJOR;
   int minor = STUDIO_VERSION_MINOR;
   int patch = STUDIO_VERSION_PATCH;

   std::string toString() const { return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch); }
};

inline Version getVersionNumbers()
{
   return Version{};
}