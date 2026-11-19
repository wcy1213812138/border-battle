#include "UI/RTSMatchWidget.h"
#include "UI/RTSResourceWidget.h"
#include "UI/RTSUIFont.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/ScrollBox.h"
#include "GameFramework/GameUserSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Audio/RTSAudioSubsystem.h"
#include "UI/RTSUIStyle.h"
#include "Components/SizeBox.h"

namespace
{
	UTextBlock* Text(UWidgetTree* Tree, const FString& Value, int32 Size)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>();
		RTSUIStyle::Text(Label, Size);
		Label->SetText(FText::FromString(Value)); Label->SetAutoWrapText(true);
		return Label;
	}
	void SetQuality(int32 Level)
	{
		if (GEngine && GEngine->GetGameUserSettings())
		{
			UGameUserSettings* Settings = GEngine->GetGameUserSettings();
			Settings->SetOverallScalabilityLevel(Level); Settings->ApplyNonResolutionSettings(); Settings->SaveSettings();
		}
	}
}
UButton* URTSMatchWidget::Button(UVerticalBox* Parent, const FString& Value, FName Function)
{
	UButton* Result = WidgetTree->ConstructWidget<URTSEconomyButton>();
	Result->SetContent(Text(WidgetTree, Value, 15));
	RTSUIStyle::Button(Result, Function == GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Start) || Function == GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Resume), Function == GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Surrender));
	FScriptDelegate Delegate; Delegate.BindUFunction(this, Function); Result->OnClicked.Add(Delegate);
	Parent->AddChildToVerticalBox(Result)->SetPadding(FMargin(0, 4));
	return Result;
}
void URTSMatchWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget = Canvas;
	Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	UBorder* TopPanel = WidgetTree->ConstructWidget<UBorder>(); RTSUIStyle::Panel(TopPanel);
	UCanvasPanelSlot* TopSlot = Canvas->AddChildToCanvas(TopPanel);
	TopSlot->SetAnchors(FAnchors(1, 0)); TopSlot->SetAlignment(FVector2D(1, 0)); TopSlot->SetPosition(FVector2D(-20, 20)); TopSlot->SetAutoSize(true);
	TopSlot->SetZOrder(3);
	USizeBox* TopSize = WidgetTree->ConstructWidget<USizeBox>(); TopSize->SetWidthOverride(260); TopPanel->SetContent(TopSize);
	UVerticalBox* Top = WidgetTree->ConstructWidget<UVerticalBox>(); TopSize->SetContent(Top);
	TopText = Text(WidgetTree, TEXT("战术对局"), 12); TopText->SetWrapTextAt(250); Top->AddChild(TopText);
	PauseButton = Button(Top, TEXT("暂停 / 继续  [P]"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Pause));
	Button(Top, TEXT("主菜单"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, ToggleMenu));
	Scrim = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MenuScrim"));
	Scrim->SetBrushColor(FLinearColor(0.003f, 0.008f, 0.012f, 0.76f));
	UCanvasPanelSlot* ScrimSlot = Canvas->AddChildToCanvas(Scrim); ScrimSlot->SetAnchors(FAnchors(0, 0, 1, 1)); ScrimSlot->SetOffsets(FMargin(0)); ScrimSlot->SetZOrder(1);
	Modal = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("MatchModal"));
	RTSUIStyle::Panel(Modal, true); Modal->SetPadding(FMargin(28, 24));
	UCanvasPanelSlot* ModalSlot = Canvas->AddChildToCanvas(Modal);
	ModalSlot->SetZOrder(2); ModalSlot->SetAnchors(FAnchors(0.5f, 0.5f)); ModalSlot->SetAlignment(FVector2D(0.5f, 0.5f)); ModalSlot->SetSize(FVector2D(620, 700));
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(); Modal->SetContent(Scroll);
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(); Scroll->AddChild(Column);
	UTextBlock* Eyebrow = Text(WidgetTree, TEXT("迷你即时战略  /  单人对抗"), 11); RTSUIStyle::Text(Eyebrow, 11, RTSUIStyle::Muted()); Column->AddChild(Eyebrow);
	Header = Text(WidgetTree, TEXT("迷你即时战略"), 28); RTSUIStyle::Text(Header, 28, RTSUIStyle::Gold()); Column->AddChild(Header);
	Body = Text(WidgetTree, TEXT(""), 13); Body->SetAutoWrapText(true); Column->AddChildToVerticalBox(Body)->SetPadding(FMargin(0, 16));
	DifficultyText = Text(WidgetTree, TEXT(""), 13); DifficultyText->SetColorAndOpacity(RTSUIStyle::Accent()); Column->AddChild(DifficultyText);
	Actions = WidgetTree->ConstructWidget<UVerticalBox>(); Column->AddChild(Actions);
	UTextBlock* SettingsTitle = Text(WidgetTree, TEXT("设置｜音量（本次运行）"), 12); RTSUIStyle::Text(SettingsTitle, 12, RTSUIStyle::Gold()); Column->AddChildToVerticalBox(SettingsTitle)->SetPadding(FMargin(0, 16, 0, 8));
	Volume = WidgetTree->ConstructWidget<USlider>(); Volume->SetSliderBarColor(RTSUIStyle::Muted()); Volume->SetSliderHandleColor(RTSUIStyle::Gold());
	const URTSAudioSubsystem* Audio = URTSAudioSubsystem::Get(this);
	Volume->SetValue(Audio ? Audio->GetVolume() : 1.f); Volume->OnValueChanged.AddDynamic(this, &URTSMatchWidget::VolumeChanged); Column->AddChild(Volume);
	UTextBlock* QualityTitle = Text(WidgetTree, TEXT("画质（保存到本机设置）"), 12); RTSUIStyle::Text(QualityTitle, 12, RTSUIStyle::Muted()); Column->AddChildToVerticalBox(QualityTitle)->SetPadding(FMargin(0, 14, 0, 4));
	Button(Column, TEXT("低画质"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, QualityLow));
	Button(Column, TEXT("中画质"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, QualityMedium));
	Button(Column, TEXT("高画质"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, QualityHigh));
	BoundMatch = URTSMatchSubsystem::Get(this);
	if (BoundMatch.IsValid()) { BoundMatch->OnMatchChanged.AddUObject(this, &URTSMatchWidget::Refresh); }
	Refresh();
}
void URTSMatchWidget::NativeDestruct()
{
	if (BoundMatch.IsValid()) { BoundMatch->OnMatchChanged.RemoveAll(this); }
	Super::NativeDestruct();
}
void URTSMatchWidget::Refresh()
{
	URTSMatchSubsystem* Match = BoundMatch.Get(); if (!Match) { return; }
	const ERTSMatchPhase Phase = Match->GetPhase();
	const bool bModal = Phase != ERTSMatchPhase::Sandbox && Phase != ERTSMatchPhase::Playing;
	Modal->SetVisibility(bModal ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Scrim->SetVisibility(bModal ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	PauseButton->SetIsEnabled(Phase == ERTSMatchPhase::Playing || Phase == ERTSMatchPhase::Paused);
	TopText->SetVisibility(bModal ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	PauseButton->SetVisibility(bModal ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	const int32 Seconds = FMath::FloorToInt(Match->GetElapsedSeconds());
	if (Match->IsManagedMatch())
	{
		const ARTSBuilding* Hall = Match->GetMainBase(0); const ARTSBuilding* EnemyHall = Match->GetMainBase(1);
		TopText->SetText(FText::FromString(FString::Printf(TEXT("%s｜%02d:%02d\n工人 %d｜军队 %d\n己方主基地 %d｜敌方主基地 %d"),
			*URTSMatchSubsystem::DifficultyName(Match->GetDifficulty()).ToString(), Seconds / 60, Seconds % 60,
			Match->GetUnitCount(0, true), Match->GetUnitCount(0, false), Hall ? FMath::CeilToInt(Hall->GetHealth()->GetHealth()) : 0,
			EnemyHall ? FMath::CeilToInt(EnemyHall->GetHealth()->GetHealth()) : 0)));
	}
	else { TopText->SetText(FText::FromString(TEXT("沙盒对局｜点击主菜单开始对局"))); }
	if (Match->HasDebugBeenUsed()) { TopText->SetText(FText::FromString(TEXT("调试对局｜不计入正式战绩\n") + TopText->GetText().ToString())); }
	DifficultyText->SetVisibility(Phase == ERTSMatchPhase::MainMenu ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	DifficultyText->SetText(FText::FromString(TEXT("当前难度：") + URTSMatchSubsystem::DifficultyName(SelectedDifficulty).ToString()));
	if (!bBuilt || LastPhase != Phase)
	{
		Actions->ClearChildren(); DifficultyButtons.Reset(); LastPhase = Phase; bBuilt = true;
		if (Phase == ERTSMatchPhase::MainMenu)
		{
			Header->SetText(FText::FromString(TEXT("迷你即时战略")));
			DifficultyButtons.Add(Button(Actions, TEXT("简单｜较慢扩张与进攻"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Easy)));
			DifficultyButtons.Add(Button(Actions, TEXT("普通｜均衡经营与混编"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Normal)));
			DifficultyButtons.Add(Button(Actions, TEXT("困难｜更快决策与出兵"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Hard)));
			Button(Actions, TEXT("开始对局"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Start));
			Button(Actions, TEXT("关闭主菜单"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, ToggleMenu));
		}
		else if (Phase == ERTSMatchPhase::Paused)
		{
			Header->SetText(FText::FromString(TEXT("对局已暂停")));
			Button(Actions, TEXT("继续对局"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Resume));
			Button(Actions, TEXT("重新开始"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Restart));
			Button(Actions, TEXT("认输并结算"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Surrender));
			Button(Actions, TEXT("返回主菜单"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Menu));
		}
		else if (bModal)
		{
			Header->SetText(FText::FromString(Phase == ERTSMatchPhase::Victory ? TEXT("胜利！敌方主基地已摧毁") : Phase == ERTSMatchPhase::Draw ? TEXT("平局｜双方主基地均被摧毁") : TEXT("战败｜再试一次")));
			Button(Actions, TEXT("再来一局"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Restart));
			Button(Actions, TEXT("返回主菜单"), GET_FUNCTION_NAME_CHECKED(URTSMatchWidget, Menu));
		}
	}
	for (int32 i = 0; i < DifficultyButtons.Num(); ++i) { RTSUIStyle::Button(DifficultyButtons[i], i == static_cast<int32>(SelectedDifficulty)); }
	if (Phase == ERTSMatchPhase::Victory || Phase == ERTSMatchPhase::Defeat || Phase == ERTSMatchPhase::Draw)
	{
		const FRTSMatchStats& S = Match->GetStats(0);
		Body->SetText(FText::FromString((Match->HasDebugBeenUsed() ? FString(TEXT("本局使用过调试修改，不计入正式战绩。\n")) : FString())
			+ FString::Printf(TEXT("用时 %02d:%02d｜难度：%s\n采集木材 %d｜食物 %d\n训练单位 %d｜建成建筑 %d\n消灭敌军 %d｜损失单位 %d"), Seconds / 60, Seconds % 60,
			*URTSMatchSubsystem::DifficultyName(Match->GetDifficulty()).ToString(), S.WoodGathered, S.FoodGathered, S.UnitsTrained, S.BuildingsBuilt, S.UnitsKilled, S.UnitsLost)));
	}
	else
	{
		Body->SetText(FText::FromString(TEXT("目标：摧毁敌方开局主基地，保护己方主基地。\n双方初始资源和兵力相同，难度影响电脑经营与出兵节奏。\n左键/框选单位，右键资源采集或工地施工；选工人建兵营，选兵营训练军队；右键敌方建筑攻击。\n退格键停止，P键暂停。\n") + Match->GetMessage()));
	}
}
void URTSMatchWidget::NativeTick(const FGeometry& Geometry, float DeltaTime)
{
	Super::NativeTick(Geometry, DeltaTime);
	const FVector2D View = Geometry.GetLocalSize();
	if (Modal && View.X > 0)
	{ if (UCanvasPanelSlot* PanelSlot = Cast<UCanvasPanelSlot>(Modal->Slot)) { PanelSlot->SetSize(RTSUIStyle::ModalSize(View)); } }
}
void URTSMatchWidget::Menu() { if (BoundMatch.IsValid()) { BoundMatch->ShowMainMenu(); } }
void URTSMatchWidget::ToggleMenu() { if (BoundMatch.IsValid()) { BoundMatch->ToggleMainMenu(); } }
void URTSMatchWidget::Easy() { SelectedDifficulty = ERTSDifficulty::Easy; Refresh(); }
void URTSMatchWidget::Normal() { SelectedDifficulty = ERTSDifficulty::Normal; Refresh(); }
void URTSMatchWidget::Hard() { SelectedDifficulty = ERTSDifficulty::Hard; Refresh(); }
void URTSMatchWidget::Start()
{
	if (BoundMatch.IsValid()) { FString Error; if (!BoundMatch->StartMatch(SelectedDifficulty, Error)) { Body->SetText(FText::FromString(Error)); } }
}
void URTSMatchWidget::Pause() { if (BoundMatch.IsValid()) { BoundMatch->TogglePause(); } }
void URTSMatchWidget::Resume() { if (BoundMatch.IsValid()) { BoundMatch->SetMatchPaused(false); } }
void URTSMatchWidget::Restart()
{ if (BoundMatch.IsValid()) { SelectedDifficulty = BoundMatch->GetDifficulty(); Start(); } }
void URTSMatchWidget::Surrender() { if (BoundMatch.IsValid()) { BoundMatch->Surrender(); } }
void URTSMatchWidget::VolumeChanged(float Value)
{ if (URTSAudioSubsystem* Audio = URTSAudioSubsystem::Get(this)) { Audio->SetVolume(Value); } }
void URTSMatchWidget::QualityLow() { SetQuality(0); }
void URTSMatchWidget::QualityMedium() { SetQuality(1); }
void URTSMatchWidget::QualityHigh() { SetQuality(3); }
