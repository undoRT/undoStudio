/**
 * @file Settings.hpp
 * @brief Reading and writing the IDE's own state files
 * @ingroup core
 *
 * One small format, used by the files the IDE writes for itself:
 *
 * ~~~ini
 * [window]
 * width=1600
 * ~~~
 *
 * It is the same shape Dear ImGui writes its own layout in, which is not an
 * accident: the layout next to it is the file a user is most likely to be looking
 * at when they wonder where this one went, and the two should read alike.
 *
 * A write is a read-modify-write of the whole file, so two owners with their own
 * sections cannot lose each other's values by saving at the same time. That
 * matters because the window and the recent projects are both core state that
 * gets written on the way out, and they are written from different places.
 *
 * @author Salvatore Bamundo
 * @date July 2026
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: Copyright (c) 2026 undoRT
 */

#pragma once

#include <string>
#include <vector>

namespace undoStudio {
namespace core {
namespace settings {

/// @brief The file the core keeps its own state in
extern const char* const kCoreFile;

/**
 * @brief Read a string
 * @param file    File to read
 * @param section Section to look in
 * @param key     Key to look for
 * @param def     Value to return when it is not there
 * @return The stored value, or def
 */
std::string getString(const std::string& file, const std::string& section, const std::string& key,
                      const std::string& def = "");

/**
 * @brief Read an integer
 * @param file    File to read
 * @param section Section to look in
 * @param key     Key to look for
 * @param def     Value to return when it is not there or is not a number
 * @return The stored value, or def
 */
int getInt(const std::string& file, const std::string& section, const std::string& key, int def);

/**
 * @brief Read every value of a key, in the order they appear
 * @param file    File to read
 * @param section Section to look in
 * @param key     Key to collect
 * @return The values, possibly empty
 */
std::vector<std::string> getList(const std::string& file, const std::string& section, const std::string& key);

/**
 * @brief Store a string, keeping the rest of the file as it is
 * @param file    File to write
 * @param section Section to write into, created if it is not there
 * @param key     Key to write
 * @param value   Value to store
 */
void setString(const std::string& file, const std::string& section, const std::string& key, const std::string& value);

/**
 * @brief Store an integer
 * @param file    File to write
 * @param section Section to write into
 * @param key     Key to write
 * @param value   Value to store
 */
void setInt(const std::string& file, const std::string& section, const std::string& key, int value);

/**
 * @brief Replace every value of a key, keeping the rest of the file as it is
 * @param file    File to write
 * @param section Section to write into
 * @param key     Key to write
 * @param values  Values to store, in order
 */
void setList(const std::string& file, const std::string& section, const std::string& key,
             const std::vector<std::string>& values);

} // namespace settings
} // namespace core
} // namespace undoStudio
