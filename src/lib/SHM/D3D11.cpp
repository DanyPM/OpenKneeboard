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
#include <OpenKneeboard/SHM/D3D11.h>

namespace OpenKneeboard::SHM::D3D11 {

LayerTextureCache::~LayerTextureCache() = default;

ID3D11ShaderResourceView* LayerTextureCache::GetD3D11ShaderResourceView() {
  if (!mD3D11ShaderResourceView) [[unlikely]] {
    auto texture = this->GetD3D11Texture();
    winrt::com_ptr<ID3D11Device> device;
    texture->GetDevice(device.put());
    winrt::check_hresult(device->CreateShaderResourceView(
      texture, nullptr, mD3D11ShaderResourceView.put()));
  }
  return mD3D11ShaderResourceView.get();
}

std::shared_ptr<SHM::LayerTextureCache> CachedReader::CreateLayerTextureCache(
  [[maybe_unused]] uint8_t layerIndex,
  const winrt::com_ptr<ID3D11Texture2D>& texture) {
  return std::make_shared<SHM::D3D11::LayerTextureCache>(texture);
}

}// namespace OpenKneeboard::SHM::D3D11