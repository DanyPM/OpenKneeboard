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

#include "ID3D12CommandQueueExecuteCommandListsHook.h"
#include "OculusKneeboard.h"

#include <OpenKneeboard/SHM/D3D12.h>

#include <OpenKneeboard/config.h>

#include <shims/winrt/base.h>

#include <vector>

#include <d3d11.h>
#include <d3d11on12.h>

namespace OpenKneeboard {

class OculusD3D12Kneeboard final : public OculusKneeboard::Renderer {
 public:
  OculusD3D12Kneeboard();
  virtual ~OculusD3D12Kneeboard();

  void UninstallHook();

  virtual SHM::CachedReader* GetSHM() override;

  virtual ovrTextureSwapChain CreateSwapChain(
    ovrSession session,
    const PixelSize&) override;

  virtual bool RenderLayers(
    ovrTextureSwapChain swapchain,
    uint32_t swapchainTextureIndex,
    const SHM::Snapshot& snapshot,
    uint8_t layerCount,
    SHM::LayerSprite* layers) override;

  virtual SHM::ConsumerKind GetConsumerKind() const override {
    return SHM::ConsumerKind::OculusD3D12;
  }

  virtual winrt::com_ptr<ID3D11Device> GetD3D11Device() override;

 private:
  std::unique_ptr<SHM::D3D12::CachedReader> mSHM;
  ID3D12CommandQueueExecuteCommandListsHook mExecuteCommandListsHook;
  OculusKneeboard mOculusKneeboard;

  using DeviceResources = SHM::D3D12::Renderer::DeviceResources;
  std::unique_ptr<DeviceResources> mDeviceResources;
  using SwapchainResources = SHM::D3D12::Renderer::SwapchainResources;
  std::unordered_map<ovrTextureSwapChain, std::unique_ptr<SwapchainResources>>
    mSwapchainResources;

  void OnID3D12CommandQueue_ExecuteCommandLists(
    ID3D12CommandQueue* this_,
    UINT NumCommandLists,
    ID3D12CommandList* const* ppCommandLists,
    const decltype(&ID3D12CommandQueue::ExecuteCommandLists)& next);
};
}// namespace OpenKneeboard
