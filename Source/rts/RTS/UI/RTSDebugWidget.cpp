#include "UI/RTSDebugWidget.h"
#include "UI/RTSResourceWidget.h"
#include "UI/RTSUIFont.h"
#include "Debug/RTSDebugSubsystem.h"
#include "Match/RTSMatchSubsystem.h"
#include "AI/RTSEnemyDirector.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Economy/RTSResourceComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "UI/RTSUIStyle.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"

namespace
{
	UTextBlock* DebugText(UWidgetTree* Tree, const FString& Text, int32 Size)
	{
		UTextBlock* Block = Tree->ConstructWidget<UTextBlock>();
		RTSUIStyle::Text(Block, Size); Block->SetText(FText::FromString(Text));
		Block->SetAutoWrapText(true);
		return Block;
	}
}
void URTSDebugWidget::AddButton(UVerticalBox* Parent, const TCHAR* Label, FName Function)
{
	UButton* Button = WidgetTree->ConstructWidget<URTSEconomyButton>();
	Button->SetContent(DebugText(WidgetTree, Label, 12));
	FScriptDelegate Delegate; Delegate.BindUFunction(this, Function); Button->OnClicked.Add(Delegate);
	Parent->AddChildToVerticalBox(Button)->SetPadding(FMargin(0, 2));
	if (Function != GET_FUNCTION_NAME_CHECKED(URTSDebugWidget, Close)) { MutationButtons.Add(Button); }
}
void URTSDebugWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Canvas;
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	Panel = WidgetTree->ConstructWidget<UBorder>(); RTSUIStyle::Panel(Panel, true);
	UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel); PanelSlot->SetPosition(FVector2D(20, 88)); PanelSlot->SetAutoSize(true);
	PanelSize = WidgetTree->ConstructWidget<USizeBox>(); PanelSize->SetWidthOverride(330); PanelSize->SetMaxDesiredHeight(420); Panel->SetContent(PanelSize);
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); PanelSize->SetContent(Scroll);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Column);
	UTextBlock* Title = DebugText(WidgetTree, TEXT("开发调试｜F3 显示/隐藏"), 16); RTSUIStyle::Text(Title, 16, RTSUIStyle::Gold()); Column->AddChild(Title);
	Column->AddChild(DebugText(WidgetTree, TEXT("修改会标记调试对局；不计入正式战绩。"), 11));
	StateText = DebugText(WidgetTree, TEXT("读取对局状态…"), 12); Column->AddChild(StateText);
	AddButton(Column, TEXT("己方增加500木材和500食物"), GET_FUNCTION_NAME_CHECKED(URTSDebugWidget, AddResources));
	AddButton(Column, TEXT("生成己方工人"), GET_FUNCTION_NAME_CHECKED(URTSDebugWidget, SpawnPeasant));
	AddButton(Column, TEXT("生成己方剑士"), GET_FUNCTION_NAME_CHECKED(URTSDebugWidget, SpawnSwordman));
	AddButton(Column, TEXT("生成己方弓箭手"), GET_FUNCTION_NAME_CHECKED(URTSDebugWidget, SpawnArcher));
	AddButton(Column, TEXT("时间：0.5倍"), GET_FUNCTION_NAME_CHECKED(URTSDebugWidget, Slow));
	AddButton(Column, TEXT("时间：1倍"), GET_FUNCTION_NAME_CHECKED(URTSDebugWidget, Normal));
	AddButton(Column, TEXT("时间：2倍"), GET_FUNCTION_NAME_CHECKED(URTSDebugWidget, Fast));
	MessageText = DebugText(WidgetTree, TEXT("仅查看不会修改对局。"), 11); Column->AddChild(MessageText);
	AddButton(Column, TEXT("关闭调试面板"), GET_FUNCTION_NAME_CHECKED(URTSDebugWidget, Close));
	Panel->SetVisibility(ESlateVisibility::Collapsed);
}
void URTSDebugWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	if (PanelSize) { PanelSize->SetMaxDesiredHeight(FMath::Clamp(static_cast<float>(Geometry.GetLocalSize().Y) - 340.f, 150.f, 500.f)); }
	const URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(this);
	const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this);
	const bool bShow = Debug && Match && Debug->IsPanelOpen()
		&& (Match->GetPhase() == ERTSMatchPhase::Sandbox || Match->GetPhase() == ERTSMatchPhase::Playing);
	Panel->SetVisibility(bShow ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (!bShow) { return; }
	RefreshTimer -= DeltaTime; if (RefreshTimer > 0) { return; } RefreshTimer = 0.2f;
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
	const URTSResourceComponent* Player = Balance ? Balance->GetResources(0) : nullptr;
	const URTSResourceComponent* Enemy = Balance ? Balance->GetResources(1) : nullptr;
	const ARTSEnemyDirector* Director = Match->GetEnemyDirector();
	StateText->SetText(FText::FromString(FString::Printf(TEXT("时间倍率 %.1f｜%s\n己方 木材%d 食物%d｜工人%d 军队%d\n敌方 木材%d 食物%d｜工人%d 军队%d\n电脑意图：%s"),
		Debug->GetSpeed(), Match->HasDebugBeenUsed() ? TEXT("已使用调试修改") : TEXT("未修改"),
		Player ? Player->GetAmount(ERTSResource::Wood) : 0, Player ? Player->GetAmount(ERTSResource::Food) : 0, Match->GetUnitCount(0,true), Match->GetUnitCount(0,false),
		Enemy ? Enemy->GetAmount(ERTSResource::Wood) : 0, Enemy ? Enemy->GetAmount(ERTSResource::Food) : 0, Match->GetUnitCount(1,true), Match->GetUnitCount(1,false),
		Director ? *Director->GetIntent() : TEXT("沙盒无电脑指挥器"))));
	for (UButton* Button : MutationButtons) { Button->SetIsEnabled(Match->AllowsGameplay() && !UGameplayStatics::IsGamePaused(GetWorld())); }
}
void URTSDebugWidget::AddResources()
{
	if (URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(this))
	{ FString Error; MessageText->SetText(FText::FromString(Debug->AddResources(Error) ? TEXT("已为己方增加资源，本局已标记调试。") : Error)); }
}
void URTSDebugWidget::Spawn(FName Id)
{
	if (URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(this))
	{ FString Error; MessageText->SetText(FText::FromString(Debug->SpawnUnit(Id, Error) ? TEXT("单位已在合法位置生成。") : Error)); }
}
void URTSDebugWidget::Speed(float Value)
{
	if (URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(this))
	{ FString Error; MessageText->SetText(FText::FromString(Debug->SetSpeed(Value, Error) ? TEXT("已设置时间倍率，结束或重开会恢复1倍。") : Error)); }
}
void URTSDebugWidget::SpawnPeasant() { Spawn(TEXT("Peasant")); }
void URTSDebugWidget::SpawnSwordman() { Spawn(TEXT("Swordman")); }
void URTSDebugWidget::SpawnArcher() { Spawn(TEXT("Archer")); }
void URTSDebugWidget::Slow() { Speed(0.5f); }
void URTSDebugWidget::Normal() { Speed(1.f); }
void URTSDebugWidget::Fast() { Speed(2.f); }
void URTSDebugWidget::Close() { if (URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(this); Debug && Debug->IsPanelOpen()) { Debug->TogglePanel(); } }
