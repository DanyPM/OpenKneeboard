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
#include <OpenKneeboard/RayIntersectsRect.h>
#include <OpenKneeboard/VRKneeboard.h>

using namespace DirectX::SimpleMath;

namespace OpenKneeboard {

VRKneeboard::Pose VRKneeboard::GetKneeboardPose(
  const VRRenderConfig& vr,
  const SHM::LayerConfig& layer,
  const Pose& hmdPose) {
  const auto& vrl = layer.mVR;
  this->Recenter(vr, hmdPose);
  // clang-format off
    auto matrix =
      Matrix::CreateRotationX(vrl.mRX)
      * Matrix::CreateRotationY(vrl.mRY)
      * Matrix::CreateRotationZ(vrl.mRZ)
      * Matrix::CreateTranslation({
        vrl.mX,
        this->GetYOrigin() == YOrigin::EYE_LEVEL ? vrl.mEyeY : vrl.mFloorY, vrl.mZ})
      * mRecenter;
  // clang-format on
  return {
    .mPosition = matrix.Translation(),
    .mOrientation = Quaternion::CreateFromRotationMatrix(matrix),
  };
}

Vector2 VRKneeboard::GetKneeboardSize(
  const SHM::Config& config,
  const SHM::LayerConfig& layer,
  bool isLookingAtKneeboard) {
  const auto sizes = this->GetSizes(config.mVR, layer);
  if (!this->IsGazeEnabled(config.mVR)) {
    return sizes.mNormalSize;
  }
  return isLookingAtKneeboard ? sizes.mZoomedSize : sizes.mNormalSize;
}

bool VRKneeboard::IsGazeEnabled(const VRRenderConfig& vr) {
  if (std::abs(vr.mGazeOpacity - vr.mNormalOpacity) >= 0.01f) {
    return true;
  }

  if (vr.mFlags & VRRenderConfig::Flags::FORCE_ZOOM) {
    return false;
  }
  if (!(vr.mFlags & VRRenderConfig::Flags::GAZE_ZOOM)) {
    return false;
  }

  if (
    vr.mZoomScale < 1.1 || vr.mGazeTargetHorizontalScale < 0.1
    || vr.mGazeTargetVerticalScale < 0.1) {
    return false;
  }

  return true;
}

VRKneeboard::Sizes VRKneeboard::GetSizes(
  const VRRenderConfig& vrc,
  const SHM::LayerConfig& layer) const {
  const auto& vr = layer.mVR;
  const auto aspectRatio = float(layer.mImageWidth) / layer.mImageHeight;
  const auto virtualHeight = vr.mHeight;
  const auto virtualWidth = aspectRatio * vr.mHeight;

  return {
    .mNormalSize = {virtualWidth, virtualHeight},
    .mZoomedSize
    = {virtualWidth * vrc.mZoomScale, virtualHeight * vrc.mZoomScale},
  };
}

void VRKneeboard::Recenter(const VRRenderConfig& vr, const Pose& hmdPose) {
  if (vr.mRecenterCount == mRecenterCount) {
    return;
  }

  auto pos = hmdPose.mPosition;
  pos.y = 0;// don't adjust floor level

  // We're only going to respect ry (yaw) as we want the new
  // center to remain gravity-aligned

  auto quat = hmdPose.mOrientation;

  // clang-format off
    mRecenter =
      Matrix::CreateRotationY(quat.ToEuler().y) 
      * Matrix::CreateTranslation({pos.x, pos.y, pos.z});
  // clang-format on

  mRecenterCount = vr.mRecenterCount;
}

VRKneeboard::RenderParameters VRKneeboard::GetRenderParameters(
  const SHM::Snapshot& snapshot,
  const SHM::LayerConfig& layer,
  const Pose& hmdPose) {
  auto config = snapshot.GetConfig();
  const auto kneeboardPose = this->GetKneeboardPose(config.mVR, layer, hmdPose);
  const auto isLookingAtKneeboard
    = this->IsLookingAtKneeboard(config, layer, hmdPose, kneeboardPose);
  return {
    .mKneeboardPose = kneeboardPose,
    .mKneeboardSize
    = this->GetKneeboardSize(config, layer, isLookingAtKneeboard),
    .mKneeboardOpacity = isLookingAtKneeboard ? config.mVR.mGazeOpacity
                                              : config.mVR.mNormalOpacity,
    .mCacheKey = snapshot.GetSequenceNumber()
      ^ ((isLookingAtKneeboard ? 1ui64 : 0ui64) << 63),
  };
}

bool VRKneeboard::IsLookingAtKneeboard(
  const SHM::Config& config,
  const SHM::LayerConfig& layer,
  const Pose& hmdPose,
  const Pose& kneeboardPose) {
  static bool sIsLookingAtKneeboard = false;
  if (!this->IsGazeEnabled(config.mVR)) {
    return false;
  }

  const auto sizes = this->GetSizes(config.mVR, layer);
  const auto currentSize
    = sIsLookingAtKneeboard ? sizes.mZoomedSize : sizes.mNormalSize;

  sIsLookingAtKneeboard = RayIntersectsRect(
    hmdPose.mPosition,
    hmdPose.mOrientation,
    kneeboardPose.mPosition,
    kneeboardPose.mOrientation,
    currentSize);

  return sIsLookingAtKneeboard;
}

}// namespace OpenKneeboard
