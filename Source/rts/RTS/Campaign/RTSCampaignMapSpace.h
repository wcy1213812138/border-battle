#pragma once

#include "CoreMinimal.h"

// Centimetres. Camera yaw 0: screen up is +X, screen right is +Y.
// Layout 11 reduces TOTAL area to 2/3. Only XY is contracted; unit bodies and heights are unchanged.
namespace RTSCampaignMapSpace
{
 inline constexpr int32 LayoutVersion=11;
 inline constexpr double LayoutScale=0.81649658092772603273; // sqrt(2/3), not 2/3 on both axes.
 inline constexpr double LegacyMinX=-28500.0, LegacyMaxX=35500.0;
 inline constexpr double LegacyMinY=-44800.0, LegacyMaxY=44800.0;
 inline constexpr double CenterX=3500.0, CenterY=0.0;
 inline constexpr double MinX=CenterX+(LegacyMinX-CenterX)*LayoutScale;
 inline constexpr double MaxX=CenterX+(LegacyMaxX-CenterX)*LayoutScale;
 inline constexpr double MinY=LegacyMinY*LayoutScale, MaxY=LegacyMaxY*LayoutScale;
 inline constexpr double SizeX=MaxX-MinX, SizeY=MaxY-MinY;
 inline constexpr double MinimapWidthToHeight=SizeY/SizeX; // Screen horizontal:vertical = 7:5.

 inline FVector FromLegacy(const FVector& P)
 { return FVector(CenterX+(P.X-CenterX)*LayoutScale,CenterY+(P.Y-CenterY)*LayoutScale,P.Z); }
 inline FVector ToLegacy(const FVector& P)
 { return FVector(CenterX+(P.X-CenterX)/LayoutScale,CenterY+(P.Y-CenterY)/LayoutScale,P.Z); }
 inline FVector ScaleOffset(const FVector& P)
 { return FVector(P.X*LayoutScale,P.Y*LayoutScale,P.Z); }
 inline bool ContainsLegacyXY(const FVector& P)
 { return !P.ContainsNaN() && P.X>=LegacyMinX && P.X<=LegacyMaxX && P.Y>=LegacyMinY && P.Y<=LegacyMaxY; }

 inline bool ContainsXY(const FVector& P)
 {
  return !P.ContainsNaN() && P.X>=MinX && P.X<=MaxX && P.Y>=MinY && P.Y<=MaxY;
 }

 inline FVector2D WorldToMap(const FVector& P)
 {
  return FVector2D((P.Y-MinY)/SizeY,(MaxX-P.X)/SizeX);
 }

 inline FVector MapToWorld(const FVector2D& UV,double Z=0.0)
 {
  return FVector(MaxX-UV.Y*SizeX,MinY+UV.X*SizeY,Z);
 }
}
