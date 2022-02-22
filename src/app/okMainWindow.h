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
#pragma once

#include <OpenKneeboard/CursorEvent.h>
#include <OpenKneeboard/DXResources.h>
#include <shims/wx.h>
#include <wx/frame.h>

#include <memory>
#include <thread>

#include "Events.h"
#include "Settings.h"
#include "okTab.h"

class wxBookCtrlEvent;
class wxNotebook;

namespace OpenKneeboard {
enum class UserAction;
struct GameEvent;
class DirectInputAdapter;
class GamesList;
class InterprocessRenderer;
class KneeboardState;
class TabsList;
class TabletInputAdapter;
}// namespace OpenKneeboard

class okMainWindow final : public wxFrame,
                           private OpenKneeboard::EventReceiver {
 public:
  okMainWindow();
  virtual ~okMainWindow();

 private:
  void OnExit(wxCommandEvent&);
  void PostGameEvent(const OpenKneeboard::GameEvent&);
  void OnShowSettings(wxCommandEvent&);

  void OnToggleVisibility();

  void OnNotebookTabChanged(wxBookCtrlEvent&);
  void OnTabChanged(uint8_t tabIndex);
  void OnUserAction(OpenKneeboard::UserAction);

  void UpdateTabs();

  OpenKneeboard::DXResources mDXR;
  wxNotebook* mNotebook = nullptr;
  OpenKneeboard::Settings mSettings = OpenKneeboard::Settings::Load();

  std::unique_ptr<OpenKneeboard::DirectInputAdapter> mDirectInput;
  std::unique_ptr<OpenKneeboard::TabletInputAdapter> mTabletInput;
  std::unique_ptr<OpenKneeboard::GamesList> mGamesList;
  std::unique_ptr<OpenKneeboard::TabsList> mTabsList;

  std::shared_ptr<OpenKneeboard::KneeboardState> mKneeboard;
  std::unique_ptr<OpenKneeboard::InterprocessRenderer> mInterprocessRenderer;

  std::jthread mGameEventThread;
  std::jthread mOpenVRThread;

  void InitUI();
};
