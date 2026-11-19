#include "Core/RTSHumanController.h"
#include "Core/RTSTypes.h"
#include "Selection/RTSSelectionComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "Units/RTSUnit.h"
#include "Combat/RTSHealthComponent.h"
#include "UI/RTSCommandMarker.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "GenericTeamAgentInterface.h"
#include "Engine/LocalPlayer.h"

#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "EngineUtils.h"
#include "Economy/RTSResourceComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Campaign/RTSCampaignMapSpace.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "DrawDebugHelpers.h"
#include "Match/RTSMatchSubsystem.h"
#include "Debug/RTSDebugSubsystem.h"
#include "Audio/RTSAudioSubsystem.h"
#include "Campaign/RTSCampaign.h"
#include "Kismet/GameplayStatics.h"


bool ARTSHumanController::IsCampaignFootprintInsideMap(const FVector& Ground, float Footprint)
{
	if (!FMath::IsFinite(Footprint) || Footprint < 0.f || !RTSCampaignMapSpace::ContainsXY(Ground))
	{
		return false;
	}
	const double Half = Footprint * 0.5;
	const FVector Corners[] =
	{
		Ground + FVector(Half, Half, 0), Ground + FVector(Half, -Half, 0),
		Ground + FVector(-Half, Half, 0), Ground + FVector(-Half, -Half, 0)
	};
	for (const FVector& Corner : Corners)
	{
		if (!RTSCampaignMapSpace::ContainsXY(Corner)) { return false; }
	}
	return true;
}

bool ARTSHumanController::CanStartCampaignBarracksBuild(int32 GuideStep, int32 AliveFriendlyBuildings,
	int32 Wood, int32 Food, const FRTSBalanceRow& Row, FString& OutError)
{
	OutError.Reset();
	if (GuideStep < 1)
	{
		OutError = TEXT("请先确认开始引导，再选择工人建造。");
		return false;
	}
	if (AliveFriendlyBuildings >= CampaignFriendlyBuildingLimit)
	{
		OutError = FString::Printf(TEXT("我方建筑已达%d座上限，无法继续建造。"), CampaignFriendlyBuildingLimit);
		return false;
	}
	if (Row.WoodCost < 0 || Row.FoodCost < 0 || Wood < Row.WoodCost || Food < Row.FoodCost)
	{
		OutError = FString::Printf(TEXT("资源不足：该建筑需要木材%d、食物%d。"), Row.WoodCost, Row.FoodCost);
		return false;
	}
	return true;
}
namespace
{
	void PlayerCue(const UObject* Context, ERTSAudioCue Cue)
	{ if (URTSAudioSubsystem* Audio = URTSAudioSubsystem::Get(Context)) { Audio->PlayCue(Cue); } }
}
ARTSHumanController::ARTSHumanController()
{
	Selection = CreateDefaultSubobject<URTSSelectionComponent>(TEXT("Selection"));
	bShowMouseCursor = true;
	bEnableClickEvents = false;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ARTSHumanController::BeginPlay()
{
	Super::BeginPlay();

	FInputModeGameAndUI InputMode;
	// Keep the desktop cursor free in windowed PIE, including access to editor tools.
	// GameAndUI still captures during a mouse drag without hiding the cursor.
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(MappingContext, 1);
	}
}

void ARTSHumanController::SetupInputComponent()
{
	Super::SetupInputComponent();
	FInputKeyBinding PauseBinding(FInputChord(EKeys::P), IE_Pressed);
	PauseBinding.bExecuteWhenPaused = true;
	PauseBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ARTSHumanController::OnPausePressed);
	InputComponent->KeyBindings.Add(PauseBinding);
#if WITH_EDITOR || UE_BUILD_DEBUG
	FInputKeyBinding DebugBinding(FInputChord(EKeys::F3), IE_Pressed);
	DebugBinding.bExecuteWhenPaused = true;
	DebugBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ARTSHumanController::OnDebugPressed);
	InputComponent->KeyBindings.Add(DebugBinding);
#endif
	{
		const FKey Digits[9] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		for (int32 Slot = 0; Slot < 9; ++Slot)
		{
			FInputKeyBinding Binding(FInputChord(Digits[Slot]), IE_Pressed);
			Binding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ARTSHumanController::OnGroupSlot, Slot);
			InputComponent->KeyBindings.Add(Binding);
		}
		FInputKeyBinding WorkerBinding(FInputChord(EKeys::F1, false, false, true, false), IE_Pressed);
		WorkerBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ARTSHumanController::OnWorkerKey);
		InputComponent->KeyBindings.Add(WorkerBinding);
		FInputKeyBinding ArmyBinding(FInputChord(EKeys::F2, false, false, true, false), IE_Pressed);
		ArmyBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ARTSHumanController::OnSelectAllKey);
		InputComponent->KeyBindings.Add(ArmyBinding);
		FInputKeyBinding ProductionBinding(FInputChord(EKeys::B), IE_Pressed);
		ProductionBinding.KeyDelegate.GetDelegateForManualSet().BindUObject(this, &ARTSHumanController::OnProductionKey);
		InputComponent->KeyBindings.Add(ProductionBinding);
	}

	// Input objects are built at runtime so the project needs no input assets for M0.
	SelectAction = NewObject<UInputAction>(this, TEXT("IA_RTSSelect"));
	SelectAction->ValueType = EInputActionValueType::Boolean;
	CommandAction = NewObject<UInputAction>(this, TEXT("IA_RTSCommand"));
	CommandAction->ValueType = EInputActionValueType::Boolean;
	CancelAction = NewObject<UInputAction>(this, TEXT("IA_RTSCancel"));
	CancelAction->ValueType = EInputActionValueType::Boolean;

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_RTSController"));
	MappingContext->MapKey(SelectAction, EKeys::LeftMouseButton);
	MappingContext->MapKey(CommandAction, EKeys::RightMouseButton);
	MappingContext->MapKey(CancelAction, EKeys::BackSpace);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(SelectAction, ETriggerEvent::Started, this, &ARTSHumanController::OnSelectPressed);
		EIC->BindAction(SelectAction, ETriggerEvent::Completed, this, &ARTSHumanController::OnSelectReleased);
		EIC->BindAction(CommandAction, ETriggerEvent::Started, this, &ARTSHumanController::OnCommandPressed);
		EIC->BindAction(CancelAction, ETriggerEvent::Started, this, &ARTSHumanController::OnCancelPressed);
	}
	else
	{
		UE_LOG(LogRTS, Error, TEXT("ARTSHumanController requires EnhancedInputComponent."));
	}
}

bool ARTSHumanController::GetMousePos(FVector2D& Out) const
{
	float X = 0.f, Y = 0.f;
	if (GetMousePosition(X, Y))
	{
		Out = FVector2D(X, Y);
		return true;
	}
	return false;
}

bool ARTSHumanController::IsShiftDown() const
{
	return IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);
}

void ARTSHumanController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (!CanIssueGameplayInput()) { bLeftDown = bDragging = false; return; }
	if (IsPlacingBuilding())
	{
		FHitResult Hit;
		URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
		const FRTSBalanceRow* Row = Balance ? Balance->Find(PlacementId) : nullptr;
		if (Row && GetHitResultUnderCursor(ECC_Visibility, false, Hit))
		{
			FString Error;
			const bool bGenericValid = Balance->CanPlace(PlacementId, Hit.ImpactPoint, Error);
			const bool bValid = bGenericValid && (!ARTSCampaign::Get(this)
				|| IsCampaignFootprintInsideMap(Hit.ImpactPoint, Row->Footprint));
			DrawDebugBox(GetWorld(), Hit.ImpactPoint + FVector(0, 0, 30), FVector(Row->Footprint * 0.5, Row->Footprint * 0.5, 30), bValid ? FColor::Green : FColor::Red, false, -1, 0, 4);
		}
	}

	if (bLeftDown)
	{
		FVector2D Mouse;
		if (GetMousePos(Mouse))
		{
			BoxEnd = Mouse;
			if (!bDragging && FVector2D::Distance(BoxStart, BoxEnd) > DragThreshold)
			{
				bDragging = true;
			}
		}
	}
}

void ARTSHumanController::OnSelectPressed(const FInputActionValue& Value)
{
	if (!CanIssueGameplayInput()) { return; }
	if (IsPlacingBuilding())
	{
		bPlacementClick = true;
		FHitResult Hit;
		if (GetHitResultUnderCursor(ECC_Visibility, false, Hit)) { ConfirmBuildingPlacement(Hit.ImpactPoint); }
		return;
	}
	FVector2D Mouse;
	if (!GetMousePos(Mouse))
	{
		return;
	}
	bLeftDown = true;
	bDragging = false;
	MouseSelectionFilter = GetMouseSelectionFilter();
	BoxStart = BoxEnd = Mouse;
}

ERTSSelectionFilter ARTSHumanController::GetMouseSelectionFilter() const
{
	const bool Workers = IsInputKeyDown(EKeys::T), Military = IsInputKeyDown(EKeys::G);
	return Workers && Military ? ERTSSelectionFilter::None : Workers ? ERTSSelectionFilter::Workers : Military ? ERTSSelectionFilter::Military : ERTSSelectionFilter::All;
}

bool ARTSHumanController::RequestMouseSelection(AActor* Target, bool bAdditive, ERTSSelectionFilter Filter)
{
	if (!CanIssueGameplayInput() || !Selection || IsPlacingBuilding()) { return false; }
	if (Filter == ERTSSelectionFilter::All)
	{
		if (auto* Building = Cast<ARTSBuilding>(Target); Building && Building->IsAlive() && Building->GetTeam() == Selection->OwningTeam)
		{ SelectBuilding(Building); PlayerCue(this, ERTSAudioCue::Select); return true; }
		if (auto* Resource = Cast<ARTSResourceNode>(Target))
		{ SelectResource(Resource); PlayerCue(this, ERTSAudioCue::Select); return true; }
	}
	SelectBuilding(nullptr);
	SelectResource(nullptr);
	Selection->SelectSingleFiltered(Cast<ARTSUnit>(Target), bAdditive, Filter);
	if (Selection->GetSelectedCount() > 0) { PlayerCue(this, ERTSAudioCue::Select); }
	return true;
}

bool ARTSHumanController::RequestBoxSelection(const FVector2D& A, const FVector2D& B, bool bAdditive, ERTSSelectionFilter Filter)
{
	if (!CanIssueGameplayInput() || !Selection || IsPlacingBuilding() || A.ContainsNaN() || B.ContainsNaN()) { return false; }
	SelectBuilding(nullptr);
	SelectResource(nullptr);
	Selection->SelectInScreenRect(A, B, bAdditive, Filter);
	if (Selection->GetSelectedCount() > 0) { PlayerCue(this, ERTSAudioCue::Select); }
	return true;
}

void ARTSHumanController::OnSelectReleased(const FInputActionValue& Value)
{
	if (!CanIssueGameplayInput()) { bLeftDown = bDragging = bPlacementClick = false; return; }
	if (bPlacementClick) { bPlacementClick = false; return; }
	if (!bLeftDown) { return; }
	bLeftDown = false;
	const bool bAdditive = IsShiftDown();
	const ERTSSelectionFilter Filter = MouseSelectionFilter != ERTSSelectionFilter::All ? MouseSelectionFilter : GetMouseSelectionFilter();
	MouseSelectionFilter = ERTSSelectionFilter::All;
	if (bDragging)
	{
		bDragging = false;
		RequestBoxSelection(BoxStart, BoxEnd, bAdditive, Filter);
		return;
	}
	FHitResult Hit;
	const TArray<TEnumAsByte<EObjectTypeQuery>> Types = { UEngineTypes::ConvertToObjectType(ECC_Pawn), UEngineTypes::ConvertToObjectType(ECC_WorldDynamic) };
	GetHitResultUnderCursorForObjects(Types, false, Hit);
	RequestMouseSelection(Hit.GetActor(), bAdditive, Filter);
}

void ARTSHumanController::OnCommandPressed(const FInputActionValue& Value)
{
	if (!CanIssueGameplayInput())
	{
		UE_LOG(LogTemp,Display,TEXT("RTS_COMMAND_REJECT reason=input_gate paused=%d"),UGameplayStatics::IsGamePaused(this));
		return;
	}
	if (IsPlacingBuilding()) { CancelBuildingPlacement(); return; }
	// P1-03: with a completed own building selected, right-click sets its rally point.
	if (ARTSBuilding* Owned = GetSelectedBuilding())
	{
		FHitResult Ground;
		if (Owned->IsComplete() && GetHitResultUnderCursor(ECC_Visibility, false, Ground) && Ground.bBlockingHit)
		{
			Owned->SetRallyPoint(Ground.ImpactPoint);
			PlayerCue(this, ERTSAudioCue::Order);
			ARTSCommandMarker::SpawnAt(GetWorld(), Ground.ImpactPoint, FLinearColor(0.35f, 0.7f, 1.0f));
			SetEconomyMessage(TEXT("已设置集结点：该建筑新产出的单位会先前往那里。"));
			return;
		}
	}
	// Units use Pawn's Visibility-ignore profile. Resolve the nearest Pawn only
	// when it is in front of the first visible world surface (never through terrain).
	FHitResult Hit;FVector RayStart,RayDirection;
	if (DeprojectMousePositionToWorld(RayStart,RayDirection) && TraceContextTarget(RayStart,RayStart+RayDirection*1000000.,Hit))
	{
		RequestContextCommandAt(Hit.GetActor(),Hit.ImpactPoint,IsShiftDown());
	}
	else
	{
		SetEconomyMessage(TEXT("此处没有可见的命令目标，请点击地面。"));
		UE_LOG(LogTemp,Display,TEXT("RTS_COMMAND_REJECT reason=no_visible_hit"));
	}
}

bool ARTSHumanController::TraceContextTarget(const FVector& Start,const FVector& End,FHitResult& Hit) const
{
	if(!GetWorld() || Start.ContainsNaN() || End.ContainsNaN())return false;
	FHitResult Visible,PawnHit;
	const bool bVisible=GetWorld()->LineTraceSingleByChannel(Visible,Start,End,ECC_Visibility);
	FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_Pawn);
	const bool bPawn=GetWorld()->LineTraceSingleByObjectType(PawnHit,Start,End,Objects);
	if(bPawn && (!bVisible || PawnHit.Distance<=Visible.Distance)){Hit=PawnHit;return true;}
	if(bVisible){Hit=Visible;return true;}return false;
}

bool ARTSHumanController::RequestContextCommandAt(AActor* Target,const FVector& Ground,bool bQueued)
{
	if(!CanIssueGameplayInput() || !Selection || IsPlacingBuilding() || Ground.ContainsNaN())return false;
	TArray<ARTSUnit*> Units;
	for(auto* Unit:Selection->GetSelectedUnits())if(IsValid(Unit) && Unit->IsAlive() && Unit->GetTeam()==Selection->OwningTeam)Units.Add(Unit);
	if(Units.IsEmpty())
	{UE_LOG(LogTemp,Display,TEXT("RTS_COMMAND_REJECT reason=no_selection"));return false;}
	bAttackMovePending=false;
	const auto* Health=IsValid(Target)?Target->FindComponentByClass<URTSHealthComponent>():nullptr;
	const auto* Team=Cast<IGenericTeamAgentInterface>(Target);
	const uint8 TargetTeam=Team?Team->GetGenericTeamId().GetId():RTSTeam::Neutral;
	if(Health && !Health->IsDead() && TargetTeam!=RTSTeam::Neutral && TargetTeam!=Selection->OwningTeam)
	{
		URTSOrderLibrary::IssueAttackOrder(Units,Target,bQueued);PlayerCue(this,ERTSAudioCue::Order);
		ARTSCommandMarker::SpawnAt(GetWorld(),Ground,FLinearColor(1,.15f,.1f));
		UE_LOG(LogTemp,Display,TEXT("RTS_COMMAND_ACCEPT type=attack units=%d target=%s queued=%d"),Units.Num(),*GetNameSafe(Target),bQueued);return true;
	}
	auto* Node=Cast<ARTSResourceNode>(Target);auto* Building=Cast<ARTSBuilding>(Target);
	const bool bWorkTarget=(Node && (Node->GetRemaining()>0 || Node->IsRenewable())) ||
		(Building && Building->IsAlive() && Building->GetTeam()==Selection->OwningTeam &&
		 (!Building->IsComplete() || Building->IsDropOff() || (Building->GetBuildingId()==TEXT("Farm") && Building->GetFoodSource())));
	TArray<ARTSUnit*> Workers,Movers;
	for(auto* Unit:Units)
	{
		if(bWorkTarget && Unit->GetUnitData() && Unit->GetUnitData()->bWorker)Workers.Add(Unit);
		else Movers.Add(Unit);
	}
	FNavLocation Destination;bool bMoveAccepted=false;
	if(!Movers.IsEmpty())
	{
		auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FVector Click=Ground;
		if(Node)Click=Node->GetActorLocation()-FVector(0,0,90);
		else if(Building)Click=Building->GetActorLocation();
		FVector Approach=FVector::ZeroVector;float Radius=0;
		for(auto* U:Movers){Approach+=U->GetActorLocation();Radius=FMath::Max(Radius,U->GetStandingCollisionRadius());}
		Approach=(Approach/Movers.Num()-Click).GetSafeNormal2D();if(Approach.IsNearlyZero())Approach=FVector::ForwardVector;
		const float Spacing=FMath::Max(120.f,Radius*3.6f);
		const float GroupClearance=FMath::CeilToInt(FMath::Sqrt(float(Movers.Num())))*Spacing*.5f+Radius+40;
		// A nav projection can still return a building's interior while its dynamic
		// tile is updating. Use an outside approach point and prove it physically clear.
		const double Offset=Building?Building->GetFootprint()*.5+GroupClearance:Node?Node->GetSimpleCollisionRadius()+GroupClearance:0.;
		FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		const int32 Attempts=Offset>0?8:1;
		for(int32 I=0;I<Attempts && !bMoveAccepted;++I)
		{
			const FVector Candidate=Click+Approach.RotateAngleAxis((I%2?1:-1)*((I+1)/2)*45.,FVector::UpVector)*Offset;
			if(!Nav || !Nav->ProjectPointToNavigation(Candidate,Destination,FVector(100,100,200)))continue;
			if(GetWorld()->OverlapAnyTestByObjectType(Destination.Location+FVector(0,0,82),FQuat::Identity,Objects,FCollisionShape::MakeCapsule(Radius+2,78)))continue;
			URTSOrderLibrary::IssueMoveOrder(this,Movers,Destination.Location,bQueued);bMoveAccepted=true;
		}
	}
	if(!Workers.IsEmpty())URTSOrderLibrary::IssueWorkOrder(Workers,Target,bQueued);
	if(Workers.IsEmpty() && !bMoveAccepted)
	{SetEconomyMessage(TEXT("目标附近没有可通行位置，请点击旁边地面。"));UE_LOG(LogTemp,Display,TEXT("RTS_COMMAND_REJECT reason=no_nearby_nav target=%s units=%d"),*GetNameSafe(Target),Units.Num());return false;}
	PlayerCue(this,ERTSAudioCue::Order);
	ARTSCommandMarker::SpawnAt(GetWorld(),bMoveAccepted?Destination.Location:Ground,FLinearColor(.1f,1,.2f));
	SetEconomyMessage(!Movers.IsEmpty() && !bMoveAccepted?TEXT("工人已收到工作命令；其他单位目标附近不可通行。"):
		bQueued?TEXT("已加入命令队列。"):Workers.IsEmpty()?TEXT("已下达移动命令。"):Movers.IsEmpty()?TEXT("已下达工作命令。"):TEXT("工人执行工作，其他单位移动到目标附近。"));
	UE_LOG(LogTemp,Display,TEXT("RTS_COMMAND_ACCEPT type=context workers=%d movers=%d moveAccepted=%d target=%s ground=%s queued=%d"),Workers.Num(),Movers.Num(),bMoveAccepted,*GetNameSafe(Target),*Ground.ToCompactString(),bQueued);
	return true;
}

bool ARTSHumanController::RequestMinimapMoveAt(const FVector& Ground, bool bQueued)
{
	if (!CanIssueGameplayInput() || !Selection || IsPlacingBuilding() || Ground.ContainsNaN()) { return false; }
	TArray<ARTSUnit*> Units;
	for (auto* Unit : Selection->GetSelectedUnits())
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetTeam()==Selection->OwningTeam) { Units.Add(Unit); }
	if (Units.IsEmpty()) { return false; }
	auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Destination;
	if (!Nav || !Nav->ProjectPointToNavigation(Ground,Destination,FVector(300,300,1500)))
	{
		SetEconomyMessage(TEXT("该小地图位置不可通行，请选择附近地面。"));return false;
	}
	bAttackMovePending=false;
	URTSOrderLibrary::IssueMoveOrder(this,Units,Destination.Location,bQueued);
	PlayerCue(this,ERTSAudioCue::Order);
	ARTSCommandMarker::SpawnAt(GetWorld(),Destination.Location,FLinearColor(.1f,1.f,.2f));
	SetEconomyMessage(bQueued?TEXT("已将小地图移动加入命令队列。"):TEXT("已下达小地图移动命令。"));
	return true;
}

ARTSBuilding* ARTSHumanController::GetSelectedBuilding() const
{
	ARTSBuilding* Building = SelectedBuilding.Get();
	return Building && Building->IsAlive() ? Building : nullptr;
}
void ARTSHumanController::SetEconomyMessage(const FString& Message)
{
	EconomyMessage = Message;
	OnEconomyContextChanged.Broadcast();
}
void ARTSHumanController::SelectBuilding(ARTSBuilding* Building)
{
	if (auto* Resource=SelectedResource.Get()) Resource->SetSelected(false);
	SelectedResource.Reset();
	SelectedBuilding = Building;
	if (Building) { Selection->ClearSelection(); }
	OnEconomyContextChanged.Broadcast();
}
ARTSResourceNode* ARTSHumanController::GetSelectedResource() const
{
	return SelectedResource.Get();
}
void ARTSHumanController::SelectResource(ARTSResourceNode* Resource)
{
	// Clearing a resource that is not selected must not wipe an additive unit selection.
	if (!Resource && !SelectedResource.IsValid()) { return; }
	SelectBuilding(nullptr);
	Selection->ClearSelection();
	if (ARTSResourceNode* Previous = SelectedResource.Get()) { Previous->SetSelected(false); }
	SelectedResource = Resource;
	if (Resource) { Resource->SetSelected(true); }
	OnEconomyContextChanged.Broadcast();
}
void ARTSHumanController::BeginBuildingPlacement(FName Id)
{
	FString Error;
	if (!CanStartBuildingPlacement(Id, Error)) { SetEconomyMessage(Error); return; }
	PlacementId = Id; bLeftDown = bDragging = false;
	SetEconomyMessage(TEXT("左键空地确认建造；右键或退格键取消。"));
}
void ARTSHumanController::CancelBuildingPlacement()
{
	PlacementId = NAME_None;
	SetEconomyMessage(TEXT("已取消放置，未消耗资源。"));
}
bool ARTSHumanController::ConfirmBuildingPlacement(const FVector& Ground)
{
	if (!CanIssueGameplayInput()) { return false; }
	if (PlacementId.IsNone()) { return false; }
	TArray<ARTSUnit*> Workers;
	FString Error;
	if (!CanStartBuildingPlacement(PlacementId, Error)) { SetEconomyMessage(Error); return false; }
	if (!CollectSelectedWorkers(Workers) || !ValidateCampaignPlacement(PlacementId, Ground, Workers, Error))
	{
		SetEconomyMessage(Error.IsEmpty() ? TEXT("请先选中存活的工人。") : Error);
		return false;
	}
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
	ARTSBuilding* Building = Balance ? Balance->PlaceBuilding(PlacementId, Selection->OwningTeam, Ground, Error) : nullptr;
	if (!Building) { SetEconomyMessage(Error); return false; }
	PlacementId = NAME_None;
	URTSOrderLibrary::IssueWorkOrder(Workers, Building);
	PlayerCue(this, ERTSAudioCue::Order);
	SetEconomyMessage(TEXT("已下达施工命令；工人右键工地可继续建造。"));
	return true;
}

bool ARTSHumanController::CollectSelectedWorkers(TArray<ARTSUnit*>& OutWorkers) const
{
	OutWorkers.Reset();
	if (!Selection) { return false; }
	for (ARTSUnit* Unit : Selection->GetSelectedUnits())
	{
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetTeam() == RTSTeam::Player
			&& Unit->GetUnitData() && Unit->GetUnitData()->bWorker)
		{
			OutWorkers.Add(Unit);
		}
	}
	return !OutWorkers.IsEmpty();
}

bool ARTSHumanController::CanStartBuildingPlacement(FName Id, FString& Error) const
{
	Error.Reset();
	if (!CanIssueGameplayInput())
	{
		Error = TEXT("当前状态不能建造。");
		return false;
	}
	TArray<ARTSUnit*> Workers;
	if (!CollectSelectedWorkers(Workers))
	{
		Error = TEXT("请先选中存活的工人。");
		return false;
	}
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
	const FRTSBalanceRow* Row = Balance ? Balance->Find(Id) : nullptr;
	if (!Row || Row->Kind != TEXT("Building"))
	{
		Error = TEXT("建筑数值尚未就绪。");
		return false;
	}
	const ARTSCampaign* Campaign = ARTSCampaign::Get(this);
	if (!Campaign) { return true; } // Match mode keeps the original generic construction rules.
	if (Id!=TEXT("Barracks") && Id!=TEXT("ArcheryRange") && Id!=TEXT("ResearchInstitute") && Id!=TEXT("SupportBarracks") && Id!=TEXT("Farm") && Id!=TEXT("Tower"))
	{
		Error = TEXT("请选择工人面板中提供的建筑。");
		return false;
	}
	int32 AliveFriendlyBuildings = 0;
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (It->GetTeam() == RTSTeam::Player && It->IsAlive()) { ++AliveFriendlyBuildings; }
	}
	const URTSResourceComponent* Stock = Balance->GetResources(RTSTeam::Player);
	const int32 Wood = Stock ? Stock->GetAmount(ERTSResource::Wood) : 0;
	const int32 Food = Stock ? Stock->GetAmount(ERTSResource::Food) : 0;
	return CanStartCampaignBarracksBuild(Campaign->GetGuideStep(), AliveFriendlyBuildings, Wood, Food, *Row, Error);
}

bool ARTSHumanController::ValidateCampaignPlacement(FName Id, const FVector& Ground,
	const TArray<ARTSUnit*>& Workers, FString& Error) const
{
	Error.Reset();
	const ARTSCampaign* Campaign = ARTSCampaign::Get(this);
	if (!Campaign) { return true; }
	if (Id!=TEXT("Barracks") && Id!=TEXT("ArcheryRange") && Id!=TEXT("ResearchInstitute") && Id!=TEXT("SupportBarracks") && Id!=TEXT("Farm") && Id!=TEXT("Tower"))
	{
		Error = TEXT("请选择工人面板中提供的建筑。");
		return false;
	}
	const URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
	const FRTSBalanceRow* Row = Balance ? Balance->Find(Id) : nullptr;
	if (!Row || Row->Kind != TEXT("Building"))
	{
		Error = TEXT("建筑数值尚未就绪。");
		return false;
	}
	if (!IsCampaignFootprintInsideMap(Ground, Row->Footprint))
	{
		Error = TEXT("建筑必须完整位于当前战役边界内。");
		return false;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav || !Nav->IsInitialized() || Nav->IsNavigationBuildInProgress())
	{
		Error = TEXT("导航尚未就绪，请稍后再放置。");
		return false;
	}
	FNavLocation End;
	if (!Nav->ProjectPointToNavigation(Ground, End, FVector(50, 50, 120)))
	{
		Error = TEXT("请选择平坦且可行走的地面。");
		return false;
	}
	int32 ReachableWorkers = 0;
	for (const ARTSUnit* Worker : Workers)
	{
		FNavLocation Start;
		if (!IsValid(Worker) || !Worker->IsAlive()
			|| !Nav->ProjectPointToNavigation(Worker->GetActorLocation(), Start, FVector(60, 60, 160)))
		{
			continue;
		}
		UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), Start.Location, End.Location);
		if (Path && Path->IsValid() && !Path->IsPartial()) { ++ReachableWorkers; }
	}
	if (ReachableWorkers <= 0)
	{
		Error = TEXT("所选的工人均无法到达该位置，未消耗资源。");
		return false;
	}
	return true;
}
void ARTSHumanController::OnCancelPressed(const FInputActionValue& Value)
{
	if (!CanIssueGameplayInput()) { return; }
	if (IsPlacingBuilding()) { CancelBuildingPlacement(); }
	else if (GetSelectedResource()) { SelectResource(nullptr); }
	else if (Selection->GetSelectedCount() > 0) { URTSOrderLibrary::IssueStopOrder(Selection->GetSelectedUnits()); PlayerCue(this, ERTSAudioCue::Order); }
}

bool ARTSHumanController::CanIssueGameplayInput() const
{
	if(UGameplayStatics::IsGamePaused(this))return false;
	const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this);
	return !Match || Match->AllowsGameplay();
}
void ARTSHumanController::OnPausePressed()
{ ToggleGamePause(); }
void ARTSHumanController::ToggleGamePause()
{
 if(ARTSCampaign::Get(this))
 {
  bLeftDown=false;bDragging=false;
  UGameplayStatics::SetGamePaused(this,!UGameplayStatics::IsGamePaused(this));
 }
 else if(auto* Match=URTSMatchSubsystem::Get(this))Match->TogglePause();
}

bool ARTSHumanController::IsCtrlDown() const
{
	return IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl);
}
void ARTSHumanController::AssignControlGroup(int32 Slot)
{
	if (Slot < 0 || Slot >= 9 || !Selection) { return; }
	TArray<TWeakObjectPtr<ARTSUnit>> Group;
	for (ARTSUnit* Unit : Selection->GetSelectedUnits())
	{
		if (IsValid(Unit) && Unit->IsAlive()) { Group.Add(Unit); }
	}
	while (ControlGroups.Num() < 9) { ControlGroups.AddDefaulted(); }
	ControlGroups[Slot] = MoveTemp(Group);
	const int32 Count = ControlGroups[Slot].Num();
	SetEconomyMessage(Count > 0
		? FString::Printf(TEXT("已把 %d 个单位编入第 %d 组（Ctrl+%d 重新编组，%d 选中）。"), Count, Slot + 1, Slot + 1, Slot + 1)
		: FString::Printf(TEXT("第 %d 组已清空。"), Slot + 1));
	if (Count > 0) { PlayerCue(this, ERTSAudioCue::Select); }
}
bool ARTSHumanController::RecallControlGroup(int32 Slot, bool bAdditive)
{
	if (!Selection || !ControlGroups.IsValidIndex(Slot)) { return false; }
	TArray<ARTSUnit*> Alive;
	for (const TWeakObjectPtr<ARTSUnit>& Weak : ControlGroups[Slot])
	{
		ARTSUnit* Unit = Weak.Get();
		if (IsValid(Unit) && Unit->IsAlive()) { Alive.Add(Unit); }
	}
	if (Alive.IsEmpty())
	{
		SetEconomyMessage(FString::Printf(TEXT("第 %d 组已没有存活单位。"), Slot + 1));
		return false;
	}
	SelectBuilding(nullptr);
	SelectResource(nullptr);
	if (!bAdditive) { Selection->ClearSelection(); }
	for (ARTSUnit* Unit : Alive) { Selection->SelectSingle(Unit, true); }
	SetEconomyMessage(FString::Printf(TEXT("已选中第 %d 组：%d 个单位。"), Slot + 1, Alive.Num()));
	PlayerCue(this, ERTSAudioCue::Select);
	return true;
}
void ARTSHumanController::OnGroupSlot(int32 Slot)
{
	if (!CanIssueGameplayInput()) { return; }
	if (IsCtrlDown()) { AssignControlGroup(Slot); }
	else { RecallControlGroup(Slot, IsShiftDown()); }
}
bool ARTSHumanController::SelectIdleWorker()
{
	if (!CanIssueGameplayInput() || !Selection) { return false; }
	TArray<ARTSUnit*> Idle;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive() || It->GetTeam() != RTSTeam::Player) { continue; }
		const URTSUnitData* Data = It->GetUnitData();
		if (!Data || !Data->bWorker) { continue; }
		const ERTSOrderType Type = It->GetCurrentOrder().Type;
		if (Type == ERTSOrderType::None || Type == ERTSOrderType::Stop) { Idle.Add(*It); }
	}
	if (Idle.IsEmpty())
	{
		SetEconomyMessage(TEXT("没有空闲工人：所有工人都已有任务。"));
		return false;
	}
	IdleCursor = IdleCursor % Idle.Num();
	ARTSUnit* Pick = Idle[IdleCursor];
	IdleCursor = (IdleCursor + 1) % Idle.Num();
	SelectBuilding(nullptr);
	SelectResource(nullptr);
	Selection->ClearSelection();
	Selection->SelectSingle(Pick, false);
	SetEconomyMessage(FString::Printf(TEXT("已选中一名空闲工人；全场共 %d 名空闲工人。"), Idle.Num()));
	PlayerCue(this, ERTSAudioCue::Select);
	return true;
}
int32 ARTSHumanController::SelectAllWorkers() { return SelectAllByRole(true); }
int32 ARTSHumanController::SelectAllMilitary() { return SelectAllByRole(false); }
int32 ARTSHumanController::SelectAllByRole(bool bWorkers)
{
	if (!CanIssueGameplayInput() || !Selection) { return 0; }
	bLeftDown = bDragging = false;
	SelectBuilding(nullptr);
	SelectResource(nullptr);
	Selection->ClearSelection();
	int32 Count = 0;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive() || It->GetTeam() != Selection->OwningTeam) { continue; }
		const URTSUnitData* Data = It->GetUnitData();
		if (!Data || Data->bWorker != bWorkers) { continue; }
		Selection->SelectSingle(*It, true);
		++Count;
	}
	if (Count > 0)
	{
		SetEconomyMessage(bWorkers
			? FString::Printf(TEXT("已选中全部 %d 名工人（包含忙碌工人，不包含军队；Alt+F1）。"), Count)
			: FString::Printf(TEXT("已选中全部 %d 名战斗单位（不包含工人；Alt+F2）。"), Count));
	}
	else { SetEconomyMessage(bWorkers ? TEXT("没有可选的工人。") : TEXT("没有可选的战斗单位。")); }
	if (Count > 0) { PlayerCue(this, ERTSAudioCue::Select); }
	return Count;
}
bool ARTSHumanController::SetSelectedBuildingRally(const FVector& Ground)
{
	ARTSBuilding* Building = GetSelectedBuilding();
	if (!Building || !Building->IsComplete()) { return false; }
	Building->SetRallyPoint(Ground);
	return true;
}
void ARTSHumanController::OnDebugPressed()
{
	if (URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(this)) { Debug->TogglePanel(); }
}

void ARTSHumanController::OnWorkerKey() { SelectAllWorkers(); }
void ARTSHumanController::OnSelectAllKey() { SelectAllMilitary(); }
void ARTSHumanController::OnProductionKey() { SelectNextProductionBuilding(); }

bool ARTSHumanController::SelectNextProductionBuilding()
{
	UWorld* World = GetWorld();
	if (!World || IsPlacingBuilding()) return false;
	// P1-02: jump straight to a completed production building so training never needs a minimap hunt.
	TArray<ARTSBuilding*> Candidates;
	for (TActorIterator<ARTSBuilding> It(World); It; ++It)
	{
		ARTSBuilding* Building = *It;
		if (Building && Building->GetTeam() == 0 && Building->IsAlive() && Building->IsComplete() && Building->GetProducer()) Candidates.Add(Building);
	}
	if (Candidates.Num() == 0) return false;
	const int32 Index = ProductionCursor % Candidates.Num();
	ProductionCursor = Index + 1;
	ARTSBuilding* Pick = Candidates[Index];
	SelectBuilding(Pick);
	// Same convention as the campaign focus: move the camera pivot onto the chosen building.
	if (APawn* CameraPawn = GetPawn()) CameraPawn->SetActorLocation(Pick->GetActorLocation() + FVector(0, 0, 100));
	return true;
}

void ARTSHumanController::OnAttackMoveKey() {}

bool ARTSHumanController::RequestAttackMoveAt(const FVector& Goal)
{
	// Attack-move is not offered to the player; the API rejects those callers.
	return false;
}
