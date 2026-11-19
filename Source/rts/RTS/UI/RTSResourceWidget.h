// MiniRTS - event-bound resource stockpile and economy actions. Gameplay never references UI.
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Core/RTSTypes.h"
#include "RTSResourceWidget.generated.h"
class UTextBlock;
class UButton;
class UWrapBox;
class USizeBox;
class UProgressBar;
class URTSResourceComponent;
class ARTSBuilding;
UCLASS()
class URTSEconomyButton : public UButton
{
	GENERATED_BODY()
public:
	URTSEconomyButton();
};
UCLASS()
class URTSResourceWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;
	bool TryBind();
	UFUNCTION() void Refresh();
	void RefreshContext();
	void AddAction(UWrapBox* Row, FName Id, bool bBuild);
	void Train(FName Id);
	UFUNCTION() void BuildTownHall();
	UFUNCTION() void BuildBarracks();
	UFUNCTION() void BuildFarm();
	UFUNCTION() void BuildTower();
	UFUNCTION() void TrainPeasant();
	UFUNCTION() void TrainSwordman();
	UFUNCTION() void TrainArcher();
	UFUNCTION() void CancelQueue();
	UFUNCTION() void CancelPlacement();
	void ApplyStance(ERTSCombatStance Stance);
	void RefreshUnitPanel();
	UFUNCTION() void SetGuard();
	UFUNCTION() void SetPatrol();
	UFUNCTION() void SetAggressive();
private:
	UPROPERTY(Transient) TObjectPtr<UTextBlock> WoodText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> FoodText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ContextText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> ActionButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> ActionLabels;
	TArray<FName> ActionIds;
	TWeakObjectPtr<URTSResourceComponent> BoundResources;
	TWeakObjectPtr<ARTSBuilding> BoundBuilding;
	float ProgressRefresh = 0;
	bool bBoundController = false;
	UPROPERTY(Transient) TObjectPtr<class UBorder> UnitPanel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> UnitStatusText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StanceSummaryText;
	UPROPERTY(Transient) TArray<TObjectPtr<class USizeBox>> UnitPortraitFrames;
	TArray<FName> UnitPortraitKinds;
	bool bCampaignUI = false;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> StanceButtons;
	UPROPERTY(Transient) TObjectPtr<USizeBox> CommandSize;
	UPROPERTY(Transient) TObjectPtr<USizeBox> UnitListSize;
	UPROPERTY(Transient) TObjectPtr<UWrapBox> BuildActions;
	UPROPERTY(Transient) TObjectPtr<UWrapBox> TrainActions;
	UPROPERTY(Transient) TObjectPtr<UButton> CancelQueueButton;
	UPROPERTY(Transient) TObjectPtr<UButton> CancelPlacementButton;
	UPROPERTY(Transient) TObjectPtr<UProgressBar> ContextProgress;
};
