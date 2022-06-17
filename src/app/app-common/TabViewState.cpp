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
#include <OpenKneeboard/CursorEvent.h>
#include <OpenKneeboard/Tab.h>
#include <OpenKneeboard/TabViewState.h>
#include <OpenKneeboard/TabWithCursorEvents.h>
#include <OpenKneeboard/TabWithNavigation.h>
#include <OpenKneeboard/config.h>
#include <OpenKneeboard/dprint.h>

namespace OpenKneeboard {

TabViewState::TabViewState(const std::shared_ptr<Tab>& tab)
  : mInstanceID(CreateEventContext()), mRootTab(tab), mRootTabPage(0) {
  AddEventListener(tab->evNeedsRepaintEvent, this->evNeedsRepaintEvent);
  AddEventListener(
    tab->evFullyReplacedEvent, &TabViewState::OnTabFullyReplaced, this);
  AddEventListener(
    tab->evPageAppendedEvent, &TabViewState::OnTabPageAppended, this);
  AddEventListener(
    tab->evPageChangeRequestedEvent, [this](EventContext ctx, uint16_t index) {
      if (ctx != this->GetInstanceID()) {
        return;
      }
      this->SetPageIndex(index);
    });
  AddEventListener(
    tab->evAvailableFeaturesChangedEvent,
    this->evAvailableFeaturesChangedEvent);
}

TabViewState::~TabViewState() {
}

uint64_t TabViewState::GetInstanceID() const {
  return mInstanceID;
}

std::shared_ptr<Tab> TabViewState::GetRootTab() const {
  return mRootTab;
}

std::shared_ptr<Tab> TabViewState::GetTab() const {
  return mActiveSubTab ? mActiveSubTab : mRootTab;
}

uint16_t TabViewState::GetPageIndex() const {
  return mActiveSubTab ? mActiveSubTabPage : mRootTabPage;
}

void TabViewState::PostCursorEvent(const CursorEvent& ev) {
  auto receiver
    = std::dynamic_pointer_cast<TabWithCursorEvents>(this->GetTab());
  if (receiver) {
    receiver->PostCursorEvent(this->GetInstanceID(), ev, this->GetPageIndex());
  }
}

uint16_t TabViewState::GetPageCount() const {
  return this->GetTab()->GetPageCount();
}

void TabViewState::SetPageIndex(uint16_t page) {
  if (page >= this->GetPageCount()) {
    return;
  }

  if (mActiveSubTab) {
    mActiveSubTabPage = page;
  } else {
    mRootTabPage = page;
  }

  this->PostCursorEvent({});

  evNeedsRepaintEvent.Emit();
  evPageChangedEvent.Emit();
}

void TabViewState::NextPage() {
  SetPageIndex(GetPageIndex() + 1);
}

void TabViewState::PreviousPage() {
  const auto page = GetPageIndex();
  if (page > 0) {
    SetPageIndex(page - 1);
  }
}

void TabViewState::OnTabFullyReplaced() {
  mRootTabPage = 0;
  if (!mActiveSubTab) {
    evNeedsRepaintEvent.Emit();
  }
  evPageChangedEvent.Emit();
}

void TabViewState::OnTabPageAppended() {
  const auto count = mRootTab->GetPageCount();
  if (mRootTabPage == count - 2) {
    if (mActiveSubTab) {
      mRootTabPage++;
    } else {
      NextPage();
    }
  }
}

D2D1_SIZE_U TabViewState::GetNativeContentSize() const {
  return GetTab()->GetNativeContentSize(GetPageIndex());
}

TabMode TabViewState::GetTabMode() const {
  return mTabMode;
}

bool TabViewState::SupportsTabMode(TabMode mode) const {
  switch (mode) {
    case TabMode::NORMAL:
      return true;
    case TabMode::NAVIGATION: {
      auto nav = std::dynamic_pointer_cast<TabWithNavigation>(mRootTab);
      return nav && nav->IsNavigationAvailable();
    }
  }
  // above switch should be exhaustive
  OPENKNEEBOARD_BREAK;
  return false;
}

bool TabViewState::SetTabMode(TabMode mode) {
  if (mTabMode == mode || !SupportsTabMode(mode)) {
    return false;
  }

  auto receiver
    = std::dynamic_pointer_cast<TabWithCursorEvents>(this->GetTab());
  if (receiver) {
    receiver->PostCursorEvent(this->GetInstanceID(), {}, this->GetPageIndex());
  }

  mTabMode = mode;
  mActiveSubTab = nullptr;
  mActiveSubTabPage = 0;

  switch (mode) {
    case TabMode::NORMAL:
      break;
    case TabMode::NAVIGATION:
      mActiveSubTab = std::dynamic_pointer_cast<TabWithNavigation>(mRootTab)
                        ->CreateNavigationTab(mRootTabPage);
      AddEventListener(
        mActiveSubTab->evPageChangeRequestedEvent,
        [this](EventContext ctx, uint16_t newPage) {
          if (ctx != this->GetInstanceID()) {
            return;
          }
          mRootTabPage = newPage;
          SetTabMode(TabMode::NORMAL);
        });
      AddEventListener(
        mActiveSubTab->evNeedsRepaintEvent, this->evNeedsRepaintEvent);
      break;
  }

  if (mode != TabMode::NORMAL && !mActiveSubTab) {
    // Switch should have been exhaustive
    OPENKNEEBOARD_BREAK;
  }

  evPageChangedEvent.EmitFromMainThread();
  evNeedsRepaintEvent.EmitFromMainThread();
  evTabModeChangedEvent.EmitFromMainThread();

  return true;
}

}// namespace OpenKneeboard