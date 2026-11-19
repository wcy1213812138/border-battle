#pragma once
#include "CoreMinimal.h"
#include "Campaign/RTSCampaignMapSpace.h"

// Shared mountain profile in centimetres; mirrored by Tools/AddCampaignMountains.py.
// Core amber plateau remains separately authored. Frontier hills preserve faction clearings.
namespace RTSCampaignTerrain
{
 inline double Mountain(double X,double Y,double CX,double CY,double InnerX,double InnerY,double OuterX,double OuterY,double Height)
 {
  const double T=FMath::Clamp(FMath::Max((FMath::Abs(X-CX)-InnerX)/(OuterX-InnerX),(FMath::Abs(Y-CY)-InnerY)/(OuterY-InnerY)),0.0,1.0);
  // Broad traversable slopes with a flat garrison summit.
  return Height*(1.0-T);
 }
 inline double LegacySouthwestHeight(double X,double Y)
 {
  return FMath::Max(Mountain(X,Y,-23500,-19500,3000,2800,6500,6200,1400),
   Mountain(X,Y,-17500,-13500,1100,900,4000,3600,600));
 }
 inline double LegacyHeightAt(double X,double Y)
 {
  double H=LegacySouthwestHeight(X,Y);
  H=FMath::Max(H,Mountain(X,Y,-13000,6500,1200,1400,5800,5900,850));
  H=FMath::Max(H,Mountain(X,Y,18000,6500,1400,1100,6200,5400,750));
  H=FMath::Max(H,Mountain(X,Y,18500,-6500,1000,1300,5300,5700,650));
  H=FMath::Max(H,Mountain(X,Y,5000,17500,1700,900,6500,4800,500));
  H=FMath::Max(H,Mountain(X,Y,-6000,-33500,1400,1800,6200,6500,650));
  H=FMath::Max(H,Mountain(X,Y,16000,33500,1800,1400,7000,6200,800));
  return H;
 }
 inline double SouthwestHeight(double X,double Y)
 { const FVector P=RTSCampaignMapSpace::ToLegacy(FVector(X,Y,0));return LegacySouthwestHeight(P.X,P.Y); }
 inline double HeightAt(double X,double Y)
 { const FVector P=RTSCampaignMapSpace::ToLegacy(FVector(X,Y,0));return LegacyHeightAt(P.X,P.Y); }
 inline FVector Ground(const FVector& P) { return FVector(P.X,P.Y,HeightAt(P.X,P.Y)); }
}
