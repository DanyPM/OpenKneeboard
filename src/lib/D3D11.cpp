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
#include <OpenKneeboard/SHM.h>

#include <OpenKneeboard/config.h>
#include <OpenKneeboard/dprint.h>

#include <shims/winrt/base.h>

#include <directxtk/SpriteBatch.h>

#include <d3d11_3.h>

namespace OpenKneeboard::D3D11 {

struct SavedState::Impl {
  winrt::com_ptr<ID3D11DeviceContext1> mContext;
  winrt::com_ptr<ID3DDeviceContextState> mState;
};

SavedState::SavedState(const winrt::com_ptr<ID3D11DeviceContext>& ctx) {
  mImpl = new Impl {.mContext = ctx.as<ID3D11DeviceContext1>()};

  winrt::com_ptr<ID3D11Device> device;
  ctx->GetDevice(device.put());
  auto featureLevel = device->GetFeatureLevel();

  winrt::com_ptr<ID3DDeviceContextState> newState;
  winrt::check_hresult(device.as<ID3D11Device1>()->CreateDeviceContextState(
    (device->GetCreationFlags() & D3D11_CREATE_DEVICE_SINGLETHREADED)
      ? D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED
      : 0,
    &featureLevel,
    1,
    D3D11_SDK_VERSION,
    __uuidof(ID3D11Device),
    nullptr,
    newState.put()));
  mImpl->mContext->SwapDeviceContextState(newState.get(), mImpl->mState.put());
}

SavedState::~SavedState() {
  BlockingFlush(mImpl->mContext);
  mImpl->mContext->SwapDeviceContextState(mImpl->mState.get(), nullptr);
  delete mImpl;
}

void BlockingFlush(const winrt::com_ptr<ID3D11DeviceContext>& ctx) {
  BlockingFlush(ctx.as<ID3D11DeviceContext3>().get());
}

void BlockingFlush(ID3D11DeviceContext3* ctx) {
  winrt::handle event {CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS)};
  ctx->Flush1(D3D11_CONTEXT_TYPE_ALL, event.get());
  WaitForSingleObject(event.get(), INFINITE);
}

void CopyTextureWithTint(
  ID3D11Device* device,
  ID3D11ShaderResourceView* source,
  ID3D11RenderTargetView* dest,
  DirectX::FXMVECTOR tint) {
  winrt::com_ptr<ID3D11DeviceContext> ctx;
  device->GetImmediateContext(ctx.put());

  SavedState savedState(ctx);

  ctx->ClearRenderTargetView(dest, DirectX::Colors::Transparent);
  D3D11_VIEWPORT viewport {
    0.0f,
    0.0f,
    static_cast<FLOAT>(TextureWidth),
    static_cast<FLOAT>(TextureHeight),
    0.0f,
    1.0f,
  };
  ctx->RSSetViewports(1, &viewport);
  ctx->OMSetDepthStencilState(nullptr, 0);
  ctx->OMSetRenderTargets(1, &dest, nullptr);
  ctx->OMSetBlendState(nullptr, nullptr, ~static_cast<UINT>(0));
  ctx->IASetInputLayout(nullptr);
  ctx->VSSetShader(nullptr, nullptr, 0);

  DirectX::SpriteBatch sprites(ctx.get());
  sprites.Begin();
  sprites.Draw(source, DirectX::XMFLOAT2 {0.0f, 0.0f}, tint);
  sprites.End();
}

void DrawTextureWithTint(
  ID3D11Device* device,
  ID3D11ShaderResourceView* source,
  ID3D11RenderTargetView* dest,
  const RECT& sourceRect,
  const RECT& destRect,
  DirectX::FXMVECTOR tint) {
  winrt::com_ptr<ID3D11DeviceContext> ctx;
  device->GetImmediateContext(ctx.put());

  SavedState state(ctx);

  D3D11_VIEWPORT viewport {
    0.0f,
    0.0f,
    static_cast<FLOAT>(destRect.right),
    static_cast<FLOAT>(destRect.bottom),
    0.0f,
    1.0f,
  };

  ctx->RSSetViewports(1, &viewport);
  ctx->OMSetDepthStencilState(nullptr, 0);
  ctx->OMSetRenderTargets(1, &dest, nullptr);
  ctx->OMSetBlendState(nullptr, nullptr, ~static_cast<UINT>(0));
  ctx->IASetInputLayout(nullptr);
  ctx->VSSetShader(nullptr, nullptr, 0);

  DirectX::SpriteBatch sprites(ctx.get());
  sprites.Begin();
  sprites.Draw(source, destRect, &sourceRect, tint);
  sprites.End();
}

void CopyTextureWithOpacity(
  ID3D11Device* device,
  ID3D11ShaderResourceView* source,
  ID3D11RenderTargetView* dest,
  float opacity) {
  CopyTextureWithTint(
    device,
    source,
    dest,
    DirectX::FXMVECTOR {opacity, opacity, opacity, opacity});
}

void DrawTextureWithOpacity(
  ID3D11Device* device,
  ID3D11ShaderResourceView* source,
  ID3D11RenderTargetView* dest,
  const RECT& sourceRect,
  const RECT& destRect,
  float opacity) {
  DrawTextureWithTint(
    device,
    source,
    dest,
    sourceRect,
    destRect,
    DirectX::FXMVECTOR {opacity, opacity, opacity, opacity});
}

IRenderTargetView::IRenderTargetView() = default;
IRenderTargetView::~IRenderTargetView() = default;
IRenderTargetViewFactory::~IRenderTargetViewFactory() = default;

RenderTargetView::RenderTargetView(
  const winrt::com_ptr<ID3D11RenderTargetView>& impl)
  : mImpl(impl) {
}

RenderTargetView::~RenderTargetView() = default;

ID3D11RenderTargetView* RenderTargetView::Get() const {
  return mImpl.get();
}

RenderTargetViewFactory::RenderTargetViewFactory(
  ID3D11Device* device,
  ID3D11Texture2D* texture,
  DXGI_FORMAT format) {
  D3D11_RENDER_TARGET_VIEW_DESC rtvd {
    .Format = format,
    .ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D,
    .Texture2D = {.MipSlice = 0},
  };

  const auto result
    = device->CreateRenderTargetView(texture, &rtvd, mImpl.put());
  if (result != S_OK) {
    traceprint(
      "Failed to create render target view with format {} - {:#010x}",
      static_cast<UINT>(format),
      static_cast<uint32_t>(result));
  }
}

RenderTargetViewFactory::~RenderTargetViewFactory() = default;

std::unique_ptr<IRenderTargetView> RenderTargetViewFactory::Get() const {
  return std::make_unique<RenderTargetView>(mImpl);
}

}// namespace OpenKneeboard::D3D11
