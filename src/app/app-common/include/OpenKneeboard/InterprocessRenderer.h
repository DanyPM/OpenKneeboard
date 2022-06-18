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

#include <OpenKneeboard/D2DErrorRenderer.h>
#include <OpenKneeboard/DXResources.h>
#include <OpenKneeboard/Events.h>
#include <OpenKneeboard/SHM.h>
#include <OpenKneeboard/config.h>
#include <d2d1.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <shims/winrt.h>

#include <memory>
#include <mutex>
#include <optional>

namespace OpenKneeboard {
struct DXResources;
class CursorEvent;
class KneeboardState;
class IKneeboardView;
class Tab;
class TabAction;

class InterprocessRenderer final : private EventReceiver {
 public:
  InterprocessRenderer() = delete;
  InterprocessRenderer(const DXResources&, KneeboardState*);
  ~InterprocessRenderer();

 private:
  struct SharedTextureResources {
    winrt::com_ptr<ID3D11Texture2D> mTexture;
    winrt::com_ptr<IDXGIKeyedMutex> mMutex;
    winrt::handle mHandle;
    UINT mMutexKey = 0;
  };

  winrt::apartment_context mUIThread;
  OpenKneeboard::SHM::Writer mSHM;
  DXResources mDXR;

  KneeboardState* mKneeboard = nullptr;

  bool mNeedsRepaint = true;

  // TODO: move to DXResources
  winrt::com_ptr<ID3D11DeviceContext> mD3DContext;
  winrt::com_ptr<ID3D11Texture2D> mCanvasTexture;
  winrt::com_ptr<ID2D1Bitmap1> mCanvasBitmap;

  std::array<std::array<SharedTextureResources, TextureCount>, MaxLayers>
    mResources;

  winrt::com_ptr<ID2D1Brush> mErrorBGBrush;
  winrt::com_ptr<ID2D1Brush> mHeaderBGBrush;
  winrt::com_ptr<ID2D1Brush> mHeaderTextBrush;
  winrt::com_ptr<ID2D1Brush> mButtonBrush;
  winrt::com_ptr<ID2D1Brush> mDisabledButtonBrush;
  winrt::com_ptr<ID2D1Brush> mHoverButtonBrush;
  winrt::com_ptr<ID2D1Brush> mActiveButtonBrush;
  winrt::com_ptr<ID2D1Brush> mCursorBrush;

  std::unique_ptr<D2DErrorRenderer> mErrorRenderer;

  bool mCursorTouching = false;

  std::vector<std::shared_ptr<TabAction>> mActions;
  using Button = std::pair<D2D1_RECT_F, std::shared_ptr<TabAction>>;
  std::vector<Button> mButtons;
  std::optional<Button> mActiveButton;
  std::mutex mToolbarMutex;

  void RenderNow();
  void Render(const std::shared_ptr<Tab>& tab, uint16_t pageIndex);
  void RenderError(utf8_string_view tabTitle, utf8_string_view message);
  void RenderErrorImpl(utf8_string_view message, const D2D1_RECT_F&);
  void RenderWithChrome(
    const std::string_view tabTitle,
    const D2D1_SIZE_U& preferredContentSize,
    const std::function<void(const D2D1_RECT_F&)>& contentRenderer);
  void RenderToolbar(const std::shared_ptr<IKneeboardView>&);

  void Commit(const SHM::LayerConfig&);

  void OnCursorEvent(const CursorEvent&);
  void OnTabChanged();
};

}// namespace OpenKneeboard
