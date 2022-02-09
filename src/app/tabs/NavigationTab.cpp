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

#include <OpenKneeboard/NavigationTab.h>
#include <OpenKneeboard/dprint.h>

namespace OpenKneeboard {

NavigationTab::NavigationTab(
  const DXResources& dxr,
  Tab* rootTab,
  const std::vector<Entry>& entries,
  const D2D1_SIZE_U& preferredSize)
  : mDXR(dxr),
    mRootTab(rootTab),
    mPreferredSize(preferredSize),
    mPreviewLayer(dxr) {
  auto dwf = mDXR.mDWriteFactory;
  dwf->CreateTextFormat(
    L"Segoe UI",
    nullptr,
    DWRITE_FONT_WEIGHT_NORMAL,
    DWRITE_FONT_STYLE_NORMAL,
    DWRITE_FONT_STRETCH_NORMAL,
    15.0f,
    L"",
    mTextFormat.put());

  mDXR.mD2DDeviceContext->CreateSolidColorBrush(
    D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), mBackgroundBrush.put());
  mDXR.mD2DDeviceContext->CreateSolidColorBrush(
    D2D1::ColorF(0.0f, 0.8f, 1.0f, 1.0), mHighlightBrush.put());
  mDXR.mD2DDeviceContext->CreateSolidColorBrush(
    D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), mTextBrush.put());

  winrt::com_ptr<IDWriteTextLayout> textLayout;
  dwf->CreateTextLayout(
    L"My", 2, mTextFormat.get(), 1024, 1024, textLayout.put());
  DWRITE_TEXT_METRICS metrics;
  textLayout->GetMetrics(&metrics);
  const auto rowHeight = PaddingRatio * metrics.height;
  const auto padding = rowHeight / 2;

  const auto size = this->GetNativeContentSize(0);
  const D2D1_RECT_F topRect {
    .left = padding,
    .top = padding / 2,
    .right = preferredSize.width - padding,
    .bottom = padding + rowHeight,
  };
  auto rect = topRect;
  mEntries.push_back({});

  auto pageEntries = &mEntries.at(0);

  for (const auto& entry: entries) {
    pageEntries->push_back({
      entry.mName,
      entry.mPageIndex,
      rect,
    });

    rect.top = rect.bottom + padding;
    rect.bottom = rect.top + rowHeight;

    if (rect.bottom > size.height) {
      mEntries.push_back({});
      pageEntries = &mEntries.back();
      rect = topRect;
    }
  }

  mTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  mTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

NavigationTab::~NavigationTab() {
}

std::wstring NavigationTab::GetTitle() const {
  return mRootTab->GetTitle();
}

uint16_t NavigationTab::GetPageCount() const {
  return static_cast<uint16_t>(mEntries.size());
}

D2D1_SIZE_U NavigationTab::GetNativeContentSize(uint16_t) {
  return mPreferredSize;
}

void NavigationTab::PostCursorEvent(const CursorEvent& ev, uint16_t pageIndex) {
  mCursorPoint = {ev.mX, ev.mY};

  // We only care about touch start and end

  // moving with button pressed: no state change
  if (
    ev.mTouchState == CursorTouchState::TOUCHING_SURFACE
    && mButtonState != ButtonState::NOT_PRESSED) {
    dprint("Move, pressed, no change");
    return;
  }

  // moving without button pressed, no state change
  if (
    ev.mTouchState != CursorTouchState::TOUCHING_SURFACE
    && mButtonState == ButtonState::NOT_PRESSED) {
    dprint("Move, not pressed, no change");
    return;
  }

  // touch end, but touch start wasn't on a button
  if (
    ev.mTouchState != CursorTouchState::TOUCHING_SURFACE
    && mButtonState == ButtonState::PRESSING_INACTIVE_AREA) {
    mButtonState = ButtonState::NOT_PRESSED;
    dprint("Release, wasn't in button");
    return;
  }

  bool matched = false;
  uint16_t matchedIndex;
  uint16_t matchedPage;
  const auto& entries = mEntries.at(pageIndex);
  for (uint16_t i = 0; i < entries.size(); ++i) {
    const auto& r = entries.at(i).mRect;
    if (
      ev.mX >= r.left && ev.mX <= r.right && ev.mY >= r.top
      && ev.mY <= r.bottom) {
      matched = true;
      matchedIndex = i;
      matchedPage = entries.at(i).mPageIndex;
      break;
    }
  }

  // touch start
  if (ev.mTouchState == CursorTouchState::TOUCHING_SURFACE) {
    if (matched) {
      mActiveButton = matchedIndex;
      mButtonState = ButtonState::PRESSING_BUTTON;
    } else {
      mButtonState = ButtonState::PRESSING_INACTIVE_AREA;
    }
    return;
  }

  mButtonState = ButtonState::NOT_PRESSED;

  // touch release, but not on the same button
  if ((!matched) || matchedIndex != mActiveButton) {
    return;
  }

  // touch release, on the same button
  evPageChangeRequestedEvent(matchedPage);
}

void NavigationTab::RenderPage(
  ID2D1DeviceContext* ctx,
  uint16_t pageIndex,
  const D2D1_RECT_F& canvasRect) {
  const auto scale
    = (canvasRect.bottom - canvasRect.top) / mPreferredSize.height;

  ctx->SetTransform(D2D1::Matrix3x2F::Identity());
  ctx->FillRectangle(canvasRect, mBackgroundBrush.get());

  const D2D1_POINT_2F origin {canvasRect.left, canvasRect.top};
  ctx->SetTransform(
    D2D1::Matrix3x2F::Translation(origin.x, origin.y)
    * D2D1::Matrix3x2F::Scale({scale, scale}, origin));

  const auto& pageEntries = mEntries.at(pageIndex);

  uint16_t hoverPage = ~0ui16;
  const auto x = mCursorPoint.x;
  const auto y = mCursorPoint.y;

  for (int i = 0; i < pageEntries.size(); ++i) {
    const auto& entry = pageEntries.at(i);
    const auto& rect = entry.mRect;
    bool hover = false;

    switch (mButtonState) {
      case ButtonState::NOT_PRESSED:
        hover = x >= rect.left && x <= rect.right && y >= rect.top
          && y <= rect.bottom;
        break;
      case ButtonState::PRESSING_BUTTON:
        hover = (i == mActiveButton);
        break;
      case ButtonState::PRESSING_INACTIVE_AREA:
        break;
    }

    if (hover) {
      hoverPage = i;
      ctx->FillRectangle(rect, mHighlightBrush.get());
      ctx->FillRectangle(mPreviewMetrics.mRects.at(i), mBackgroundBrush.get());
    }
  }

  mPreviewLayer.Render(
    {0.0f,
     0.0f,
     static_cast<float>(mPreferredSize.width),
     static_cast<float>(mPreferredSize.height)},
    mPreferredSize,
    pageIndex,
    ctx,
    std::bind_front(&NavigationTab::RenderPreviewLayer, this, pageIndex));

  float maxRight = pageEntries.front().mRect.left;
  for (auto i = 0; i < pageEntries.size(); ++i) {
    const auto& entry = pageEntries.at(i);
    const auto& previewRect = mPreviewMetrics.mRects.at(i);
    if (previewRect.right > maxRight) {
      maxRight = previewRect.right;
    }
    if (hoverPage == i) {
      ctx->DrawRectangle(
        previewRect, mHighlightBrush.get(), mPreviewMetrics.mStroke);
    }
  }

  for (const auto& entry: pageEntries) {
    auto rect = entry.mRect;
    rect.left = maxRight + mPreviewMetrics.mBleed;
    ctx->DrawTextW(
      entry.mName.data(),
      static_cast<UINT32>(entry.mName.length()),
      mTextFormat.get(),
      rect,
      mTextBrush.get(),
      D2D1_DRAW_TEXT_OPTIONS_NO_SNAP | D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }
}

void NavigationTab::RenderPreviewLayer(
  uint16_t pageIndex,
  ID2D1DeviceContext* ctx,
  const D2D1_SIZE_U& size) {
  mPreviewMetricsPage = pageIndex;
  auto& m = mPreviewMetrics;
  m = {};

  const auto& pageEntries = mEntries.at(pageIndex);
  m.mRects.clear();
  m.mRects.resize(pageEntries.size());

  const auto& first = pageEntries.front();

  // just a little less than the padding
  m.mBleed = (first.mRect.bottom - first.mRect.top) * PaddingRatio * 0.2f;
  // arbitrary LGTM value
  m.mStroke = m.mBleed * 0.3f;
  m.mHeight = (first.mRect.bottom - first.mRect.top) + (m.mBleed * 2);

  for (auto i = 0; i < pageEntries.size(); ++i) {
    const auto& entry = pageEntries.at(i);

    const auto nativeSize = mRootTab->GetNativeContentSize(entry.mPageIndex);
    const auto scale = m.mHeight / nativeSize.height;
    const auto width = nativeSize.width * scale;

    auto& rect = m.mRects.at(i);
    rect.left = entry.mRect.left + (m.mStroke * 1.25f);
    rect.top = entry.mRect.top - m.mBleed;
    rect.right = rect.left + width;
    rect.bottom = entry.mRect.bottom + m.mBleed;

    mRootTab->RenderPage(ctx, entry.mPageIndex, rect);
  }
}

void NavigationTab::Reload() {
}

}// namespace OpenKneeboard
