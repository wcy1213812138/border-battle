#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/RTSTypes.h"
#include "Economy/RTSEconomySnapshot.h"
#include "RTSResourceNode.generated.h"
class UStaticMeshComponent;
class UCapsuleComponent;
class UMaterialInterface;
UCLASS()
class ARTSResourceNode : public AActor
{
	GENERATED_BODY()
public:
	ARTSResourceNode();
	void Configure(ERTSResource Type, int32 Amount);
	int32 Extract(int32 Requested);
	void SetRenewable(bool bValue);
	bool IsRenewable() const { return bRenewable; }
	int32 GetRemaining() const { return Remaining; }
	int32 GetCapacity() const { return Capacity; }
	float GetRecoveryPerSecond() const { return RecoveryPerSecond; }
	void SetOwningTeam(uint8 Team);
	uint8 GetOwningTeam() const { return OwningTeam; }
	bool CanGather(uint8 Team) const { return bTierConfigured || OwningTeam == RTSTeam::Neutral || Team == OwningTeam; }
	ERTSResource GetResourceType() const { return ResourceType; }
	// Campaign ownership controls the front, not permission to harvest at the player's risk.
	int32 GetResourceTier() const { return ResourceTier; }
	FString GetResourceDisplayName() const;
	FString GetResourcePortraitPath() const;
	int32 GetGatherSlots() const { return GatherSlots; }
	bool IsVineyard() const { return bVineyard; }
	void ConfigureResourceTier(int32 Tier, bool bVineyard = false);
	void SetSelected(bool bValue);
	bool IsSelected() const { return bSelected; }
	bool UpdateCampaignCapture(float Seconds);
	float GetCaptureProgress() const { return CaptureSeconds / 5.f; }
	/** World label is drawn by the HUD with the Chinese runtime font; 3D text cannot render CJK glyphs. */
	FString GetWorldLabelText() const { return WorldLabelText; }
	FLinearColor GetWorldLabelColor() const;
	FSimpleMulticastDelegate OnResourceChanged;
	void ConfigureEconomy(float Multiplier,float Recovery,int32 MaxAmount,int32 Slots);
	float GetGatherIntervalMultiplier() const { return GatherMultiplier; }
	float GetRecoveryRemainder() const { return RecoveryRemainder; }
	void RestoreEconomyState(int32 Amount,float Remainder);
	FRTSResourceCaptureSnapshot CaptureOwnership() const;
	bool RestoreOwnership(const FRTSResourceCaptureSnapshot& State);
	static bool IsValidOwnership(const FRTSResourceCaptureSnapshot& State,uint8 Owner);
	bool TryReserveGatherer(class ARTSUnit* Unit);
protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void ApplyResourceVisual();
	void ApplyOreVisual();
	void BuildDecorations();
	void RefreshLabel();
	UStaticMeshComponent* AddVisualPart(FName PartName, const TCHAR* MeshPathName, const FVector& Location, const FVector& Scale, const FRotator& Rotation, UMaterialInterface* Material);
	void SetPartColor(UStaticMeshComponent* Part, const FLinearColor& Color);
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCapsuleComponent> Collision;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
	UPROPERTY(EditAnywhere) ERTSResource ResourceType = ERTSResource::Wood;
	UPROPERTY(EditAnywhere) int32 Remaining = 500;
	UPROPERTY(EditAnywhere) uint8 OwningTeam = RTSTeam::Neutral;
	bool bRenewable = false;
	int32 Capacity = 500;
	float RecoveryPerSecond = 0.5f;
	float RecoveryRemainder = 0;
	float GatherMultiplier = 1;
	int32 GatherSlots = 0;
	int32 ResourceTier = 1;
	bool bVineyard = false;
	// Only campaign nodes call ConfigureResourceTier; arena nodes keep the base visual.
	bool bTierConfigured = false;
	/** Set at the end of BeginPlay so runtime Configure/ConfigureResourceTier can refresh safely. */
	bool bVisualReady = false;
	bool bSelected = false;
	float CaptureSeconds = 0;
	uint8 CapturingTeam = RTSTeam::Neutral;
	int32 LabelRemaining = -1;
	uint8 LabelOwiningTeam = RTSTeam::Neutral;
	/** What the decorations were built for, so a tier/vineyard change rebuilds them once. */
	int32 DecorTier = 0;
	bool bDecorVineyard = false;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> GroundPad;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> SelectBars;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> VineParts;
	/** Baked by RefreshLabel and drawn by the HUD; no TextRender component is used for campaign labels. */
	FString WorldLabelText;
	FLinearColor WorldLabelColor = FLinearColor::White;
	TArray<TWeakObjectPtr<class ARTSUnit>> Gatherers;
};
