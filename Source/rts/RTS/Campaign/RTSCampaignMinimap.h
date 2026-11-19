#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Campaign/RTSCampaignMapView.h"
#include "RTSCampaignMinimap.generated.h"

struct FRTSCampaignMinimapPaintState
{
 int32 Day=-1,Halls=0,HallEdges=0,Resources=0,ResourcesInView=0,Units=0;
 FVector2D Size=FVector2D::ZeroVector;
 double PaintedAt=0;
};

/** Lightweight schematic tactical map. No scene capture or extra render target. */
UCLASS()
class URTSCampaignMinimap : public UUserWidget
{
 GENERATED_BODY()
private:
 UPROPERTY(Transient) TObjectPtr<class UTexture2D> TerrainTexture;
 TWeakObjectPtr<AActor> CachedScenery;
 void UpdateTerrainTexture();
 void FocusCameraAt(const FGeometry& Geometry,const FVector2D& ScreenPosition);
 FRTSCampaignMapView View;
 bool bPanningMap=false;
 bool bDraggingCamera=false;
 FVector2D LastDragPosition=FVector2D::ZeroVector;
 mutable int32 LastPaintDay=-1;
 mutable FRTSCampaignMinimapPaintState LastPaintState;
public:
 bool HasTerrainTexture() const { return TerrainTexture!=nullptr; }
 const FRTSCampaignMapView& GetMapView() const { return View; }
 bool IsDraggingCamera() const { return bDraggingCamera; }
 void ResetMapView();
 const FRTSCampaignMinimapPaintState& GetLastPaintState() const { return LastPaintState; }
protected:
 virtual void NativeOnInitialized() override;
 virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
 virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
 virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event) override;
 virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
 virtual FReply NativeOnMouseMove(const FGeometry& Geometry,const FPointerEvent& Event) override;
 virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& Geometry,const FPointerEvent& Event) override;
 virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
};
