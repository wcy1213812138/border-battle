#include "UI/RTSHUD.h"
#include "UI/RTSResourceWidget.h"
#include "Core/RTSHumanController.h"
#include "Units/RTSUnit.h"
#include "Orders/RTSMoveGroup.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "Economy/RTSResourceNode.h"
#include "Buildings/RTSBuilding.h"
#include "Core/RTSDisplayNames.h"
#include "Engine/Font.h"
#include "Misc/Paths.h"
#include "UI/RTSMatchWidget.h"
#include "UI/RTSDebugWidget.h"
#include "Selection/RTSSelectionComponent.h"
#include "Economy/RTSGathererComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Data/RTSUnitData.h"
#include "Match/RTSMatchSubsystem.h"
#include "Units/RTSPeasantShowcase.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSWorldLabel.h"
#include "UI/RTSUnitNameplateStyle.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
	FString ActivityTargetName(const AActor* Actor)
	{
		if (const ARTSBuilding* Building = Cast<ARTSBuilding>(Actor))
		{
			return RTSDisplayNames::Entity(Building->GetBuildingId()).ToString();
		}
		if (const ARTSUnit* Unit = Cast<ARTSUnit>(Actor))
		{
			const URTSUnitData* Data = Unit->GetUnitData();
			if (Data && !Data->BalanceId.IsNone()) { return RTSDisplayNames::Entity(Data->BalanceId).ToString(); }
			if (Data && !Data->DisplayName.IsEmpty()) { return Data->DisplayName.ToString(); }
		}
		if (const ARTSResourceNode* Node = Cast<ARTSResourceNode>(Actor))
		{
			return Node->GetResourceType() == ERTSResource::Wood ? TEXT("木材") : TEXT("食物");
		}
		return TEXT("目标");
	}

	FVector ActivityGroundLocation(const AActor* Actor)
	{
		FVector Location = Actor->GetActorLocation();
		if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Actor->GetRootComponent()))
		{
			Location.Z -= Capsule->GetScaledCapsuleHalfHeight();
		}
		return Location + FVector(0, 0, 6);
	}
}

FRTSUnitActivity ARTSHUD::DescribeUnitActivity(const ARTSUnit* Unit)
{
	FRTSUnitActivity Activity;
	if (!IsValid(Unit) || !Unit->IsAlive()) { return Activity; }
	const FRTSOrder& Order = Unit->GetCurrentOrder();
	const AActor* Target = Order.TargetActor.Get();
	if (Target)
	{
		const URTSHealthComponent* Health = Target->FindComponentByClass<URTSHealthComponent>();
		if (Health && Health->IsDead()) { Target = nullptr; }
	}
	if (Order.Type == ERTSOrderType::Move || Order.Type == ERTSOrderType::AttackMove)
	{
		Activity.Summary = Activity.Label = TEXT("移动");
		Activity.Color = FLinearColor(0.3f, 0.8f, 1.f);
		Activity.Target = Order.TargetLocation + FVector(0, 0, 6);
		Activity.bHasTarget = true;
	}
	else if (Order.Type == ERTSOrderType::Attack && Target)
	{
		const URTSAttackComponent* Attack = Unit->GetAttack();
		const bool bAttacking = Attack && (Attack->IsSwinging()
			|| (Attack->IsInRange(Target) && Unit->GetVelocity().SizeSquared2D() < 100.f));
		Activity.Summary = bAttacking ? TEXT("正在攻击") : TEXT("追击");
		Activity.Label = Activity.Summary + TEXT("：") + ActivityTargetName(Target);
		Activity.Color = FLinearColor(1.f, 0.35f, 0.25f);
		Activity.Target = ActivityGroundLocation(Target);
		Activity.bHasTarget = true;
	}
	else if (Order.Type == ERTSOrderType::Gather || Order.Type == ERTSOrderType::Build
		|| Order.Type == ERTSOrderType::ReturnResources)
	{
		const URTSUnitData* Data = Unit->GetUnitData();
		const URTSGathererComponent* Gatherer = Unit->GetGatherer();
		// Every unit owns a gatherer, but only worker work orders may use its status.
		if (Data && Data->bWorker && Gatherer)
		{
			Activity.Summary = Gatherer->GetStatus();
			if (Activity.Summary.IsEmpty() || Activity.Summary == TEXT("空闲"))
			{
				Activity.Summary = Order.Type == ERTSOrderType::Build ? TEXT("前往工地")
					: Order.Type == ERTSOrderType::Gather ? TEXT("前往资源点") : TEXT("运回资源");
			}
			Activity.Color = FLinearColor(1.f, 0.8f, 0.25f);
			// Gather orders retain the resource as TargetActor during automatic return trips.
			// Do not draw a misleading line back to the resource while carrying cargo home.
			if (Target && Activity.Summary != TEXT("运回资源")
				&& Activity.Summary != TEXT("没有已建成的交付点"))
			{
				Activity.Target = ActivityGroundLocation(Target);
				Activity.bHasTarget = true;
			}
			else if (const AActor* ReturnTarget = Gatherer->GetReturnGuideTarget())
			{
				Activity.Target = ActivityGroundLocation(ReturnTarget);
				Activity.bHasTarget = true;
			}
			else if (Order.Type == ERTSOrderType::ReturnResources)
			{
				const ARTSBuilding* Drop = Cast<ARTSBuilding>(Target);
				if (Drop && Drop->IsDropOff() && Drop->GetTeam() == Unit->GetTeam())
				{
					Activity.Target = ActivityGroundLocation(Drop);
					Activity.bHasTarget = true;
				}
			}
			Activity.Label = Activity.Summary;
			if (Target && Activity.bHasTarget) { Activity.Label += TEXT("：") + ActivityTargetName(Target); }
		}
	}
	return Activity;
}

bool ARTSHUD::ResolveMoveGuideTarget(const FRTSOrder& Order, const FRTSMoveGroup* Group,
	bool bMultipleSelected, FVector& OutTarget)
{
	// Only Move orders own the blue guide line; Stop/Attack never reuse an old destination.
	if (Order.Type == ERTSOrderType::AttackMove) { OutTarget = Order.TargetLocation; return true; }
	if (Order.Type != ERTSOrderType::Move)
	{
		return false;
	}
	// Display-only read of shared order metadata: a multi-selection converges on the
	// group's requested destination, while a solo selection keeps the live slot/detour.
	OutTarget = (bMultipleSelected && Group) ? (Group->bHasRequestedGoal ? Group->RequestedGoal : Group->Center) : Order.TargetLocation;
	return true;
}

ARTSHUD::ARTSHUD()
{
	ResourceWidgetClass = URTSResourceWidget::StaticClass();
}

void ARTSHUD::BeginPlay()
{
	Super::BeginPlay();
	CjkFontFile = FPaths::EngineContentDir() / TEXT("Slate/Fonts/DroidSansFallback.ttf");
	ChineseFont = NewObject<UFont>(this);
	ChineseFont->FontCacheType = EFontCacheType::Runtime;
	ChineseFont->LegacyFontSize = 14;
	ChineseFont->GetMutableInternalCompositeFont() = FCompositeFont(FName("Regular"), CjkFontFile, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);

	if (APlayerController* PC = GetOwningPlayerController(); PC && PC->IsLocalController() && ResourceWidgetClass)
	{
		ResourceWidget = CreateWidget<URTSResourceWidget>(PC, ResourceWidgetClass);
		if (ResourceWidget)
		{
			ResourceWidget->AddToViewport();
		}
		if (!Cast<ARTSCampaignGameMode>(GetWorld()->GetAuthGameMode()))
		{
			MatchWidget = CreateWidget<URTSMatchWidget>(PC, URTSMatchWidget::StaticClass());
			if (MatchWidget) { MatchWidget->AddToViewport(10); }
		}
#if WITH_EDITOR || UE_BUILD_DEBUG
		DebugWidget = CreateWidget<URTSDebugWidget>(PC, URTSDebugWidget::StaticClass());
		if (DebugWidget) { DebugWidget->AddToViewport(5); }
#endif
	}
}

bool ARTSHUD::IsCjkFontRuntime() const
{
	// TextRender can only use offline UFonts; canvas text with this runtime font is the CJK-capable path.
	return ChineseFont && ChineseFont->FontCacheType == EFontCacheType::Runtime;
}

void ARTSHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	DrawSelectedBuilding();
	DrawSelectedUnitActivities();
	DrawHealthBars();
	DrawSelectionBox();
	const auto* Campaign = ARTSCampaign::Get(this);
	const APlayerController* PC = GetOwningPlayerController();
	const APawn* CameraPawn = PC ? PC->GetPawn() : nullptr;
	const USpringArmComponent* CameraArm = CameraPawn ? CameraPawn->FindComponentByClass<USpringArmComponent>() : nullptr;
	const float ZoomDistance = CameraArm ? CameraArm->TargetArmLength : 2000.f;
	TArray<ARTSPeasantShowcase*> NameplateUnits;
	for (TActorIterator<ARTSPeasantShowcase> It(GetWorld()); It; ++It)
	{
		if (It->IsAlive() && !It->IsHidden()) { NameplateUnits.Add(*It); }
	}
	// Selected labels reserve their space first; stable order prevents crowd labels flickering.
	NameplateUnits.Sort([](const ARTSPeasantShowcase& A, const ARTSPeasantShowcase& B)
	{
		if (A.IsSelected() != B.IsSelected()) { return A.IsSelected(); }
		return A.GetUniqueID() < B.GetUniqueID();
	});
	TArray<FBox2D> NameplateBounds;
	for (const ARTSPeasantShowcase* Unit : NameplateUnits)
	{
		const FVector Anchor = Unit->GetActorLocation() + FVector(0, 0, 165);
		if (PC && PC->PlayerCameraManager && FVector::DotProduct(Anchor - PC->PlayerCameraManager->GetCameraLocation(), PC->PlayerCameraManager->GetCameraRotation().Vector()) <= 0) { continue; }
		const FVector Screen = Project(Anchor, false);
		if (Screen.Z <= 0) { continue; }
		const auto Style = RTSUnitNameplateStyle::Resolve(ZoomDistance, Unit->IsSelected());
		const TCHAR* Names[] = {TEXT("I 普通"), TEXT("II 熟练"), TEXT("III 精英"), TEXT("IV 领主"), TEXT("V 超凡")};
		const FString KindName = RTSDisplayNames::Entity(Unit->GetPreviewKind()).ToString();
		const int32 TierIndex = FMath::Clamp(Unit->GetPreviewTier() - 1, 0, 4);
		const FString Label = Style.bCompact
			? (Campaign && Unit->GetTeam() == 0 ? FString::Printf(TEXT("%s Lv%d"), *KindName, Campaign->GetLevel(Unit->GetPreviewKind())) : KindName)
			: (Campaign && Unit->GetTeam() == 0 ? FString::Printf(TEXT("%s Lv%d · %s"), *KindName, Campaign->GetLevel(Unit->GetPreviewKind()), Names[TierIndex]) : FString(Names[TierIndex]) + KindName);
		float W = 0, H = 0;
		GetTextSize(Label, W, H, ChineseFont);
		W *= Style.Scale; H *= Style.Scale;
		const float PadX = 5.f * Style.Scale, PadY = 2.f * Style.Scale;
		const FBox2D Bounds(FVector2D(Screen.X - W * .5f - PadX, Screen.Y - PadY), FVector2D(Screen.X + W * .5f + PadX, Screen.Y + H + PadY));
		if (Bounds.Max.X < 0 || Bounds.Min.X > Canvas->SizeX || Bounds.Max.Y < 0 || Bounds.Min.Y > Canvas->SizeY) { continue; }
		const FVector2D CanvasSize(Canvas->SizeX, Canvas->SizeY);
		if (CampaignStockExclusion.bIsValid && CampaignStockExclusion.Intersect(FBox2D(Bounds.Min / CanvasSize, Bounds.Max / CanvasSize))) { continue; }
		if (Style.bSuppressOverlaps && NameplateBounds.ContainsByPredicate([&](const FBox2D& Other) { return Other.Intersect(Bounds); })) { continue; }
		NameplateBounds.Add(Bounds);
		DrawRect(FLinearColor(.015f, .02f, .03f, Style.BackgroundAlpha), Bounds.Min.X, Bounds.Min.Y, Bounds.GetSize().X, Bounds.GetSize().Y);
		DrawText(Label, FLinearColor(1, .8f, .35f), Screen.X - W * .5f, Screen.Y, ChineseFont, Style.Scale);
	}
	// Resource names, stock and ownership are shown only in the selected-resource panel.
	for (TActorIterator<ARTSWorldLabel> It(GetWorld()); It; ++It)
	{
		const FString Label = It->GetLabel();
		if (Label.IsEmpty()) { continue; }
		const FVector LabelAnchor = It->GetActorLocation() + FVector(0, 0, It->GetLabelHeight());
		float LabelScale = 1.f;
		const float Fade = WorldLabelFade(LabelAnchor, LabelScale);
		if (Fade <= 0.f) { continue; }
		const FVector Screen = Project(LabelAnchor, false);
		if (Screen.Z <= 0) { continue; }
		FLinearColor Faded = It->GetLabelColor(); Faded.A *= Fade;
		DrawWorldLabel(Screen, Label, Faded, 1.1f * LabelScale);
	}
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive()) { continue; }
		const FVector LabelAnchor = It->GetActorLocation() + FVector(0, 0, 280);
		float LabelScale = 1.f;
		const float Fade = WorldLabelFade(LabelAnchor, LabelScale);
		const FVector Screen = Fade > 0.f ? Project(LabelAnchor, false) : FVector(0, 0, -1);
		// Building names live in the selected-object panel, not opaque world-space labels.
		if (!It->IsComplete() && It->GetTeam() == RTSTeam::Player && Screen.Z > 0
			&& Screen.X >= 0 && Screen.X < Canvas->SizeX && Screen.Y >= 0 && Screen.Y < Canvas->SizeY)
		{
			const float BarX = Screen.X - 60, BarY = Screen.Y + 22;
			const float Progress = It->GetBuildProgress();
			DrawRect(FLinearColor(0.02f, 0.025f, 0.03f, 0.9f), BarX - 2, BarY - 2, 124, 12);
			DrawRect(FLinearColor(0.15f, 0.8f, 0.6f), BarX, BarY, 120 * Progress, 8);
			const FString Status = FString::Printf(TEXT("建造 %d%%｜%s"), FMath::FloorToInt(Progress * 100), *It->GetConstructionStatus());
			float TextWidth = 0, TextHeight = 0;
			GetTextSize(Status, TextWidth, TextHeight, ChineseFont);
			DrawRect(FLinearColor(0, 0, 0, 0.8f), Screen.X - TextWidth * 0.5f - 3, BarY + 12, TextWidth + 6, TextHeight + 4);
			DrawText(Status, FLinearColor::White, Screen.X - TextWidth * 0.5f, BarY + 14, ChineseFont);
		}
	}
}

float ARTSHUD::WorldLabelFade(const FVector& WorldPoint, float& OutScale) const
{
	const APlayerController* PC = GetOwningPlayerController();
	const FVector CameraLocation = PC && PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraLocation()
		: (PC && PC->GetPawn() ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector);
	const float Distance = FMath::Max(0.f, FVector::Dist(CameraLocation, WorldPoint));
	// Near stays crisp; mid shrinks slightly; far fades to a legible floor; beyond the cutoff it is dropped.
	OutScale = Distance > 4000.f ? 0.82f : 1.f;
	if (Distance > 7000.f) { return 0.f; }
	return FMath::Clamp(1.f - FMath::Max(0.f, Distance - 4500.f) / 2500.f, 0.42f, 1.f);
}

void ARTSHUD::DrawWorldLabel(const FVector& Screen, const FString& Text, const FLinearColor& Color, float Scale)
{
	if (!Canvas || !ChineseFont || Text.IsEmpty()) { return; }
	if (Screen.X < -200.f || Screen.X > Canvas->SizeX + 200.f || Screen.Y < -100.f || Screen.Y > Canvas->SizeY + 100.f) { return; }
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	if (Lines.Num() == 0) { return; }
	float MaxWidth = 0.f, TotalHeight = 0.f;
	for (const FString& Line : Lines)
	{
		float W = 0.f, H = 0.f;
		GetTextSize(Line, W, H, ChineseFont);
		MaxWidth = FMath::Max(MaxWidth, W * Scale);
		TotalHeight += H * Scale + 2.f;
	}
	TotalHeight -= 2.f;
	const float Left = Screen.X - MaxWidth * 0.5f;
 if(CampaignStockExclusion.bIsValid && Canvas->SizeX>0 && Canvas->SizeY>0)
 {
  const FVector2D CanvasSize(Canvas->SizeX,Canvas->SizeY);
  const FBox2D LabelRect(FVector2D(Left-7.f,Screen.Y-4.f)/CanvasSize,FVector2D(Left+MaxWidth+7.f,Screen.Y+TotalHeight+4.f)/CanvasSize);
  if(CampaignStockExclusion.Intersect(LabelRect))return;
 }
	DrawRect(FLinearColor(0.01f, 0.015f, 0.02f, 0.72f), Left - 7.f, Screen.Y - 4.f, MaxWidth + 14.f, TotalHeight + 8.f);
	float Cursor = Screen.Y;
	for (const FString& Line : Lines)
	{
		float W = 0.f, H = 0.f;
		GetTextSize(Line, W, H, ChineseFont);
		DrawText(Line, Color, Screen.X - W * Scale * 0.5f, Cursor, ChineseFont, Scale);
		Cursor += H * Scale + 2.f;
	}
}

void ARTSHUD::DrawSelectionBox()
{
	const ARTSHumanController* PC = Cast<ARTSHumanController>(GetOwningPlayerController());
	if (!PC || !PC->IsBoxSelecting())
	{
		return;
	}
	const FVector2D A = PC->GetBoxStart();
	const FVector2D B = PC->GetBoxEnd();
	const FVector2D Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y));
	const FVector2D Max(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
	const FVector2D Size = Max - Min;

	const FLinearColor Fill(0.2f, 0.9f, 0.3f, 0.15f);
	const FLinearColor Edge(0.2f, 1.0f, 0.3f, 0.9f);
	DrawRect(Fill, Min.X, Min.Y, Size.X, Size.Y);
	DrawLine(Min.X, Min.Y, Max.X, Min.Y, Edge, 1.5f);
	DrawLine(Max.X, Min.Y, Max.X, Max.Y, Edge, 1.5f);
	DrawLine(Max.X, Max.Y, Min.X, Max.Y, Edge, 1.5f);
	DrawLine(Min.X, Max.Y, Min.X, Min.Y, Edge, 1.5f);
}

void ARTSHUD::DrawSelectedBuilding()
{
	const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this);
	if (Match && !Match->AllowsGameplay()) { return; }
	const ARTSHumanController* PC = Cast<ARTSHumanController>(GetOwningPlayerController());
	const ARTSBuilding* Building = PC ? PC->GetSelectedBuilding() : nullptr;
	if (!Building) { return; }
	const float Half = Building->GetFootprint() * 0.5f + 14.f;
	const FVector Center = Building->GetActorLocation() + FVector(0, 0, 8);
	const FVector Offsets[] = { FVector(-Half, -Half, 0), FVector(Half, -Half, 0),
		FVector(Half, Half, 0), FVector(-Half, Half, 0) };
	FVector Points[4];
	for (int32 i = 0; i < 4; ++i)
	{
		Points[i] = Project(Center + Building->GetActorRotation().RotateVector(Offsets[i]), false);
		if (Points[i].Z <= 0.f) { return; }
	}
	const FLinearColor Teal(0.1f, 1.f, 0.75f);
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector& A = Points[i];
		const FVector& B = Points[(i + 1) % 4];
		DrawLine(A.X, A.Y, B.X, B.Y, FLinearColor::Black, 6.f);
		DrawLine(A.X, A.Y, B.X, B.Y, Teal, 3.f);
		DrawRect(Teal, A.X - 4.f, A.Y - 4.f, 8.f, 8.f);
	}
	const FVector Label = Project(Center, false);
	if (Label.Z > 0.f)
	{
		const FString Text = Building->IsComplete() ? TEXT("已选中建筑") : TEXT("已选中工地");
		float W = 0.f, H = 0.f;
		GetTextSize(Text, W, H, ChineseFont);
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.8f), Label.X - W * 0.5f - 4, Label.Y - 2, W + 8, H + 4);
		DrawText(Text, Teal, Label.X - W * 0.5f, Label.Y, ChineseFont);
	}
}

void ARTSHUD::DrawSelectedUnitActivities()
{
	const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this);
	if (Match && !Match->AllowsGameplay()) { return; }
	const ARTSHumanController* PC = Cast<ARTSHumanController>(GetOwningPlayerController());
	if (!PC || !PC->GetSelection()) { return; }
	const TArray<ARTSUnit*> SelectedUnits = PC->GetSelection()->GetSelectedUnits();
	const bool bMultipleSelected = SelectedUnits.Num() > 1;
	for (const ARTSUnit* Unit : SelectedUnits)
	{
		if (!IsValid(Unit) || !Unit->IsAlive()) { continue; }
		const FRTSUnitActivity Activity = DescribeUnitActivity(Unit);
		const UCapsuleComponent* Capsule = Unit->GetCapsuleComponent();
		const FVector Anchor = Capsule->GetComponentLocation()
			+ FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + HealthBarHeightOffset);
		const FVector Screen = Project(Anchor, false);
		if (Screen.Z <= 0.f || Screen.X < 0 || Screen.X > Canvas->SizeX
			|| Screen.Y < 0 || Screen.Y > Canvas->SizeY) { continue; }
		float W = 0.f, H = 0.f;
		GetTextSize(Activity.Label, W, H, ChineseFont);
		const float X = Screen.X - W * 0.5f;
		const float Y = Screen.Y - HealthBarSize.Y - H - 6.f;
		DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.8f), X - 4, Y - 2, W + 8, H + 4);
		DrawText(Activity.Label, Activity.Color, X, Y, ChineseFont);
		if (!Activity.bHasTarget) { continue; }
		// Group moves show the shared requested destination to a multi-selection; a solo
		// selection keeps the unit's live slot/detour target.
		FVector GuideTarget;
		const bool bMoveGuide = ResolveMoveGuideTarget(Unit->GetCurrentOrder(), Unit->GetMoveGroup(),
			bMultipleSelected, GuideTarget);
		const FVector From = Project(ActivityGroundLocation(Unit), false);
		const FVector To = Project(bMoveGuide ? GuideTarget + FVector(0, 0, 6) : Activity.Target, false);
		if (From.Z <= 0.f || To.Z <= 0.f) { continue; }
		// Straight projected order indicators only: no actors, traces, collision or path changes.
		DrawLine(From.X, From.Y, To.X, To.Y, FLinearColor::Black, 3.f);
		DrawLine(From.X, From.Y, To.X, To.Y, Activity.Color, 1.5f);
		DrawLine(To.X - 7, To.Y, To.X, To.Y - 5, Activity.Color, 2.f);
		DrawLine(To.X, To.Y - 5, To.X + 7, To.Y, Activity.Color, 2.f);
		DrawLine(To.X + 7, To.Y, To.X, To.Y + 5, Activity.Color, 2.f);
		DrawLine(To.X, To.Y + 5, To.X - 7, To.Y, Activity.Color, 2.f);
	}
}

void ARTSHUD::DrawHealthBars()
{
	// Every damageable actor (units, dummies, later buildings) gets a screen-space bar,
	// so bars always face the camera regardless of camera yaw/pitch.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		const AActor* Actor = *It;
		const URTSHealthComponent* Health = Actor->FindComponentByClass<URTSHealthComponent>();
		if (!Health || Health->IsDead())
		{
			continue;
		}

		const ARTSUnit* Unit = Cast<ARTSUnit>(Actor);
		// Units: only selected or wounded, to keep the screen clean. Other targets: always.
		if (Unit && !Unit->IsSelected() && Health->GetHealthPercent() >= 1.0f)
		{
			continue;
		}

		float TopZ;
		if (const UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(Actor->GetRootComponent()))
		{
			TopZ = Capsule->GetComponentLocation().Z + Capsule->GetScaledCapsuleHalfHeight();
		}
		else
		{
			FVector Origin, Extent;
			Actor->GetActorBounds(true, Origin, Extent);
			TopZ = Origin.Z + Extent.Z;
		}
		const FVector Anchor(Actor->GetActorLocation().X, Actor->GetActorLocation().Y, TopZ + HealthBarHeightOffset);
		DrawHealthBar(Anchor, Health, /*bShowNumbers=*/ Unit == nullptr);
	}
}

void ARTSHUD::DrawHealthBar(const FVector& WorldPos, const URTSHealthComponent* Health, bool bShowNumbers)
{
	const FVector Screen = Project(WorldPos, false);
	if (Screen.Z <= 0.f)
	{
		return; // behind camera
	}
	const float Pct = FMath::Clamp(Health->GetHealthPercent(), 0.f, 1.f);
	const float X = Screen.X - HealthBarSize.X * 0.5f;
	const float Y = Screen.Y - HealthBarSize.Y;
	const FLinearColor Fill = FMath::Lerp(FLinearColor(0.9f, 0.1f, 0.1f), FLinearColor(0.1f, 0.9f, 0.2f), Pct);

	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), X - 1.f, Y - 1.f, HealthBarSize.X + 2.f, HealthBarSize.Y + 2.f);
	DrawRect(Fill, X, Y, HealthBarSize.X * Pct, HealthBarSize.Y);

	if (bShowNumbers && GEngine)
	{
		UFont* Font = ChineseFont;
		const FString Label = FString::Printf(TEXT("生命 %d/%d"),
			FMath::CeilToInt(Health->GetHealth()), FMath::CeilToInt(Health->GetMaxHealth()));
		float TextW = 0.f, TextH = 0.f;
		GetTextSize(Label, TextW, TextH, Font);
		DrawText(Label, FLinearColor::White, Screen.X - TextW * 0.5f, Y - TextH - 2.f, Font);
	}
}
