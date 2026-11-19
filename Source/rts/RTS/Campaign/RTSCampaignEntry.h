#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/GameModeBase.h"
#include "Containers/Ticker.h"
#include "RTSCampaignEntry.generated.h"

class UBorder;
class URTSCampaignEntryWidget;
class UButton;
class UTextBlock;
class UVerticalBox;

namespace RTSCampaignEntry
{
 const TCHAR* MenuMap();
 const TCHAR* CampaignMap();
 bool IsContinueRequest(const UObject* Context);
 void OpenMenu(const UObject* Context);
}

/** Empty, independent frontend; no RTS pawn, HUD or battlefield is spawned here. */
UCLASS()
class ARTSCampaignMenuGameMode : public AGameModeBase
{
 GENERATED_BODY()
public:
 ARTSCampaignMenuGameMode();
 virtual void BeginPlay() override;
private:
 UPROPERTY() TObjectPtr<class URTSCampaignCookAssets> CookAssets;
};

/** Also covers campaign startup, so failed navigation/loads always have an escape path. */
UCLASS()
class URTSCampaignEntryWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 UFUNCTION() void ContinueCampaign();
 UFUNCTION() void NewCampaign();
 UFUNCTION() void ConfirmNewCampaign();
 UFUNCTION() void CancelNewCampaign();
 UFUNCTION() void ReturnToMenu();
 UFUNCTION() void Quit();
	/** Left-click selects a slot; ContinueCampaign opens the selected record. */
	void RequestLoadSlot(int32 Index);
	/** Start-menu cleanup: right-click a listed slot row to delete that slot and its own backups. */
	void RequestCleanSlot(int32 Index);
	bool IsConfirmingSlotClean() const { return PendingCleanSlot >= 0; }
	void SetSlotHover(int32 Index, bool bHovered);
	UFUNCTION() void ConfirmCleanSlot();
	UFUNCTION() void CancelCleanSlot();
 bool IsConfirmingNewCampaign() const { return bConfirming; }
 bool HasEntryFailed() const { return bFailed; }
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
 virtual FReply NativeOnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
private:
 UButton* AddButton(UVerticalBox* Column, const TCHAR* Name, const FString& Caption, FName Function);
 void OpenCampaign(bool bContinue);
 void UpdateStartup();
 FTSTicker::FDelegateHandle StartupTicker;
 void ShowError(const FString& Error);
	void BuildSlotRows(UVerticalBox* Column);
	void RefreshSlotRows();
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SlotSection;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SlotRows;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> CleanConfirmation;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CleanStatus;
	UPROPERTY(Transient) TArray<TObjectPtr<class URTSCampaignSaveSlotRowWidget>> SlotRowWidgets;
	int32 PendingCleanSlot = -1;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> MenuActions;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> Confirmation;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> Recovery;
 UPROPERTY(Transient) TObjectPtr<UButton> ContinueButton;
 UPROPERTY(Transient) TObjectPtr<UButton> NewButton;
 bool bLoading = false;
 bool bTransitioning = false;
 bool bFailed = false;
 bool bConfirming = false;
 double StartupBegan = 0;
};

/**
 * One start-menu save row. Left-click selects; right-click asks for cleanup confirmation.
 * Selecting never loads a world or modifies the saved record.
 */
UCLASS()
class URTSCampaignSaveSlotRowWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 void Setup(int32 InIndex, URTSCampaignEntryWidget* InOwner);
 int32 GetSlotIndex() const { return Index; }
protected:
 virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
 virtual void NativeOnMouseEnter(const FGeometry& Geometry, const FPointerEvent& Event) override;
 virtual void NativeOnMouseLeave(const FPointerEvent& Event) override;
private:
 void Refresh();
 void Paint(bool bHovered);
 UPROPERTY(Transient) TObjectPtr<UBorder> Frame;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Caption;
 UPROPERTY(Transient) TObjectPtr<URTSCampaignEntryWidget> Owner;
 int32 Index = 0;
 bool bHovered = false;
};
