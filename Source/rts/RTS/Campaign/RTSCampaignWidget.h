// MiniRTS - campaign operations panel. Presentation only; every rule lives in ARTSCampaign.
#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSCampaignWidget.generated.h"
class UBorder;
class UButton;
class UHorizontalBox;
class UScrollBox;
class USizeBox;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;
class ARTSCampaign;
class UImage;

UCLASS()
class URTSCampaignWidget : public UUserWidget
{
	GENERATED_BODY()
public:
 int32 GetActivePage() const { return ActivePage; }
 void OpenPage(int32 Index) { SelectPage(Index); }
 FName GetDisplayedBuilding() const { return LastService; }
 void RefreshSelectedObject() { Refresh(); }
 bool HasBuildingPortrait() const;
 bool IsOnlyServiceVisible(FName Kind) const;
 bool IsResourceDetailsVisible() const;
 UFUNCTION() void TogglePause();
 bool IsPauseMenuVisible() const;
 UFUNCTION() void RequestReturnToMenu();
 UFUNCTION() void ConfirmReturnToMenu();
 UFUNCTION() void CancelReturnToMenu();
	/** P0-05: save first, then leave the battlefield; a failed save keeps the panel open. */
	UFUNCTION() void SaveThenReturn();
 UFUNCTION() void ShowUnitUpgrades();
 UFUNCTION() void ShowUnitResearch();
 bool IsUnitResearchPage() const { return bUnitResearchPage; }
 UFUNCTION() void ShowSettings();
 bool IsSettingsOpen() const;
 UFUNCTION() void ResetMinimap();
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	// Reads the campaign snapshot into the labels. Never rebuilds the widget tree.
	void Refresh();
	void AddSection(UVerticalBox* Column, const FString& Caption);
	UButton* AddRow(UVerticalBox* Column, const FString& Caption, FName Function, TObjectPtr<UTextBlock>* OutLabel = nullptr, FName PortraitKind = NAME_None);
	// Tab strip entry; the active page keeps the accent style and a marker prefix.
	UButton* AddNavButton(UHorizontalBox* Bar, const FString& Caption, FName Function, int32 PageIndex);
	// Creates one page column inside the switcher and returns its content column.
	UVerticalBox* AddPage(const FString& Name);
	void SelectPage(int32 Index);
	UFUNCTION() void ShowCampaignPage();
	UFUNCTION() void ShowGrowthPage();
	UFUNCTION() void ShowProductionPage();
	UFUNCTION() void ShowSystemPage();
 UFUNCTION() void ConfirmGuide();
	UFUNCTION() void UpgradeSword();
	UFUNCTION() void UpgradeArcher();
	UFUNCTION() void AssignWood();
	UFUNCTION() void AssignFood();
	UFUNCTION() void AssignIron();
	UFUNCTION() void AssignRefined();
	UFUNCTION() void AttackIron();
	UFUNCTION() void AttackRichIron();
	UFUNCTION() void AttackRefined();
	UFUNCTION() void AttackRichRefined();
	UFUNCTION() void AttackLord();
	UFUNCTION() void AttackOutpost1();
	UFUNCTION() void AttackOutpost2();
	UFUNCTION() void BuyHousing();
	UFUNCTION() void BuyIronOutpost();
	UFUNCTION() void BuyRichIron();
	UFUNCTION() void BuyRefinedOutpost();
	UFUNCTION() void BuyRichRefined();
	UFUNCTION() void RecruitWorker();
	UFUNCTION() void RecruitSword();
	UFUNCTION() void RecruitArcher();
	UFUNCTION() void FocusHome();
	UFUNCTION() void PrepareSave();
	UFUNCTION() void Save();
	UFUNCTION() void Load();
 UFUNCTION() void CancelTraining();
 UFUNCTION() void BuildFrontlineBarracks();
 UFUNCTION() void BuildArcheryRange();
 UFUNCTION() void BuildResearchInstitute();
 UFUNCTION() void BuildSupportBarracks();
 UFUNCTION() void BuildFarm();
 UFUNCTION() void BuildTower();
 UFUNCTION() void CycleHighlandColor();
 UFUNCTION() void CycleValleyColor();
 UFUNCTION() void SelectBase();
 UFUNCTION() void SelectMelee();
 UFUNCTION() void SelectRanged();
 UFUNCTION() void SelectResearch();
 UFUNCTION() void SelectSupport();
 UFUNCTION() void UnlockSpear();
 UFUNCTION() void AttackRedBase();
 UFUNCTION() void AttackPinkBase();
 UFUNCTION() void CycleRedColor();
 UFUNCTION() void CyclePinkColor();
 UFUNCTION() void UnlockScout();
 UFUNCTION() void UnlockPriest();
 UFUNCTION() void RecruitSpear();
 UFUNCTION() void RecruitScout();
 UFUNCTION() void RecruitPriest();
 UFUNCTION() void CloseTools();
private:
	UPROPERTY(Transient) TObjectPtr<UBorder> Board;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> QueuePanel;
 UPROPERTY(Transient) TArray<TObjectPtr<UBorder>> QueueRows;
 UPROPERTY(Transient) TArray<TObjectPtr<UImage>> QueueImages;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> QueueLabels;
 TArray<FString> QueuePortraitPaths;
 UPROPERTY(Transient) TObjectPtr<UBorder> AlertBanner;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> AlertLabel;
 UPROPERTY(Transient) TObjectPtr<UBorder> PauseOverlay;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> ReturnConfirmation;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ReturnStatus;
	// One settlement card per end state; dismissing it returns to normal play.
	UPROPERTY(Transient) TObjectPtr<UBorder> SettlementOverlay;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SettlementTitle;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SettlementBody;
	UFUNCTION() void KeepCampaignRunning();
	UFUNCTION() void SettlementSaveAndReturn();
 UPROPERTY(Transient) TArray<TObjectPtr<class URTSCampaignAction>> UnitActions;
 UPROPERTY(Transient) TObjectPtr<UBorder> StockBoard;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> UnitActionLabels;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StockLabels;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> PopulationLabel;
 UPROPERTY(Transient) TObjectPtr<UBorder> ResourceDetails;
 UPROPERTY(Transient) TObjectPtr<class URTSCampaignMinimap> CampaignMinimap;
 UPROPERTY(Transient) TObjectPtr<UImage> ResourcePortrait;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> ResourceDetailsText;
 FString LastResourcePortrait;
	UPROPERTY(Transient) TObjectPtr<USizeBox> PanelSize;
	UPROPERTY(Transient) TObjectPtr<UScrollBox> PageScroll;
	UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> Pages;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> NavButtons;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> NavLabels;
	// Tab captions without the selected marker; kept parallel to NavButtons.
	TArray<FString> NavCaptions;
	int32 ActivePage = 0;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> OverviewText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ObjectivesText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SwordLabel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> ArcherLabel;
	UPROPERTY(Transient) TObjectPtr<UButton> SwordButton;
	UPROPERTY(Transient) TObjectPtr<UButton> ArcherButton;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> GuideText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> WarText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> PageTitle;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> PageSubtitle;
 UPROPERTY(Transient) TObjectPtr<UButton> GuideButton;
 UPROPERTY(Transient) TObjectPtr<UBorder> Dimmer;
 UPROPERTY(Transient) TObjectPtr<UBorder> GuideCard;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> ContextText;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> BaseActions;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> WorkerActions;
 UPROPERTY(Transient) TArray<TObjectPtr<UButton>> WorkerBuildButtons;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> WorkerBuildLabels;
 TArray<FName> WorkerBuildKinds;
 UPROPERTY(Transient) TArray<TObjectPtr<UButton>> WorkerFacilityButtons;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> WorkerFacilityLabels;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> WorkerBuildHint;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> MeleeActions;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> RangedActions;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> UpgradeList;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> UnlockList;
 UPROPERTY(Transient) TObjectPtr<UButton> UpgradeTab;
 UPROPERTY(Transient) TObjectPtr<UButton> ResearchTab;
 bool bUnitResearchPage=false;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> ResearchActions;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> SupportActions;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> SpearResearchLabel;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> ScoutResearchLabel;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> PriestResearchLabel;
 UPROPERTY(Transient) TObjectPtr<UImage> BuildingPortrait;
 UPROPERTY(Transient) TObjectPtr<UBorder> ToolsPanel;
 UPROPERTY(Transient) TObjectPtr<UWidgetSwitcher> ToolPages;
 UPROPERTY(Transient) TObjectPtr<USizeBox> ToolsSize;
 UPROPERTY(Transient) TObjectPtr<class URTSCampaignSettingsWidget> Settings;
 UPROPERTY(Transient) TObjectPtr<UButton> CancelTrainingButton;
 TWeakObjectPtr<class ARTSBuilding> LastBuilding;
 FString EventDialogue;
 int32 SeenEvents = 0;
 FName LastService;
 bool bHadCampaign = false;
 bool bLastWorkerView = false;
	float LabelTimer = 0.f;
	bool bSettlementShown = false;
	bool bSettlementDefeat = false;
};
