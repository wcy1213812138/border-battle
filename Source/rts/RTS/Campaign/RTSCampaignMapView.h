#pragma once
#include "CoreMinimal.h"

struct FRTSCampaignMapView
{
 double Zoom=1;
 FVector2D Center=FVector2D(.5,.5);
 FVector2D ToMap(const FVector2D& Local) const { return Center+(Local-FVector2D(.5,.5))/Zoom; }
 FVector2D ToLocal(const FVector2D& Map) const { return (Map-Center)*Zoom+FVector2D(.5,.5); }
 bool IsInView(const FVector2D& Map) const
 {
  const FVector2D P=ToLocal(Map);return P.X>=0 && P.X<=1 && P.Y>=0 && P.Y<=1;
 }
 // Keep an off-view objective's direction visible without changing the map or command coordinates.
 FVector2D EdgeIndicator(const FVector2D& Map,double Inset=.03) const
 {
  const FVector2D Delta=ToLocal(Map)-FVector2D(.5,.5);
  const double Extent=FMath::Max(FMath::Abs(Delta.X),FMath::Abs(Delta.Y));
  const double Limit=.5-FMath::Clamp(Inset,0.,.49);
  return Extent>Limit?FVector2D(.5,.5)+Delta*(Limit/Extent):ToLocal(Map);
 }
 void Clamp()
 {
  const double Half=.5/Zoom;
  Center.X=FMath::Clamp(Center.X,Half,1-Half);Center.Y=FMath::Clamp(Center.Y,Half,1-Half);
 }
 void ZoomAt(double Steps,const FVector2D& Anchor)
 {
  const FVector2D Before=ToMap(Anchor);
  Zoom=FMath::Clamp(Zoom*FMath::Pow(1.25,Steps),1.0,4.0);
  Center=Before-(Anchor-FVector2D(.5,.5))/Zoom;Clamp();
 }
 void Pan(const FVector2D& Delta) { Center-=Delta/Zoom;Clamp(); }
 void Reset() { Zoom=1;Center=FVector2D(.5,.5); }
};
