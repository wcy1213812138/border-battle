#pragma once

// Pure presentation policy, independent of actors and gameplay. Distance is the camera's zoom arm.
namespace RTSUnitNameplateStyle
{
struct FStyle
{
 float Scale;
 float BackgroundAlpha;
 bool bCompact;
 bool bSuppressOverlaps;
};

inline FStyle Resolve(float ZoomDistance, bool bSelected)
{
 const float T = ZoomDistance <= 2000.f ? 0.f : ZoomDistance >= 6500.f ? 1.f : (ZoomDistance - 2000.f) / 4500.f;
 const float Smooth = T * T * (3.f - 2.f * T);
 const float MinimumScale = bSelected ? .72f : .48f;
 return {1.f - (1.f - MinimumScale) * Smooth,
  .85f - (bSelected ? .20f : .40f) * Smooth,
  ZoomDistance >= 3200.f,
  !bSelected && ZoomDistance >= 3200.f};
}
}
