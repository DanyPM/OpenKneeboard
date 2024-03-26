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

#include <OpenKneeboard/DXResources.h>
#include <OpenKneeboard/Events.h>
#include <OpenKneeboard/ProfileSettings.h>
#include <OpenKneeboard/RunnerThread.h>
#include <OpenKneeboard/SHM.h>
#include <OpenKneeboard/Settings.h>
#include <OpenKneeboard/VRConfig.h>

#include <OpenKneeboard/audited_ptr.h>

#include <shims/winrt/base.h>

#include <winrt/Windows.Foundation.h>

#include <memory>
#include <shared_mutex>
#include <thread>
#include <vector>

namespace OpenKneeboard {

enum class UserAction;
class DirectInputAdapter;
class GamesList;
class KneeboardView;
class InterprocessRenderer;
class KneeboardView;
class ITab;
class TabletInputAdapter;
class TabsList;
class UserInputDevice;
struct BaseSetTabEvent;
struct GameEvent;
struct GameInstance;
class GameEventServer;

struct ViewRenderInfo {
  std::shared_ptr<KneeboardView> mView;
  std::optional<SHM::VRLayer> mVR;
  std::optional<SHM::NonVRLayer> mNonVR;
  PixelSize mFullSize;
  bool mIsActiveForInput = false;
};

struct RunningGame {
  DWORD mProcessID = 0;
  std::weak_ptr<GameInstance> mGameInstance;
};

class KneeboardState final
  : private EventReceiver,
    public std::enable_shared_from_this<KneeboardState> {
 public:
  KneeboardState() = delete;
  static std::shared_ptr<KneeboardState> Create(
    HWND mainWindow,
    const audited_ptr<DXResources>&);
  ~KneeboardState() noexcept;
  static winrt::fire_and_forget final_release(std::unique_ptr<KneeboardState>);

  std::shared_ptr<KneeboardView> GetActiveViewForGlobalInput() const;
  std::shared_ptr<KneeboardView> GetActiveInGameView() const;
  std::shared_ptr<KneeboardView> GetAppWindowView() const;
  std::vector<std::shared_ptr<KneeboardView>> GetAllViewsInFixedOrder() const;
  std::vector<ViewRenderInfo> GetViewRenderInfo() const;

  Event<> evFrameTimerPreEvent;
  Event<> evFrameTimerEvent;
  Event<> evFrameTimerPostEvent;
  Event<> evNeedsRepaintEvent;
  Event<> evSettingsChangedEvent;
  Event<> evProfileSettingsChangedEvent;
  Event<> evCurrentProfileChangedEvent;
  Event<> evActiveViewChangedEvent;
  Event<> evInputDevicesChangedEvent;
  Event<UserAction> evUserActionEvent;
  Event<GameEvent> evGameEvent;
  Event<DWORD, std::shared_ptr<GameInstance>> evGameChangedEvent;

  std::vector<std::shared_ptr<UserInputDevice>> GetInputDevices() const;

  GamesList* GetGamesList() const;
  std::optional<RunningGame> GetCurrentGame() const;

  std::shared_ptr<TabletInputAdapter> GetTabletInputAdapter() const;

  ProfileSettings GetProfileSettings() const;
  void SetProfileSettings(const ProfileSettings&);

  void NotifyAppWindowIsForeground(bool isForeground);

  TabsList* GetTabsList() const;

  winrt::Windows::Foundation::IAsyncAction ReleaseExclusiveResources();
  winrt::Windows::Foundation::IAsyncAction ReleaseHwndResources();
  void AcquireExclusiveResources();
#define IT(cpptype, name) \
  cpptype Get##name##Settings() const; \
  void Set##name##Settings(const cpptype&); \
  void Reset##name##Settings();
  OPENKNEEBOARD_SETTINGS_SECTIONS
#undef IT

  void SaveSettings();

  void PostUserAction(UserAction action);

  bool IsRepaintNeeded() const;
  void Repainted();

  /** Implement `Lockable`; use `std::unique_lock`.
   *
   * This:
   * - is a recursive lock
   * - currently has no ordering issues with DXResources::lock()
   * - should not be used *instead of* DXResources::lock()
   *
   * Use DXResources::lock() for DirectX ownership, but use this
   * for application-level locks.
   **/
  void lock();
  bool try_lock();
  void unlock();
  void lock_shared();
  bool try_lock_shared();
  void unlock_shared();

 private:
  KneeboardState(HWND mainWindow, const audited_ptr<DXResources>&);

  std::shared_mutex mMutex;
  bool mHaveUniqueLock = false;
  bool mNeedsRepaint = false;
  winrt::apartment_context mUIThread;
  HWND mHwnd;
  audited_ptr<DXResources> mDXResources;
  ProfileSettings mProfiles {ProfileSettings::Load()};
  Settings mSettings {Settings::Load(mProfiles.mActiveProfile)};

  uint8_t mInputViewIndex = 0;
  std::vector<std::shared_ptr<KneeboardView>> mViews;

  std::shared_ptr<KneeboardView> mAppWindowView;
  bool mAppWindowIsForeground {false};

  PixelSize mLastNonVRPixelSize {};

  std::unique_ptr<GamesList> mGamesList;
  std::unique_ptr<TabsList> mTabsList;
  std::shared_ptr<InterprocessRenderer> mInterprocessRenderer;
  // Initalization and destruction order must match as they both use
  // SetWindowLongPtr
  std::shared_ptr<DirectInputAdapter> mDirectInput;
  std::shared_ptr<TabletInputAdapter> mTabletInput;

  std::shared_ptr<GameEventServer> mGameEventServer;
  RunnerThread mOpenVRThread;
  std::optional<RunningGame> mCurrentGame;

  bool mSaveSettingsEnabled = true;

  void OnGameChangedEvent(
    DWORD processID,
    const std::shared_ptr<GameInstance>& game);
  void OnGameEvent(const GameEvent& ev) noexcept;

  void StartOpenVRThread();
  void StartTabletInput();

  void SetCurrentTab(
    const std::shared_ptr<ITab>& tab,
    const BaseSetTabEvent& metadata);

  enum class Direction {
    Previous,
    Next,
  };
  void SwitchProfile(Direction);

  void InitializeViews();
};

}// namespace OpenKneeboard
