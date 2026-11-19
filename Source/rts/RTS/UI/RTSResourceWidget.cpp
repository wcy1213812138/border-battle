#include "UI/RTSResourceWidget.h"
#include "Campaign/RTSCampaign.h"
#include "Economy/RTSResourceNode.h"
#include "UI/RTSHUD.h"
#include "Core/RTSHumanController.h"
#include "Core/RTSPlayerState.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSGathererComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Selection/RTSSelectionComponent.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/WrapBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "UI/RTSUIFont.h"
#include "Core/RTSDisplayNames.h"
#include "Match/RTSMatchSubsystem.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "UI/RTSUIStyle.h"
#include "Components/WrapBox.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "RTSEconomyWidget"
namespace
{
	UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		RTSUIStyle::Text(Block, Size);
		Block->SetText(Text);
		return Block;
	}
}
URTSEconomyButton::URTSEconomyButton()
{
	InitIsFocusable(false);
	RTSUIStyle::Button(this);
}
void URTSResourceWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	bCampaignUI = GetWorld() && Cast<ARTSCampaignGameMode>(GetWorld()->GetAuthGameMode()) != nullptr;
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
	WidgetTree->RootWidget = Canvas;
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	auto Panel = [&](FVector2D Anchor, FVector2D Position, FVector2D Alignment, FName Name)
	{
		UBorder* Border = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		RTSUIStyle::Panel(Border);
		UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Border);
		PanelSlot->SetAnchors(FAnchors(Anchor.X, Anchor.Y)); PanelSlot->SetPosition(Position); PanelSlot->SetAlignment(Alignment); PanelSlot->SetAutoSize(true);
		return Border;
	};
	UBorder* Top = Panel(FVector2D::ZeroVector, FVector2D(20, 20), FVector2D::ZeroVector, TEXT("StockPanel"));
	UHorizontalBox* Stocks = WidgetTree->ConstructWidget<UHorizontalBox>(); Top->SetContent(Stocks);
	WoodText = MakeText(WidgetTree, FText::GetEmpty(), 17); FoodText = MakeText(WidgetTree, FText::GetEmpty(), 17);
	WoodText->SetColorAndOpacity(RTSUIStyle::Gold()); FoodText->SetColorAndOpacity(RTSUIStyle::Accent());
	Stocks->AddChildToHorizontalBox(WoodText)->SetPadding(FMargin(0, 0, 28, 0)); Stocks->AddChild(FoodText);
	UBorder* Bottom = Panel(FVector2D(0, 1), FVector2D(20, -20), FVector2D(0, 1), TEXT("CommandPanel"));
	CommandSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("CommandSize"));
	CommandSize->SetWidthOverride(560); Bottom->SetContent(CommandSize);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(); CommandSize->SetContent(Column);
	UTextBlock* Title = MakeText(WidgetTree, LOCTEXT("CommandTitle", "指令面板"), 15); RTSUIStyle::Text(Title, 15, RTSUIStyle::Gold()); Column->AddChild(Title);
	ContextText = MakeText(WidgetTree, LOCTEXT("Context", "请选择工人或己方建筑。"), 12); Column->AddChild(ContextText);
	ContextText->SetAutoWrapText(true);
	ContextProgress = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("ContextProgress"));
	ContextProgress->SetFillColorAndOpacity(RTSUIStyle::Accent()); ContextProgress->SetVisibility(ESlateVisibility::Collapsed);
	USizeBox* ProgressSize = WidgetTree->ConstructWidget<USizeBox>(); ProgressSize->SetHeightOverride(6); ProgressSize->SetContent(ContextProgress); Column->AddChild(ProgressSize);
	BuildActions = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("BuildActions"));
	BuildActions->SetInnerSlotPadding(FVector2D(6, 6)); Column->AddChild(BuildActions);
	BuildActions->SetExplicitWrapSize(true); BuildActions->SetWrapSize(560);
	for (FName Id : {FName("TownHall"), FName("Barracks"), FName("Farm"), FName("Tower")}) { AddAction(BuildActions, Id, true); }
	TrainActions = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("TrainActions"));
	TrainActions->SetInnerSlotPadding(FVector2D(6, 6)); Column->AddChild(TrainActions);
	TrainActions->SetExplicitWrapSize(true); TrainActions->SetWrapSize(560);
	for (FName Id : {FName("Peasant"), FName("Swordman"), FName("Archer")}) { AddAction(TrainActions, Id, false); }
	CancelQueueButton = WidgetTree->ConstructWidget<URTSEconomyButton>(URTSEconomyButton::StaticClass(), TEXT("CancelQueueButton"));
	CancelQueueButton->SetContent(MakeText(WidgetTree, LOCTEXT("CancelQueue", "取消队尾并退款"), 11));
	CancelQueueButton->OnClicked.AddDynamic(this, &URTSResourceWidget::CancelQueue); TrainActions->AddChild(CancelQueueButton);
	CancelPlacementButton = WidgetTree->ConstructWidget<URTSEconomyButton>(URTSEconomyButton::StaticClass(), TEXT("CancelPlacementButton"));
	CancelPlacementButton->SetContent(MakeText(WidgetTree, LOCTEXT("CancelBuild", "取消放置"), 11));
	CancelPlacementButton->OnClicked.AddDynamic(this, &URTSResourceWidget::CancelPlacement); Column->AddChild(CancelPlacementButton);
	MessageText = MakeText(WidgetTree, LOCTEXT("Hint", "工人：右键资源采集，右键工地施工；退格键停止。"), 11);
	RTSUIStyle::Text(MessageText, 11, RTSUIStyle::Muted()); MessageText->SetAutoWrapText(true); Column->AddChild(MessageText);
	UnitPanel = Panel(FVector2D(1, 1), FVector2D(-20, -20), FVector2D(1, 1), TEXT("UnitPanel"));
	USizeBox* PanelSize = WidgetTree->ConstructWidget<USizeBox>(); PanelSize->SetWidthOverride(330); UnitPanel->SetContent(PanelSize);
	UVerticalBox* UnitColumn = WidgetTree->ConstructWidget<UVerticalBox>(); PanelSize->SetContent(UnitColumn);
	if (bCampaignUI)
	{
		// Campaign-only portrait strip: one thumbnail per trainable kind, collapsed when not selected.
		UWrapBox* PortraitRow = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(), TEXT("UnitPortraitRow"));
		PortraitRow->SetInnerSlotPadding(FVector2D(4,4));
		UnitColumn->AddChildToVerticalBox(PortraitRow)->SetPadding(FMargin(0, 0, 0, 6));
		const TCHAR* PortraitKinds[] = {TEXT("Peasant"),TEXT("Swordman"),TEXT("Archer"),TEXT("Spearman"),TEXT("Scout"),TEXT("Priest"),TEXT("Paladin"),TEXT("Mage"),TEXT("Light_Infantry"),TEXT("HighPriest"),TEXT("King"),TEXT("Heavy_Infantry"),TEXT("HeavySwordman"),TEXT("Halberdier"),TEXT("Crossbowman"),TEXT("Commander")};
		for (int32 i = 0; i < UE_ARRAY_COUNT(PortraitKinds); ++i)
		{
			USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>();
			Frame->SetWidthOverride(96); Frame->SetHeightOverride(96); Frame->SetVisibility(ESlateVisibility::Collapsed);
			UImage* Portrait = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Portrait->SetBrushFromTexture(LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("/Game/RTS/CampaignArt/UI/T_Unit_%s.T_Unit_%s"), PortraitKinds[i], PortraitKinds[i])));
			Frame->SetContent(Portrait);
			PortraitRow->AddChildToWrapBox(Frame);
			UnitPortraitFrames.Add(Frame); UnitPortraitKinds.Add(FName(PortraitKinds[i]));
		}
	}
	UTextBlock* UnitTitle = MakeText(WidgetTree, LOCTEXT("UnitTitle", "单位状态与攻击倾向"), 15); RTSUIStyle::Text(UnitTitle, 15, RTSUIStyle::Gold()); UnitColumn->AddChild(UnitTitle);
	StanceSummaryText = MakeText(WidgetTree, FText::GetEmpty(), 11); StanceSummaryText->SetAutoWrapText(true); UnitColumn->AddChild(StanceSummaryText);
	UHorizontalBox* StanceRow = WidgetTree->ConstructWidget<UHorizontalBox>(); UnitColumn->AddChild(StanceRow);
	const TCHAR* Names[] = {TEXT("原地警戒"), TEXT("巡逻"), TEXT("侵略")};
	const TCHAR* Tips[] = {TEXT("不主动追敌；被攻击后仅在射程内还击，手动命令优先。"),
		TEXT("驻点周围约10米发现敌人就迎击，追击边界约14米；不是往返巡逻路线。"),
		TEXT("约18米内主动索敌，追击边界约28米；手动命令优先。")};
	for (int32 i = 0; i < 3; ++i)
	{
		UButton* StanceButton = WidgetTree->ConstructWidget<URTSEconomyButton>();
		StanceButton->SetContent(MakeText(WidgetTree, FText::FromString(Names[i]), 11));
		StanceButton->SetToolTipText(FText::FromString(Tips[i])); StanceRow->AddChildToHorizontalBox(StanceButton)->SetPadding(FMargin(0, 6, 6, 6));
		StanceButtons.Add(StanceButton);
		if (i == 0) { StanceButton->OnClicked.AddDynamic(this, &URTSResourceWidget::SetGuard); }
		if (i == 1) { StanceButton->OnClicked.AddDynamic(this, &URTSResourceWidget::SetPatrol); }
		if (i == 2) { StanceButton->OnClicked.AddDynamic(this, &URTSResourceWidget::SetAggressive); }
	}
	UnitListSize = WidgetTree->ConstructWidget<USizeBox>(); UnitListSize->SetMaxDesiredHeight(220); UnitColumn->AddChild(UnitListSize);
	UScrollBox* List = WidgetTree->ConstructWidget<UScrollBox>(); UnitListSize->SetContent(List);
	UnitStatusText = MakeText(WidgetTree, FText::GetEmpty(), 12); UnitStatusText->SetWrapTextAt(310); List->AddChild(UnitStatusText);
	UnitPanel->SetVisibility(ESlateVisibility::Collapsed);
	if (bCampaignUI)
	{
		Bottom->SetVisibility(ESlateVisibility::Collapsed); Top->SetVisibility(ESlateVisibility::Collapsed);
		// Bottom-right, 6px left of the 360-wide campaign board (board right margin 4), 16px above the screen bottom.
		if (auto* UnitSlot = Cast<UCanvasPanelSlot>(UnitPanel->Slot))
		{
			UnitSlot->SetAnchors(FAnchors(1, 1)); UnitSlot->SetAlignment(FVector2D(1, 1)); UnitSlot->SetPosition(FVector2D(-370, -16));
		}
	}
	TryBind(); Refresh();
}
void URTSResourceWidget::AddAction(UWrapBox* Row, FName Id, bool bBuild)
{
	UButton* Action = WidgetTree->ConstructWidget<URTSEconomyButton>();
	UTextBlock* Label = MakeText(WidgetTree, RTSDisplayNames::Entity(Id), 11); Action->SetContent(Label);
	Row->AddChild(Action);
	ActionButtons.Add(Action); ActionLabels.Add(Label); ActionIds.Add(Id);
	if (bBuild && Id == TEXT("TownHall")) { Action->OnClicked.AddDynamic(this, &URTSResourceWidget::BuildTownHall); }
	if (bBuild && Id == TEXT("Barracks")) { Action->OnClicked.AddDynamic(this, &URTSResourceWidget::BuildBarracks); }
	if (bBuild && Id == TEXT("Farm")) { Action->OnClicked.AddDynamic(this, &URTSResourceWidget::BuildFarm); }
	if (bBuild && Id == TEXT("Tower")) { Action->OnClicked.AddDynamic(this, &URTSResourceWidget::BuildTower); }
	if (!bBuild && Id == TEXT("Peasant")) { Action->OnClicked.AddDynamic(this, &URTSResourceWidget::TrainPeasant); }
	if (!bBuild && Id == TEXT("Swordman")) { Action->OnClicked.AddDynamic(this, &URTSResourceWidget::TrainSwordman); }
	if (!bBuild && Id == TEXT("Archer")) { Action->OnClicked.AddDynamic(this, &URTSResourceWidget::TrainArcher); }
}
bool URTSResourceWidget::TryBind()
{
	ARTSHumanController* PC = Cast<ARTSHumanController>(GetOwningPlayer());
	if (PC && !bBoundController)
	{
		if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this)) { Match->OnMatchChanged.AddUObject(this, &URTSResourceWidget::RefreshContext); }
		PC->OnEconomyContextChanged.AddUObject(this, &URTSResourceWidget::RefreshContext);
		PC->GetSelection()->OnSelectionChanged.AddDynamic(this, &URTSResourceWidget::Refresh);
		bBoundController = true;
	}
	if (BoundResources.IsValid()) { return true; }
	const ARTSPlayerState* PS = PC ? PC->GetPlayerState<ARTSPlayerState>() : nullptr;
	URTSResourceComponent* Resources = PS ? PS->GetResources() : nullptr;
	if (!Resources) { return false; }
	BoundResources = Resources; Resources->OnResourcesChanged.AddDynamic(this, &URTSResourceWidget::Refresh);
	Refresh(); return true;
}
void URTSResourceWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	const FVector2D View = Geometry.GetLocalSize();
	if (View.X > 0 && CommandSize)
	{
		const float Width = RTSUIStyle::CommandWidth(View.X);
		CommandSize->SetWidthOverride(Width); BuildActions->SetWrapSize(Width); TrainActions->SetWrapSize(Width);
		UnitListSize->SetMaxDesiredHeight(FMath::Clamp(static_cast<float>(View.Y) - 390.f, 90.f, 260.f));
	}
	if (!BoundResources.IsValid()) { TryBind(); }
	ProgressRefresh += DeltaTime;
	if (ProgressRefresh >= 0.2f) { ProgressRefresh = 0; RefreshContext(); }
}
void URTSResourceWidget::NativeDestruct()
{
	if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this)) { Match->OnMatchChanged.RemoveAll(this); }
	if (URTSResourceComponent* Stock = BoundResources.Get()) { Stock->OnResourcesChanged.RemoveDynamic(this, &URTSResourceWidget::Refresh); }
	if (ARTSHumanController* PC = Cast<ARTSHumanController>(GetOwningPlayer()))
	{ PC->OnEconomyContextChanged.RemoveAll(this); PC->GetSelection()->OnSelectionChanged.RemoveDynamic(this, &URTSResourceWidget::Refresh); }
	if (ARTSBuilding* Building = BoundBuilding.Get())
	{ Building->OnBuildingChanged.RemoveAll(this); Building->GetProducer()->OnQueueChanged.RemoveAll(this); }
	BoundResources.Reset(); BoundBuilding.Reset();
	Super::NativeDestruct();
}
void URTSResourceWidget::Refresh()
{
	const URTSResourceComponent* Stock = BoundResources.Get();
	if (WoodText) { WoodText->SetText(FText::Format(LOCTEXT("Wood", "木材：{0}"), FText::AsNumber(Stock ? Stock->GetAmount(ERTSResource::Wood) : 0))); }
	if (FoodText) { FoodText->SetText(FText::Format(LOCTEXT("Food", "食物：{0}"), FText::AsNumber(Stock ? Stock->GetAmount(ERTSResource::Food) : 0))); }
	RefreshContext();
}
void URTSResourceWidget::RefreshContext()
{
	ARTSHumanController* PC = Cast<ARTSHumanController>(GetOwningPlayer());
	if (!PC || !ContextText) { return; }
 if(Cast<ARTSCampaignGameMode>(GetWorld()->GetAuthGameMode())){RefreshUnitPanel();return;}
	const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this);
	SetVisibility(!Match || Match->AllowsGameplay() ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	RefreshUnitPanel();
	ARTSBuilding* Building = PC->GetSelectedBuilding();
	if (BoundBuilding.Get() != Building)
	{
		if (ARTSBuilding* Old = BoundBuilding.Get()) { Old->OnBuildingChanged.RemoveAll(this); Old->GetProducer()->OnQueueChanged.RemoveAll(this); }
		BoundBuilding = Building;
		if (Building) { Building->OnBuildingChanged.AddUObject(this, &URTSResourceWidget::RefreshContext); Building->GetProducer()->OnQueueChanged.AddUObject(this, &URTSResourceWidget::RefreshContext); }
	}
	bool bWorker = false;
	const TArray<ARTSUnit*> Selected = PC->GetSelection()->GetSelectedUnits();
	FString Context = TEXT("请选择单位或己方建筑。工人可采集资源和建造建筑。");
	TMap<FString, int32> StatusCounts;
	for (const ARTSUnit* Unit : Selected)
	{
		if (!IsValid(Unit) || !Unit->IsAlive()) { continue; }
		const URTSUnitData* Data = Unit->GetUnitData();
		bWorker |= Data && Data->bWorker;
		const FRTSUnitActivity Activity = ARTSHUD::DescribeUnitActivity(Unit);
		++StatusCounts.FindOrAdd(Activity.Summary);
		if (Selected.Num() == 1)
		{
			const FString Name = Data && !Data->BalanceId.IsNone() ? RTSDisplayNames::Entity(Data->BalanceId).ToString()
				: Data && !Data->DisplayName.IsEmpty() ? Data->DisplayName.ToString() : TEXT("单位");
			Context = Name + TEXT("｜") + Activity.Label;
			if (Data && Data->bWorker && Unit->GetGatherer())
			{
				const URTSGathererComponent* Gatherer = Unit->GetGatherer();
				Context += FString::Printf(TEXT("｜携带：%d %s"), Gatherer->GetCarriedAmount(),
					Gatherer->GetCarriedType() == ERTSResource::Wood ? TEXT("木材") : TEXT("食物"));
			}
		}
	}
	if (Selected.Num() > 1)
	{
		TArray<FString> States;
		StatusCounts.GetKeys(States);
		States.Sort(); // Stable ordering as units change state; no last-worker overwrite.
		TArray<FString> Parts;
		for (const FString& State : States) { Parts.Add(FString::Printf(TEXT("%s：%d"), *State, StatusCounts[State])); }
		Context = FString::Printf(TEXT("已选单位：%d｜%s"), Selected.Num(), *FString::Join(Parts, TEXT("｜")));
	}
	if (Building)
	{
		const URTSProducerComponent* Producer = Building->GetProducer();
		TArray<FString> Names; for (FName Name : Producer->GetQueue()) { Names.Add(RTSDisplayNames::Entity(Name).ToString()); }
		Context = FString::Printf(TEXT("%s｜建造：%d%%｜队列 %d/5：%s｜训练：%d%% %s"), *RTSDisplayNames::Entity(Building->GetBuildingId()).ToString(), FMath::RoundToInt(Building->GetBuildProgress() * 100), Names.Num(), Names.IsEmpty() ? TEXT("空") : *FString::Join(Names, TEXT("、")), FMath::RoundToInt(Producer->GetProgress() * 100), *Producer->GetSpawnBlockReason());
		if (!Building->IsComplete())
		{
			Context = FString::Printf(TEXT("%s｜建造 %d%%｜%s｜进度保留，续建不重复扣费"),
				*RTSDisplayNames::Entity(Building->GetBuildingId()).ToString(), FMath::FloorToInt(Building->GetBuildProgress() * 100), *Building->GetConstructionStatus());
		}
		else if (const ARTSResourceNode* Food = Building->GetFoodSource())
		{
			Context = FString::Printf(TEXT("农场｜食物 %d/%d｜每2秒恢复1｜选工人右键采集"), Food->GetRemaining(), Food->GetCapacity());
		}
	}
	ContextText->SetText(FText::FromString(Context));
	BuildActions->SetVisibility(bWorker ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	const bool bProducer = Building && Building->IsComplete() && (Building->GetBuildingId() == TEXT("TownHall") || Building->GetBuildingId() == TEXT("Barracks"));
	TrainActions->SetVisibility(bProducer ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	CancelPlacementButton->SetVisibility(PC->IsPlacingBuilding() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	CancelQueueButton->SetIsEnabled(bProducer && Building->GetProducer()->GetQueue().Num() > 0);
	const bool bProgress = Building && (!Building->IsComplete() || Building->GetProducer()->GetQueue().Num() > 0);
	ContextProgress->SetVisibility(bProgress ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (bProgress) { ContextProgress->SetPercent(Building->IsComplete() ? Building->GetProducer()->GetProgress() : Building->GetBuildProgress()); }
	if (!PC->GetEconomyMessage().IsEmpty()) { MessageText->SetText(FText::FromString(PC->GetEconomyMessage())); }
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
	for (int32 i = 0; i < ActionIds.Num(); ++i)
	{
		const FRTSBalanceRow* Data = Balance ? Balance->Find(ActionIds[i]) : nullptr;
		if (!Data) { ActionButtons[i]->SetIsEnabled(false); continue; }
		const bool bBuild = Data->Kind == TEXT("Building");
		ActionLabels[i]->SetText(FText::FromString(FString::Printf(TEXT("%s%s\n木材 %d　食物 %d"), bBuild ? TEXT("建造") : TEXT("训练"), *RTSDisplayNames::Entity(ActionIds[i]).ToString(), Data->WoodCost, Data->FoodCost)));
		const bool bAllowed = bBuild ? bWorker : Building && Building->IsComplete()
			&& ((Building->GetBuildingId() == TEXT("TownHall") && ActionIds[i] == TEXT("Peasant"))
			|| (Building->GetBuildingId() == TEXT("Barracks") && ActionIds[i] != TEXT("Peasant")));
		ActionButtons[i]->SetVisibility(bAllowed ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		const URTSResourceComponent* Stock = BoundResources.Get();
		const bool bAffordable = Stock && Stock->GetAmount(ERTSResource::Wood) >= Data->WoodCost && Stock->GetAmount(ERTSResource::Food) >= Data->FoodCost;
		const bool bQueueRoom = bBuild || (Building && Building->GetProducer()->GetQueue().Num() < 5);
		ActionButtons[i]->SetIsEnabled(bAllowed && bAffordable && bQueueRoom);
		ActionButtons[i]->SetToolTipText(FText::FromString(!bAffordable ? TEXT("资源不足，请先采集并交回资源。") : !bQueueRoom ? TEXT("训练队列已满，最多5个订单。") : bBuild ? TEXT("点击后左键确认位置；右键取消，不扣资源。") : TEXT("加入当前建筑的训练队列，立即支付费用。")));
	}
}
void URTSResourceWidget::BuildTownHall() { if (auto* PC = Cast<ARTSHumanController>(GetOwningPlayer())) { PC->BeginBuildingPlacement(TEXT("TownHall")); } }
void URTSResourceWidget::BuildBarracks() { if (auto* PC = Cast<ARTSHumanController>(GetOwningPlayer())) { PC->BeginBuildingPlacement(TEXT("Barracks")); } }
void URTSResourceWidget::BuildFarm() { if (auto* PC = Cast<ARTSHumanController>(GetOwningPlayer())) { PC->BeginBuildingPlacement(TEXT("Farm")); } }
void URTSResourceWidget::BuildTower() { if (auto* PC = Cast<ARTSHumanController>(GetOwningPlayer())) { PC->BeginBuildingPlacement(TEXT("Tower")); } }
void URTSResourceWidget::Train(FName Id)
{
	if (auto* PC = Cast<ARTSHumanController>(GetOwningPlayer()))
	{
		ARTSBuilding* Building = PC->GetSelectedBuilding(); FString Error;
		if (!Building) { PC->SetEconomyMessage(TEXT("请先选中已建成的生产建筑。")); return; }
		if (!Building->GetProducer()->Enqueue(Id, Error)) { PC->SetEconomyMessage(Error); }
		else { PC->SetEconomyMessage(TEXT("已加入训练队列；取消队尾可退回该订单的全部费用。")); }
	}
}
void URTSResourceWidget::TrainPeasant() { Train(TEXT("Peasant")); }
void URTSResourceWidget::TrainSwordman() { Train(TEXT("Swordman")); }
void URTSResourceWidget::TrainArcher() { Train(TEXT("Archer")); }
void URTSResourceWidget::CancelQueue()
{
	if (auto* PC = Cast<ARTSHumanController>(GetOwningPlayer()))
	{ ARTSBuilding* Building = PC->GetSelectedBuilding(); PC->SetEconomyMessage(Building && Building->GetProducer()->CancelLast() ? TEXT("已取消队尾订单并全额退款。") : TEXT("没有可取消的订单。")); }
}
void URTSResourceWidget::CancelPlacement() { if (auto* PC = Cast<ARTSHumanController>(GetOwningPlayer())) { PC->CancelBuildingPlacement(); } }

void URTSResourceWidget::SetGuard() { ApplyStance(ERTSCombatStance::Guard); }
void URTSResourceWidget::SetPatrol() { ApplyStance(ERTSCombatStance::Patrol); }
void URTSResourceWidget::SetAggressive() { ApplyStance(ERTSCombatStance::Aggressive); }
void URTSResourceWidget::ApplyStance(ERTSCombatStance Stance)
{
	const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this);
	if (Match && !Match->AllowsGameplay()) { return; }
	ARTSHumanController* PC = Cast<ARTSHumanController>(GetOwningPlayer());
	if (!PC || !PC->GetSelection()) { return; }
	for (ARTSUnit* Unit : PC->GetSelection()->GetSelectedUnits())
	{
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetTeam() == RTSTeam::Player) { Unit->SetCombatStance(Stance); }
	}
	RefreshUnitPanel();
}
void URTSResourceWidget::RefreshUnitPanel()
{
	if (!UnitPanel || !UnitStatusText) { return; }
	const ARTSHumanController* PC = Cast<ARTSHumanController>(GetOwningPlayer());
	if (!PC || !PC->GetSelection() || PC->GetSelectedBuilding()) { UnitPanel->SetVisibility(ESlateVisibility::Collapsed); return; }
	const TCHAR* Names[] = {TEXT("原地警戒"), TEXT("巡逻"), TEXT("侵略")};
	int32 Counts[3] = {0, 0, 0};
	TArray<FString> Lines;
	TSet<FName> SelectedKinds;
	for (const ARTSUnit* Unit : PC->GetSelection()->GetSelectedUnits())
	{
		if (!IsValid(Unit) || !Unit->IsAlive() || Unit->GetTeam() != RTSTeam::Player) { continue; }
		const int32 Stance = static_cast<int32>(Unit->GetCombatStance());
		if (Stance < 0 || Stance > 2) { continue; }
		++Counts[Stance];
		const URTSUnitData* Data = Unit->GetUnitData();
		if (Data && !Data->BalanceId.IsNone()) { SelectedKinds.Add(Data->BalanceId); }
		const FString Name = Data && !Data->BalanceId.IsNone() ? RTSDisplayNames::Entity(Data->BalanceId).ToString() : TEXT("单位");
		FString Line = FString::Printf(TEXT("%d. %s　生命 %d/%d\n%s｜%s%s"), Lines.Num() + 1, *Name,
			FMath::CeilToInt(Unit->GetHealth()->GetHealth()), FMath::CeilToInt(Unit->GetHealth()->GetMaxHealth()),
			*ARTSHUD::DescribeUnitActivity(Unit).Label, Names[Stance], Unit->IsRecoveringFromCongestion() ? TEXT("｜正在脱困") : TEXT(""));
		if (Data && Data->bWorker) { Line += FString::Printf(TEXT("｜携带%d%s"), Unit->GetGatherer()->GetCarriedAmount(),
			Unit->GetGatherer()->GetCarriedType() == ERTSResource::Wood ? TEXT("木材") : TEXT("食物")); }
		Lines.Add(Line);
	}
	UnitPanel->SetVisibility(Lines.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (Lines.IsEmpty()) { return; }
	for (int32 i = 0; i < UnitPortraitFrames.Num(); ++i)
	{
		if (USizeBox* Frame = UnitPortraitFrames[i].Get())
		{
			Frame->SetVisibility(SelectedKinds.Contains(UnitPortraitKinds[i]) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
	UnitStatusText->SetText(FText::FromString(FString::Join(Lines, TEXT("\n\n"))));
	StanceSummaryText->SetText(FText::FromString(FString::Printf(TEXT("已选%d｜警戒%d　巡逻%d　侵略%d（点击统一设置）"), Lines.Num(), Counts[0], Counts[1], Counts[2])));
	for (int32 i = 0; i < StanceButtons.Num(); ++i)
	{
		RTSUIStyle::Button(StanceButtons[i], Counts[i] == Lines.Num());
	}
}
#undef LOCTEXT_NAMESPACE
