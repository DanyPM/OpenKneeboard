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
#include <OpenKneeboard/DCSTab.h>
#include <OpenKneeboard/GameEvent.h>
#include <OpenKneeboard/Games/DCSWorld.h>
#include <OpenKneeboard/RuntimeFiles.h>
#include <OpenKneeboard/dprint.h>
#include <fmt/xchar.h>
#include <shims/winrt.h>
#include <shims/wx.h>
#include <wx/msgdlg.h>

#include <algorithm>
#include <filesystem>

using DCS = OpenKneeboard::Games::DCSWorld;
using namespace OpenKneeboard;

static bool FilesDiffer(
  const std::filesystem::path& a,
  const std::filesystem::path& b) {
  const auto size = std::filesystem::file_size(a);
  if (std::filesystem::file_size(b) != size) {
    return true;
  }

  winrt::handle ah {CreateFile(
    a.c_str(),
    GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL)};
  winrt::handle bh {CreateFile(
    b.c_str(),
    GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE,
    nullptr,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL)};

  winrt::handle afm {
    CreateFileMappingW(ah.get(), nullptr, PAGE_READONLY, 0, 0, nullptr)};
  winrt::handle bfm {
    CreateFileMappingW(bh.get(), nullptr, PAGE_READONLY, 0, 0, nullptr)};

  auto av = reinterpret_cast<std::byte*>(
    MapViewOfFile(afm.get(), FILE_MAP_READ, 0, 0, 0));
  auto bv = reinterpret_cast<std::byte*>(
    MapViewOfFile(bfm.get(), FILE_MAP_READ, 0, 0, 0));

  auto equal = std::equal(av, av + size, bv, bv + size);

  UnmapViewOfFile(av);
  UnmapViewOfFile(bv);

  return !equal;
}

static void InstallHooks(DCS::Version version, const wxString& label) {
  const auto baseDir = DCS::GetSavedGamesPath(version);
  if (!std::filesystem::is_directory(baseDir)) {
    return;
  }

  const auto hookDir = baseDir / "Scripts" / "Hooks";
  const auto dllDest = hookDir / RuntimeFiles::DCSWORLD_HOOK_DLL;
  const auto luaDest = hookDir / RuntimeFiles::DCSWORLD_HOOK_LUA;

  wchar_t buffer[MAX_PATH];
  GetModuleFileNameW(NULL, buffer, MAX_PATH);
  const auto exeDir = std::filesystem::path(buffer).parent_path();
  const auto dllSource = exeDir / RuntimeFiles::DCSWORLD_HOOK_DLL;
  const auto luaSource = exeDir / RuntimeFiles::DCSWORLD_HOOK_LUA;

  std::wstring message;
  if (!(std::filesystem::exists(dllDest) && std::filesystem::exists(luaDest))) {
    message = fmt::format(
      _("Required hooks aren't installed for {}; would you like to install "
        "them?")
        .ToStdWstring(),
      label.ToStdWstring());
  } else if (
    FilesDiffer(dllSource, dllDest) || FilesDiffer(luaSource, luaDest)) {
    message = fmt::format(
      _("Hooks for {} are out of date; would you like to update "
        "them?")
        .ToStdWstring(),
      label.ToStdWstring());
  } else {
    // Installed and equal
    return;
  }

  wxMessageDialog dialog(
    nullptr, message, "OpenKneeboard", wxOK | wxCANCEL | wxICON_WARNING);
  if (dialog.ShowModal() != wxID_OK) {
    return;
  }

  std::filesystem::create_directories(hookDir);
  std::filesystem::copy_file(
    dllSource, dllDest, std::filesystem::copy_options::overwrite_existing);
  std::filesystem::copy_file(
    luaSource, luaDest, std::filesystem::copy_options::overwrite_existing);
}

static void InstallHooks() {
  static bool sFirstRun = true;
  if (!sFirstRun) {
    return;
  }
  sFirstRun = false;

  InstallHooks(DCS::Version::OPEN_BETA, _("DCS World (Open Beta)"));
  InstallHooks(DCS::Version::STABLE, _("DCS World (Stable)"));
}

namespace OpenKneeboard {

DCSTab::DCSTab() {
  InstallHooks();
}

void DCSTab::PostGameEvent(const GameEvent& event) {
  if (event.name == this->GetGameEventName()) {
    mCurrentConfig.mValue = event.value;
    Update();
    return;
  }

  if (event.name == DCS::EVT_INSTALL_PATH) {
    mCurrentConfig.mInstallPath = std::filesystem::canonical(event.value);
    Update();
    return;
  }

  if (event.name == DCS::EVT_SAVED_GAMES_PATH) {
    mCurrentConfig.mSavedGamesPath = std::filesystem::canonical(event.value);
    Update();
    return;
  }

  if (event.name == DCS::EVT_SIMULATION_START) {
    this->OnSimulationStart();
    return;
  }
}

void DCSTab::Update() {
  auto c = mCurrentConfig;
  if (c == mLastValidConfig) {
    return;
  }

  if (c.mInstallPath.empty()) {
    return;
  }

  if (c.mSavedGamesPath.empty()) {
    return;
  }

  if (c.mValue == mLastValue) {
    return;
  }
  mLastValue = c.mValue;

  this->Update(c.mInstallPath, c.mSavedGamesPath, c.mValue);
}

void DCSTab::OnSimulationStart() {
}

}// namespace OpenKneeboard
