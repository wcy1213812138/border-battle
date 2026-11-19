#include "Campaign/RTSCampaignMinimap.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignPalette.h"
#include "Campaign/RTSCampaignMapSpace.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Engine/Texture2D.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "NavigationSystem.h"
#include "GameFramework/Pawn.h"
#include "Units/RTSUnit.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"

void URTSCampaignMinimap::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 // Markers depend on world actors, not UMG property invalidation. Repaint even inside an invalidation root.
 ForceVolatile(true);
}
void URTSCampaignMinimap::ResetMapView()
{
 bDraggingCamera=bPanningMap=false;View.Reset();InvalidateLayoutAndVolatility();
}
void URTSCampaignMinimap::UpdateTerrainTexture()
{
 AActor* Scenery=nullptr;
 if(GetWorld())for(TActorIterator<AActor> It(GetWorld());It;++It)
  if(It->FindComponentByClass<UInstancedStaticMeshComponent>()){Scenery=*It;break;}
 if(TerrainTexture && CachedScenery.Get()==Scenery)return;
 CachedScenery=Scenery;
 constexpr int32 Width=448,Height=320;
 TArray<FColor> Pixels;Pixels.SetNumUninitialized(Width*Height);
 auto Elevation=[](double X,double Y){double H=RTSCampaignTerrain::HeightAt(X,Y);const FVector P=RTSCampaignMapSpace::ToLegacy(FVector(X,Y,0));if(P.X>=1800 && P.X<=8500 && P.Y>=1000 && P.Y<=4200)H=FMath::Max(H,900.0);return H;};
 for(int32 Y=0;Y<Height;++Y)for(int32 X=0;X<Width;++X)
 {
  const FVector P=RTSCampaignMapSpace::MapToWorld(FVector2D((X+.5)/Width,(Y+.5)/Height));
  const double H=Elevation(P.X,P.Y);
  const double Slope=(Elevation(P.X+200,P.Y)-Elevation(P.X-200,P.Y)+Elevation(P.X,P.Y+200)-Elevation(P.X,P.Y-200))/800.0;
  const float Shade=FMath::Clamp(1.f+float(Slope)*.65f,.65f,1.3f);
  const float T=FMath::Clamp(float(H/1400.0),0.f,1.f);
  FLinearColor C=FMath::Lerp(FLinearColor(.16f,.29f,.12f),FLinearColor(.70f,.67f,.48f),T)*Shade;C.A=1;
  Pixels[Y*Width+X]=C.ToFColorSRGB();
 }
 if(Scenery)
 {
  TArray<UInstancedStaticMeshComponent*> Parts;Scenery->GetComponents(Parts);
  for(auto* Part:Parts)if(Part->GetName().Contains(TEXT("PineTrunk")))
   for(int32 I=0;I<Part->GetInstanceCount();++I)
   {
    FTransform T;Part->GetInstanceTransform(I,T,true);const FVector2D UV=RTSCampaignMapSpace::WorldToMap(T.GetLocation());
    const int32 X=FMath::FloorToInt(UV.X*Width),Y=FMath::FloorToInt(UV.Y*Height);
    if(X>=0 && X<Width && Y>=0 && Y<Height)Pixels[Y*Width+X]=FColor(35,72,40);
   }
 }
 TerrainTexture=UTexture2D::CreateTransient(Width,Height,PF_B8G8R8A8,TEXT("CampaignTerrainMinimap"));
 if(!TerrainTexture)return;
 TerrainTexture->Filter=TF_Bilinear;TerrainTexture->SRGB=true;
 auto& Mip=TerrainTexture->GetPlatformData()->Mips[0];void* Data=Mip.BulkData.Lock(LOCK_READ_WRITE);
 FMemory::Memcpy(Data,Pixels.GetData(),Pixels.Num()*sizeof(FColor));Mip.BulkData.Unlock();TerrainTexture->UpdateResource();
}
int32 URTSCampaignMinimap::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const
{
 Layer=Super::NativePaint(Args,Geometry,CullingRect,Elements,Layer,Style,ParentEnabled);
 const FVector2D Size=Geometry.GetLocalSize();
 auto MapPoint=[this](const FVector& P,const FVector2D& Extent){return View.ToLocal(RTSCampaignMapSpace::WorldToMap(P))*Extent;};
 Elements.PushClip(FSlateClippingZone(Geometry));
 const FSlateBrush* Brush=FCoreStyle::Get().GetBrush("WhiteBrush");
 auto Rect=[&](FVector2D P,FVector2D Extent,FLinearColor Color,int32 MarkerLayer=2)
 { FSlateDrawElement::MakeBox(Elements,Layer+MarkerLayer,Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(P)),Brush,ESlateDrawEffect::None,Color); };
 auto ResourceDiamond=[&](FVector2D P,FLinearColor Color)
 {
  const TArray<FVector2D> Points={P+FVector2D(0,-4),P+FVector2D(4,0),P+FVector2D(0,4),P+FVector2D(-4,0),P+FVector2D(0,-4)};
  FSlateDrawElement::MakeLines(Elements,Layer+4,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.015f,.02f,.025f),true,4);
  FSlateDrawElement::MakeLines(Elements,Layer+5,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,2);
 };
 FSlateDrawElement::MakeBox(Elements,Layer,Geometry.ToPaintGeometry(),Brush,ESlateDrawEffect::None,FLinearColor(.045f,.10f,.085f));
 const_cast<URTSCampaignMinimap*>(this)->UpdateTerrainTexture();
 if(TerrainTexture)
 {
  FSlateBrush TerrainBrush;TerrainBrush.SetResourceObject(TerrainTexture);TerrainBrush.ImageSize=FVector2D(448,320);
  const FVector2D Half(.5/View.Zoom,.5/View.Zoom);TerrainBrush.SetUVRegion(FBox2f(FVector2f(View.Center-Half),FVector2f(View.Center+Half)));
  TerrainBrush.DrawAs=ESlateBrushDrawType::Image;
  FSlateDrawElement::MakeBox(Elements,Layer+1,Geometry.ToPaintGeometry(),&TerrainBrush,ESlateDrawEffect::None,FLinearColor::White);
 }
 // Authored plateau perimeter and sole west ramp; project every vertex.
 auto Contour=[&](const TArray<FVector>& Vertices,FLinearColor Color)
 {
  TArray<FVector2D> Points;
  for(const FVector& P:Vertices) Points.Add(MapPoint(RTSCampaignMapSpace::FromLegacy(P),Size));
  if(!Points.IsEmpty()) { const FVector2D First=Points[0];Points.Add(First); }
  FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,2);
 };
 Contour({FVector(1800,900,900),FVector(8600,900,900),FVector(9600,1700,900),FVector(9600,5700,900),FVector(8500,6600,900),FVector(2100,4200,900),FVector(1400,3500,900),FVector(1400,1700,900)},FLinearColor(.55f,.49f,.32f));
 Contour({FVector(-1400,1950,0),FVector(1600,1950,900),FVector(1600,3050,900),FVector(-1400,3050,0)},FLinearColor(.60f,.42f,.24f));
 Contour({FVector(-28500,-24000,0),FVector(-17000,-24000,0),FVector(-17000,-13300,0),FVector(-28500,-13300,0)},FLinearColor(.38f,.43f,.26f));
 Contour({FVector(-26500,-22300,1400),FVector(-20500,-22300,1400),FVector(-20500,-16700,1400),FVector(-26500,-16700,1400)},FLinearColor(.72f,.63f,.42f));
 if(GetWorld())
 {
  const auto* Campaign=ARTSCampaign::Get(this);
  int32 UnitCount=0,ResourceCount=0,ResourceInView=0,HallCount=0,HallEdgeCount=0;
  // Troops are below persistent objectives: a garrison must not paint over its own HQ.
  for(TActorIterator<ARTSUnit> It(GetWorld());It;++It) if(It->IsAlive() && !It->ActorHasTag(TEXT("RTS_CampaignRestoreStaged")))
  {
   ++UnitCount;
   const FLinearColor Color=It->GetTeam()==0?FLinearColor(.3f,.8f,1):RTSCampaignPalette::Color(Campaign?Campaign->GetUnitFaction(*It):0);
   Rect(MapPoint(It->GetActorLocation(),Size)-FVector2D(1.5f),FVector2D(3),Color);
  }
  const FLinearColor ResourceColors[]={FLinearColor(.6f,1.f,.45f),FLinearColor(1.f,.92f,.35f),FLinearColor(.75f,.88f,1.f),FLinearColor(.35f,1.f,1.f),FLinearColor(1.f,.62f,.15f),FLinearColor(.8f,.55f,1.f),FLinearColor(1.f,.84f,.1f),FLinearColor(.5f,.65f,1.f)};
  for(TActorIterator<ARTSResourceNode> It(GetWorld());It;++It)
  {
   if(It->ActorHasTag(TEXT("RTS_CampaignRestoreStaged")))continue;
   ++ResourceCount;
   const FVector2D UV=RTSCampaignMapSpace::WorldToMap(It->GetActorLocation());
   if(!View.IsInView(UV))continue;
   ++ResourceInView;
   ResourceDiamond(View.ToLocal(UV)*Size,ResourceColors[FMath::Clamp(int32(It->GetResourceType()),0,7)]);
  }
  for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It) if(It->IsAlive() && !It->ActorHasTag(TEXT("RTS_CampaignRestoreStaged")))
  {
   const bool bHall=It->GetBuildingId()==TEXT("TownHall");
   const FVector2D UV=RTSCampaignMapSpace::WorldToMap(It->GetActorLocation());
   const int32 Faction=Campaign?Campaign->GetActorFaction(*It):-1;
   const FLinearColor Color=It->GetTeam()==0?FLinearColor(.25f,.85f,1):Faction>=0?RTSCampaignPalette::Color(Faction):FLinearColor(1,.45f,.25f);
   if(!bHall)
   {
    if(View.IsInView(UV)){const FVector2D P=View.ToLocal(UV)*Size;Rect(P-FVector2D(4),FVector2D(8),FLinearColor::Black,3);Rect(P-FVector2D(2.5),FVector2D(5),Color,3);}
    continue;
   }
   ++HallCount;const bool bInView=View.IsInView(UV);HallEdgeCount+=!bInView;
   const FVector2D P=(bInView?View.ToLocal(UV):View.EdgeIndicator(UV,.045))*Size;
   // Filled HQ square on map; hollow edge square means the real HQ is outside this zoomed view.
   Rect(P-FVector2D(6),FVector2D(12),FLinearColor(.01f,.02f,.025f),6);
   Rect(P-FVector2D(5),FVector2D(10),FLinearColor(.95f,.97f,1.f),6);
   Rect(P-FVector2D(4),FVector2D(8),Color,6);
   if(!bInView)Rect(P-FVector2D(2),FVector2D(4),FLinearColor(.02f,.03f,.035f),6);
  }
  LastPaintState.Day=Campaign?Campaign->GetDay():-1;LastPaintState.Halls=HallCount;LastPaintState.HallEdges=HallEdgeCount;
  LastPaintState.Resources=ResourceCount;LastPaintState.ResourcesInView=ResourceInView;LastPaintState.Units=UnitCount;
  LastPaintState.Size=Size;LastPaintState.PaintedAt=FPlatformTime::Seconds();
  if(Campaign && LastPaintDay!=Campaign->GetDay())
  {
   LastPaintDay=Campaign->GetDay();
   UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_MINIMAP_STATE day=%d zoom=%.2f center=%.3f,%.3f halls=%d edge=%d resources=%d inView=%d units=%d size=%.0f,%.0f"),LastPaintDay+1,View.Zoom,View.Center.X,View.Center.Y,HallCount,HallEdgeCount,ResourceCount,ResourceInView,UnitCount,Size.X,Size.Y);
  }
 }
 if(auto* PC=GetOwningPlayer()) if(APawn* CameraPawn=PC->GetPawn())
 {
  const FVector2D P=MapPoint(CameraPawn->GetActorLocation(),Size);
  TArray<FVector2D> Outline={P+FVector2D(-12,-9),P+FVector2D(12,-9),P+FVector2D(12,9),P+FVector2D(-12,9),P+FVector2D(-12,-9)};
  FSlateDrawElement::MakeLines(Elements,Layer+7,Geometry.ToPaintGeometry(),Outline,ESlateDrawEffect::None,FLinearColor::White,true,1);
 }
 Elements.PopClip();return Layer+7;
}
void URTSCampaignMinimap::FocusCameraAt(const FGeometry& Geometry,const FVector2D& ScreenPosition)
{
 const FVector2D Size=Geometry.GetLocalSize();
 if(Size.X<=0 || Size.Y<=0)return;
 const FVector2D UV=View.ToMap(Geometry.AbsoluteToLocal(ScreenPosition)/Size);
 if(auto* PC=GetOwningPlayer()) if(APawn* CameraPawn=PC->GetPawn())
 {
  auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
  FNavLocation Destination,CurrentGround;
  FVector Target=RTSCampaignMapSpace::MapToWorld(FVector2D(FMath::Clamp(UV.X,0.,1.),FMath::Clamp(UV.Y,0.,1.)),450);
  Target.Z=FMath::Max(450.0,RTSCampaignTerrain::HeightAt(Target.X,Target.Y));
  if(Nav && Nav->ProjectPointToNavigation(Target,Destination,FVector(200,200,1200)))
  {
   const FVector Current=CameraPawn->GetActorLocation();
   const double Height=Nav->ProjectPointToNavigation(Current,CurrentGround,FVector(200,200,2000))?FMath::Max(0.0,Current.Z-CurrentGround.Location.Z):0.0;
   CameraPawn->SetActorLocation(Destination.Location+FVector(0,0,Height));
  }
 }
}
FReply URTSCampaignMinimap::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
 if(Event.GetEffectingButton()==EKeys::RightMouseButton)
 {
  if(auto* PC=Cast<ARTSHumanController>(GetOwningPlayer()))
  {
   if(PC->IsPlacingBuilding()){PC->CancelBuildingPlacement();return FReply::Handled();}
   if(PC->GetSelection() && PC->GetSelection()->GetSelectedCount()>0)
   {
    bPanningMap=bDraggingCamera=false;
    const FVector2D Size=Geometry.GetLocalSize();
    if(Size.X>0 && Size.Y>0)
    {
     const FVector2D UV=View.ToMap(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())/Size);
     FVector Target=RTSCampaignMapSpace::MapToWorld(FVector2D(FMath::Clamp(UV.X,0.,1.),FMath::Clamp(UV.Y,0.,1.)));
     Target.Z=RTSCampaignTerrain::HeightAt(Target.X,Target.Y);
     PC->RequestMinimapMoveAt(Target,Event.IsShiftDown());
    }
    return FReply::Handled();
   }
  }
  bPanningMap=true;bDraggingCamera=false;LastDragPosition=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
  return FReply::Handled().CaptureMouse(TakeWidget());
 }
 if(Event.GetEffectingButton()!=EKeys::LeftMouseButton)return FReply::Unhandled();
 bDraggingCamera=true;bPanningMap=false;FocusCameraAt(Geometry,Event.GetScreenSpacePosition());
 return FReply::Handled().CaptureMouse(TakeWidget());
}
FReply URTSCampaignMinimap::NativeOnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event)
{
 if(Event.GetEffectingButton()==EKeys::RightMouseButton){bPanningMap=false;return FReply::Handled().ReleaseMouseCapture();}
 if(Event.GetEffectingButton()==EKeys::LeftMouseButton){bDraggingCamera=false;return FReply::Handled().ReleaseMouseCapture();}
 return Super::NativeOnMouseButtonUp(Geometry,Event);
}
FReply URTSCampaignMinimap::NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event)
{
 const FVector2D Size=Geometry.GetLocalSize();if(Size.X<=0 || Size.Y<=0)return FReply::Handled();
 View.ZoomAt(Event.GetWheelDelta(),Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition())/Size);
 InvalidateLayoutAndVolatility();return FReply::Handled();
}
FReply URTSCampaignMinimap::NativeOnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event)
{
 if(bDraggingCamera)
 {
  if(!Event.IsMouseButtonDown(EKeys::LeftMouseButton)){bDraggingCamera=false;return FReply::Handled().ReleaseMouseCapture();}
  FocusCameraAt(Geometry,Event.GetScreenSpacePosition());return FReply::Handled();
 }
 if(!bPanningMap)return Super::NativeOnMouseMove(Geometry,Event);
 if(!Event.IsMouseButtonDown(EKeys::RightMouseButton)){bPanningMap=false;return FReply::Handled().ReleaseMouseCapture();}
 const FVector2D Position=Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()),Size=Geometry.GetLocalSize();
 if(Size.X>0 && Size.Y>0)View.Pan((Position-LastDragPosition)/Size);
 LastDragPosition=Position;InvalidateLayoutAndVolatility();return FReply::Handled();
}
FReply URTSCampaignMinimap::NativeOnMouseButtonDoubleClick(const FGeometry& Geometry,const FPointerEvent& Event)
{
 if(Event.GetEffectingButton()!=EKeys::LeftMouseButton)return Super::NativeOnMouseButtonDoubleClick(Geometry,Event);
 ResetMapView();return FReply::Handled().ReleaseMouseCapture();
}
void URTSCampaignMinimap::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{ bPanningMap=bDraggingCamera=false;Super::NativeOnMouseCaptureLost(Event); }
