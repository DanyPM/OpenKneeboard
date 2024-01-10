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

#include <OpenKneeboard/D3D11.h>
#include <OpenKneeboard/RenderTarget.h>
#include <OpenKneeboard/SHM.h>

#include <OpenKneeboard/dprint.h>

#include <d2d1_3.h>

namespace OpenKneeboard {

std::shared_ptr<RenderTarget> RenderTarget::Create(
  const DXResources& dxr,
  const winrt::com_ptr<ID3D11Texture2D>& texture) {
  return std::shared_ptr<RenderTarget>(new RenderTarget(dxr, texture));
}

RenderTarget::~RenderTarget() = default;

RenderTarget::RenderTarget(
  const DXResources& dxr,
  const winrt::com_ptr<ID3D11Texture2D>& texture)
  : mDXR(dxr), mD3DTexture(texture) {
  winrt::check_hresult(dxr.mD3DDevice->CreateRenderTargetView(
    texture.get(), nullptr, mD3DRenderTargetView.put()));

  winrt::check_hresult(dxr.mD2DDeviceContext->CreateBitmapFromDxgiSurface(
    texture.as<IDXGISurface>().get(), nullptr, mD2DBitmap.put()));
}

RenderTargetID RenderTarget::GetID() const {
  return mID;
}

RenderTarget::D2D RenderTarget::d2d() {
  return {this->shared_from_this()};
}

RenderTarget::D3D RenderTarget::d3d() {
  return {this->shared_from_this()};
}

RenderTarget::D2D::D2D(const std::shared_ptr<RenderTarget>& parent)
  : mParent(parent) {
  if (!parent) {
    OPENKNEEBOARD_BREAK;
    return;
  }
  mUnsafeParent = mParent.get();
  this->Acquire();
}

void RenderTarget::D2D::Acquire() {
  auto& mode = mParent->mMode;
  if (mode != Mode::Unattached) {
    mParent = nullptr;
    dprintf(
      "Attempted to activate D2D for a render target in mode {}",
      static_cast<int>(mode));
    OPENKNEEBOARD_BREAK;
    return;
  }

  mode = Mode::D2D;

  (*this)->SetTarget(mUnsafeParent->mD2DBitmap.get());
  mUnsafeParent->mDXR.PushD2DDraw();
  (*this)->SetTransform(D2D1::Matrix3x2F::Identity());
}

void RenderTarget::D2D::Release() {
  if (!mParent) {
    return;
  }
  if (mReleased) {
    dprintf("{}: double-release", __FUNCTION__);
    OPENKNEEBOARD_BREAK;
    return;
  }
  mReleased = true;
  mUnsafeParent->mDXR.PopD2DDraw();
  mUnsafeParent->mDXR.mD2DDeviceContext->SetTarget(nullptr);

  auto& mode = mParent->mMode;
  if (mode != Mode::D2D) {
    dprintf(
      "{}: attempting to release D2D, but mode is {}",
      __FUNCTION__,
      static_cast<int>(mode));
    OPENKNEEBOARD_BREAK;
    return;
  }

  mode = Mode::Unattached;
}

void RenderTarget::D2D::Reacquire() {
  if (!mReleased) {
    dprint("Attempting to re-acquire without release");
    OPENKNEEBOARD_BREAK;
    return;
  }
  this->Acquire();
  mReleased = false;
}

RenderTarget::D2D::~D2D() {
  if (mReleased || !mParent) {
    return;
  }
  this->Release();
}

ID2D1DeviceContext* RenderTarget::D2D::operator->() const {
  return mUnsafeParent->mDXR.mD2DDeviceContext.get();
}

RenderTarget::D2D::operator ID2D1DeviceContext*() const {
  return mUnsafeParent->mDXR.mD2DDeviceContext.get();
}

RenderTarget::D3D::D3D(const std::shared_ptr<RenderTarget>& parent)
  : mParent(parent) {
  if (!parent) {
    OPENKNEEBOARD_BREAK;
    return;
  }
  mUnsafeParent = parent.get();

  auto& mode = mParent->mMode;
  if (mode != Mode::Unattached) {
    mParent = nullptr;
    dprintf(
      "Attempted to activate D3D for a render target in mode {}",
      static_cast<int>(mode));
    OPENKNEEBOARD_BREAK;
    return;
  }
  mode = Mode::D3D;
}

RenderTarget::D3D::~D3D() {
  if (!mParent) {
    return;
  }

  auto& mode = mParent->mMode;
  if (mode != Mode::D3D) {
    dprintf(
      "Attempting to leave D3D for render target in mode {}",
      static_cast<int>(mode));
    OPENKNEEBOARD_BREAK;
    return;
  }
  mode = Mode::Unattached;
}

ID3D11Texture2D* RenderTarget::D3D::texture() const {
  return mUnsafeParent->mD3DTexture.get();
}

ID3D11RenderTargetView* RenderTarget::D3D::rtv() const {
  return mUnsafeParent->mD3DRenderTargetView.get();
}

}// namespace OpenKneeboard