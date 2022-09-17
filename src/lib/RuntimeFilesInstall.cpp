/*
 * OpenKneeboard
 *
 * Copyright (C) 2022 Fred Emmott <fred@fredemmott.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */
#include <OpenKneeboard/FilesDiffer.h>
#include <OpenKneeboard/Filesystem.h>
#include <OpenKneeboard/RuntimeFiles.h>
#include <OpenKneeboard/dprint.h>
#include <shims/winrt/base.h>

namespace OpenKneeboard::RuntimeFiles {

void Install() {
  const auto source = Filesystem::GetRuntimeDirectory();
  const auto destination = RuntimeFiles::GetInstallationDirectory();
  std::filesystem::create_directories(destination);

#define IT(file) \
  if (FilesDiffer(source / file, destination / file)) { \
    std::filesystem::copy( \
      source / file, \
      destination / file, \
      std::filesystem::copy_options::overwrite_existing); \
  }

  OPENKNEEBOARD_PUBLIC_RUNTIME_FILES
#undef IT
}

}// namespace OpenKneeboard::RuntimeFiles
