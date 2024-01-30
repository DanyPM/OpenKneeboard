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

#include <OpenKneeboard/ProcessShutdownBlock.h>

#include <OpenKneeboard/dprint.h>

#include <shims/source_location>

#include <atomic>
#include <unordered_map>

namespace OpenKneeboard {

namespace {

struct ShutdownData {
 public:
  static ShutdownData& Get() {
    static ShutdownData sInstance;
    return sInstance;
  }

  uint64_t Increment(const std::source_location& loc) noexcept {
    mBlockCount.fetch_add(1);
    if (mShuttingDown.test()) {
      OPENKNEEBOARD_LOG_SOURCE_LOCATION_AND_FATAL(
        loc, "Incrementing after shutdown");
    }
    const auto id = mNextID++;
    std::unique_lock lock(mMutex);
    mActiveBlocks.emplace(id, loc);
    return id;
  }

  void Decrement(uint64_t id) noexcept {
    {
      std::unique_lock lock(mMutex);
      if (mShuttingDown.test()) {
        const auto loc = mActiveBlocks.at(id);
        dprintf("Shutdown cleanup @ {}", loc);
      }
      mActiveBlocks.erase(id);
    }

    const auto remaining = mBlockCount.fetch_sub(1) - 1;

    if (mShuttingDown.test()) {
      dprintf("{} shutdown items remaining.", remaining);
    }

    if (remaining == 0) {
      if (!mShuttingDown.test()) [[unlikely]] {
        OPENKNEEBOARD_LOG_AND_FATAL("Block count = 0, but not shutting down");
      }
      SetEvent(mShutdownEvent);
    }
  }

  void SetEventOnCompletion(HANDLE completionEvent) {
    mShutdownEvent = completionEvent;

    {
      std::unique_lock lock(mMutex);
      if (mShuttingDown.test_and_set()) {
        OPENKNEEBOARD_LOG_AND_FATAL("Running shutdown blockers twice");
      }

      dprintf("Waiting for {} shutdown blockers:", mActiveBlocks.size());
      for (const auto& [id, location]: mActiveBlocks) {
        dprintf("- {}", location);
      }
    }

    this->Decrement(mMyID);
  }

 private:
  ShutdownData() {
    mMyID = this->Increment(std::source_location::current());
  }

  std::atomic_uint64_t mBlockCount {0};
  std::atomic_flag mShuttingDown;
  std::atomic_uint64_t mNextID {0};

  uint64_t mMyID;

  HANDLE mShutdownEvent {};

  std::mutex mMutex;
  std::unordered_map<uint64_t, std::source_location> mActiveBlocks;
};
}// namespace

ProcessShutdownBlock::ProcessShutdownBlock(const std::source_location& loc)
  : mID(ShutdownData::Get().Increment(loc)) {
}

ProcessShutdownBlock::~ProcessShutdownBlock() {
  ShutdownData::Get().Decrement(mID);
}

void ProcessShutdownBlock::SetEventOnCompletion(HANDLE completionEvent) {
  ShutdownData::Get().SetEventOnCompletion(completionEvent);
}

}// namespace OpenKneeboard