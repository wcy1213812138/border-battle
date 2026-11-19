#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Combat/RTSAcquisitionComponent.h"
#include "Economy/RTSGathererComponent.h"
#include "Combat/RTSHitFeedbackComponent.h"
#include "Audio/RTSAudioFeedbackComponent.h"
#include "Units/RTSUnitAIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "Orders/RTSLocalFormationPath.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimSequence.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "Buildings/RTSBuilding.h"
#include "NavigationPath.h"

namespace
{
	constexpr float RingDiameterScale = 1.6f;   // ring diameter relative to capsule diameter
	constexpr float RingThickness = 0.02f;      // engine cylinder is 100cm tall -> 2cm
	constexpr float MovingSpeedSq = 25.0f * 25.0f;
	constexpr float SlotAcceptanceRadius = 5.0f;
	constexpr float SlotTolerance = 15.0f;
	constexpr float GroupMoveTimeout = 90.0f;
}

ARTSUnit::ARTSUnit()
{
	PrimaryActorTick.bCanEverTick = true;

	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = ARTSUnitAIController::StaticClass();
	bUseControllerRotationYaw = false;

	GetCapsuleComponent()->InitCapsuleSize(40.0f, 80.0f);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
	Move->MaxWalkSpeed = 350.0f;
	Move->MaxAcceleration = 1500.0f;
	Move->BrakingDecelerationWalking = 1500.0f;
	// Avoidance is done by DetourCrowd (ARTSUnitAIController); RVO would fight it.
	Move->bUseRVOAvoidance = false;
	Move->bConstrainToPlane = true;
	Move->bSnapToPlaneAtStart = true;

	// Capsule handles picking; the skeletal mesh is visual only.
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0, 0.0, -80.0), FRotator(0.0, -90.0, 0.0));

	Health = CreateDefaultSubobject<URTSHealthComponent>(TEXT("Health"));
	Attack = CreateDefaultSubobject<URTSAttackComponent>(TEXT("Attack"));
	Acquisition = CreateDefaultSubobject<URTSAcquisitionComponent>(TEXT("Acquisition"));
	Gatherer = CreateDefaultSubobject<URTSGathererComponent>(TEXT("Gatherer"));
	HitFeedback = CreateDefaultSubobject<URTSHitFeedbackComponent>(TEXT("HitFeedback"));
	CreateDefaultSubobject<URTSAudioFeedbackComponent>(TEXT("AudioFeedback"));

	SelectionRing = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectionRing"));
	SelectionRing->SetupAttachment(GetCapsuleComponent());
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		SelectionRing->SetStaticMesh(CylinderMesh.Object);
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (ShapeMaterial.Succeeded())
	{
		SelectionRing->SetMaterial(0, ShapeMaterial.Object);
	}
	SelectionRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SelectionRing->SetCanEverAffectNavigation(false);
	SelectionRing->SetCastShadow(false);
	SelectionRing->SetVisibility(false);

	TeamArmband = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TeamArmband"));
	TeamArmband->SetupAttachment(GetMesh());
	if (CylinderMesh.Succeeded()) { TeamArmband->SetStaticMesh(CylinderMesh.Object); }
	if (ShapeMaterial.Succeeded()) { TeamArmband->SetMaterial(0, ShapeMaterial.Object); }
	TeamArmband->SetRelativeLocation(FVector(10, 0, 0));
	TeamArmband->SetRelativeRotation(FRotator(90, 0, 0));
	TeamArmband->SetRelativeScale3D(FVector(0.16f, 0.16f, 0.065f));
	TeamArmband->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TeamArmband->SetCanEverAffectNavigation(false);
	TeamArmband->SetVisibility(false);
}

void ARTSUnit::Configure(URTSUnitData* Data, uint8 InTeam)
{
	check(!HasActorBegunPlay());
	UnitData = Data;
	SetGenericTeamId(FGenericTeamId(InTeam));
}

void ARTSUnit::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyUnitData();
}

void ARTSUnit::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplyUnitData();
}

void ARTSUnit::ApplyUnitData()
{
	const float Radius = UnitData ? UnitData->CapsuleRadius : GetCapsuleComponent()->GetUnscaledCapsuleRadius();
	const float HalfHeight = UnitData ? UnitData->CapsuleHalfHeight : GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight();

	GetCapsuleComponent()->SetCapsuleSize(Radius, HalfHeight);
	StandingCapsuleRadius = Radius;

	const float RingScale = Radius * 2.0f * RingDiameterScale / 100.0f;
	SelectionRing->SetRelativeLocation(FVector(0.0, 0.0, -HalfHeight + 2.0));
	SelectionRing->SetRelativeScale3D(FVector(RingScale, RingScale, RingThickness));

	if (!UnitData)
	{
		return;
	}

	if (UnitData->Mesh)
	{
		GetMesh()->SetSkeletalMeshAsset(UnitData->Mesh);
	}
	FTransform MeshTransform = UnitData->MeshOffset;
	MeshTransform.AddToTranslation(FVector(0.0, 0.0, -HalfHeight));
	GetMesh()->SetRelativeTransform(MeshTransform);

	GetCharacterMovement()->MaxWalkSpeed = UnitData->MoveSpeed;
	Health->SetMaxHealth(UnitData->MaxHealth);
	Health->SetRegeneration(UnitData->bWorker ? 0.1f : 0.0f, 30.0f);
	Attack->SetStats(UnitData->AttackDamage, UnitData->AttackRange, UnitData->AttackInterval, UnitData->AttackHitDelay);
	Attack->SetProjectileStats(UnitData->bRangedAttack, UnitData->ProjectileSpeed);
	ApplyTeamAppearance();
}

void ARTSUnit::SetGenericTeamId(const FGenericTeamId& NewTeamId)
{
	TeamId = NewTeamId.GetId();
	ApplyTeamAppearance();
	if (HasActorBegunPlay())
	{
		if (UMaterialInstanceDynamic* Ring = SelectionRing->CreateAndSetMaterialInstanceDynamic(0))
		{ Ring->SetVectorParameterValue(TEXT("Color"), TeamId == RTSTeam::Player ? FLinearColor(0.1f, 1, 0.2f) : FLinearColor(1, 0.15f, 0.1f)); }
		SelectionRing->SetVisibility(bSelected || TeamId == RTSTeam::Enemy);
	}
}

void ARTSUnit::ApplyTeamAppearance()
{
	if (!TeamArmband || !GetMesh() || !GetMesh()->GetSkeletalMeshAsset()) { return; }
	const bool bTeam = TeamId == RTSTeam::Player || TeamId == RTSTeam::Enemy;
	// Only replace this pack's named clothing slot; other meshes/materials stay intact.
	const int32 Slot = GetMesh()->GetMaterialIndex(TEXT("TT_RTS_Units"));
	if (Slot != INDEX_NONE)
	{
		UMaterialInterface* Material = bTeam ? LoadObject<UMaterialInterface>(nullptr,
			TeamId == RTSTeam::Player
			? TEXT("/Game/Toony_Tiny_RTS_Set/Materials/Units/MI_Units_Blue.MI_Units_Blue")
			: TEXT("/Game/Toony_Tiny_RTS_Set/Materials/Units/MI_Units_Red.MI_Units_Red"))
			: GetMesh()->GetSkeletalMeshAsset()->GetMaterials()[Slot].MaterialInterface.Get();
		if (Material) { GetMesh()->SetMaterial(Slot, Material); }
	}
	const bool bWorker = UnitData && (UnitData->bWorker || GetMesh()->GetSkeletalMeshAsset()->GetName() == TEXT("SK_Peasant"));
	const FName BandBone = bWorker && GetMesh()->GetBoneIndex(TEXT("Bip001-L-UpperArm")) != INDEX_NONE ? FName(TEXT("Bip001-L-UpperArm")) : NAME_None;
	if (TeamArmband->GetAttachSocketName() != BandBone)
	{ TeamArmband->AttachToComponent(GetMesh(), FAttachmentTransformRules::KeepRelativeTransform, BandBone); }
	TeamArmband->SetVisibility(bTeam && bWorker && GetMesh()->GetBoneIndex(TEXT("Bip001-L-UpperArm")) != INDEX_NONE);
	if (bTeam && bWorker)
	{
		UMaterialInstanceDynamic* Band = Cast<UMaterialInstanceDynamic>(TeamArmband->GetMaterial(0));
		if (!Band) { Band = TeamArmband->CreateAndSetMaterialInstanceDynamic(0); }
		if (Band) { Band->SetVectorParameterValue(TEXT("Color"), TeamId == RTSTeam::Player ? FLinearColor(0.02f, 0.18f, 1) : FLinearColor(1, 0.025f, 0.015f)); }
	}
}

void ARTSUnit::BeginPlay()
{
	Super::BeginPlay();

	Health->OnDied.AddDynamic(this, &ARTSUnit::HandleDied);
	Attack->OnAttackStarted.AddDynamic(this, &ARTSUnit::HandleAttackStarted);

	if (UMaterialInstanceDynamic* RingMID = SelectionRing->CreateAndSetMaterialInstanceDynamic(0))
	{
		const FLinearColor RingColor = (TeamId == RTSTeam::Player) ? FLinearColor(0.1f, 1.0f, 0.2f) : FLinearColor(1.0f, 0.15f, 0.1f);
		RingMID->SetVectorParameterValue(TEXT("Color"), RingColor);
	}
	SelectionRing->SetVisibility(TeamId == RTSTeam::Enemy);

	if (!UnitData)
	{
		UE_LOG(LogRTS, Warning, TEXT("%s has no UnitData; it will have no mesh or animations."), *GetName());
	}
	PlayAnimState(ERTSUnitAnimState::Idle);
}

FName ARTSUnit::GetFormationUnitType() const
{
	if (UnitData && !UnitData->BalanceId.IsNone()) { return UnitData->BalanceId; }
	return FName(*(UnitData ? UnitData->GetPathName() : GetClass()->GetPathName()));
}

bool ARTSUnit::IsAlive() const
{
	return Health && !Health->IsDead();
}

void ARTSUnit::SetSelected(bool bInSelected)
{
	bSelected = bInSelected && IsAlive();
	SelectionRing->SetVisibility(IsAlive() && (bSelected || TeamId == RTSTeam::Enemy));
}

ERTSCombatStance ARTSUnit::GetCombatStance() const { return Acquisition->GetStance(); }
void ARTSUnit::SetCombatStance(ERTSCombatStance Stance)
{
	if (!IsAlive()) { return; }
	if (Stance == ERTSCombatStance::Guard && YieldPhase)
	{
		FRTSOrder Stop; Stop.Type = ERTSOrderType::Stop;
		ReceiveOrder(Stop);
	}
	Acquisition->SetStance(Stance);
}
bool ARTSUnit::IsHoldingAutomaticPosition() const { return Acquisition->IsHoldingPosition(); }


void ARTSUnit::ReceiveOrder(const FRTSOrder& Order)
{
	if (!IsAlive())
	{
		return;
	}
	if (Order.Type == ERTSOrderType::Attack && !URTSAttackComponent::IsHostileTarget(this, Order.TargetActor.Get())) { return; }
	if (UnitData && UnitData->bHealer && Order.Type == ERTSOrderType::Attack) { return; }
	EndDeadlockUnstick();
	HealingTarget.Reset(); HealResolveTime = -1;
	bHealingStopped = Order.Type == ERTSOrderType::Stop;
	EndCongestionRecovery();
	Acquisition->NotifyNewOrder();
	Gatherer->NotifyNewOrder();
	Attack->CancelAttack();
	AttackAnimEndTime = 0.0;

	// Any new order gives up the slot of a previous group move.
	LeaveMoveGroup();

	CurrentOrder = Order;
	MissingNavRetryUntil = 0; NextMissingNavRetry = 0;
	NextRepathTime = 0.0;
	BuildingApproachTarget.Reset();

	switch (Order.Type)
	{
	case ERTSOrderType::Move:
	case ERTSOrderType::AttackMove:
		Attack->CancelAttack();
		AttackAnimEndTime = 0.0;
		// Attack-move keeps a destination, so meeting an enemy only interrupts the march.
		if (Order.Type == ERTSOrderType::AttackMove) { SetAttackMoveGoal(Order.TargetLocation); }
		else { ClearAttackMoveGoal(); }
		if (const AAIController* UnitAI = Cast<AAIController>(GetController()))
		{
			if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(UnitAI->GetPathFollowingComponent()))
			{ Crowd->SetPreciseArrival(true); }
		}
		if (!RequestMoveTo(Order.TargetLocation, SlotAcceptanceRadius))
		{
			if (TryRefreshMissingNavigation()) { MissingNavRetryUntil = GetWorldTimeForTraffic() + 5; NextMissingNavRetry = GetWorldTimeForTraffic() + .5; }
			else { CurrentOrder = FRTSOrder(); }
		}
		break;
	case ERTSOrderType::Attack:
		// Chasing and swinging are driven from Tick.
		break;
	case ERTSOrderType::Gather:
	case ERTSOrderType::Build:
	case ERTSOrderType::ReturnResources:
		ClearAttackMoveGoal();
		StopAIMovement();
		break;
	case ERTSOrderType::Stop:
	case ERTSOrderType::None:
	default:
		Attack->CancelAttack();
		AttackAnimEndTime = 0.0;
		StopAIMovement();
		// An explicit stop is the player's cancel: queued steps and the march goal go with it.
		OrderQueue.Reset();
		ClearAttackMoveGoal();
		CurrentOrder = FRTSOrder();
		break;
	}
}

void ARTSUnit::QueueOrder(const FRTSOrder& Order)
{
	if (!IsAlive() || Order.Type == ERTSOrderType::None || Order.Type == ERTSOrderType::Stop) { return; }
	if (Order.Type == ERTSOrderType::Attack && !URTSAttackComponent::IsHostileTarget(this, Order.TargetActor.Get())) { return; }
	if (UnitData && UnitData->bHealer && Order.Type == ERTSOrderType::Attack) { return; }
	// Bounded queue: a runaway Shift click must never grow the backlog without limit.
	if (OrderQueue.Num() >= 8) { OrderQueue.RemoveAt(0); }
	OrderQueue.Add(Order);
	// An idle unit starts working immediately instead of waiting for a first order to end.
	if (CurrentOrder.Type == ERTSOrderType::None) { FinishOrder(); }
}

void ARTSUnit::FinishOrder()
{
	if (IsAlive() && OrderQueue.Num() > 0)
	{
		const FRTSOrder Next = OrderQueue[0];
		OrderQueue.RemoveAt(0);
		ReceiveOrder(Next);
		return;
	}
	// Combat interrupted an attack-move: return to the remembered destination.
	if (IsAlive() && bHasAttackMoveGoal)
	{
		FRTSOrder Resume;
		Resume.Type = ERTSOrderType::AttackMove;
		Resume.TargetLocation = AttackMoveGoal;
		ReceiveOrder(Resume);
		return;
	}
	CurrentOrder = FRTSOrder();
}

void ARTSUnit::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!IsAlive())
	{
		EndDeadlockUnstick();
		return;
	}

	UpdateFormationCollision();
	TickDeadlockUnstick();
	if (TickTrafficPass()) { UpdateAnimation(); return; }
	if (IsTrafficSidesteppingOrWaiting())
	{
		TickTrafficYield();
		UpdateAnimation();
		return;
	}
	switch (CurrentOrder.Type)
	{
	case ERTSOrderType::Attack:
		TickAttackOrder(DeltaSeconds);
		break;
	case ERTSOrderType::Move:
	case ERTSOrderType::AttackMove:
		if (MoveGroup.IsValid())
		{
			TickGroupMove();
		}
		else if (const AAIController* AI = Cast<AAIController>(GetController()))
		{
			if (AI->GetMoveStatus() == EPathFollowingStatus::Idle)
			{
				if (MissingNavRetryUntil > GetWorldTimeForTraffic())
				{
					if (GetWorldTimeForTraffic() >= NextMissingNavRetry)
					{
						NextMissingNavRetry = GetWorldTimeForTraffic() + .5;
						if (RequestMoveTo(CurrentOrder.TargetLocation, SlotAcceptanceRadius)) { MissingNavRetryUntil = 0; }
					}
					break;
				}
				// Reaching the march goal ends the attack-move; combat interruptions resume it.
				if (CurrentOrder.Type == ERTSOrderType::AttackMove) { ClearAttackMoveGoal(); }
				FinishOrder();
			}
		}
		break;
	default:
		break;
	}

	if (UnitData && UnitData->bHealer) { TickHealing(DeltaSeconds); }
	TickCongestionRecovery();
	UpdateAnimation();
}

bool ARTSUnit::RequestMoveTo(const FVector& Location, float AcceptanceRadius)
{
	AAIController* AI = Cast<AAIController>(GetController());
	if (!AI)
	{
		UE_LOG(LogRTS,Warning,TEXT("RTS_MOVE_REJECT unit=%s reason=no_ai_controller"),*GetName());
		return false;
	}
	FAIMoveRequest Request(Location);
	Request.SetAcceptanceRadius(AcceptanceRadius);
	Request.SetReachTestIncludesAgentRadius(false);
	Request.SetUsePathfinding(true);
	Request.SetAllowPartialPath(true);
	Request.SetProjectGoalLocation(true);
	Request.SetCanStrafe(false);
	if (AI->MoveTo(Request).Code == EPathFollowingRequestResult::Failed)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		FNavLocation StartNav;
		const FVector Feet = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
		const bool bStartOnNav = Nav && Nav->ProjectPointToNavigation(Feet, StartNav, FVector(50, 50, 100));
		UE_LOG(LogRTS, Warning, TEXT("%s: move to %s failed (no path from %s). startNav=%s navBuilding=%d mode=%d speed=%.1f"),
			*GetName(), *Location.ToCompactString(), *GetActorLocation().ToCompactString(),
			bStartOnNav ? *StartNav.Location.ToCompactString() : TEXT("none"), Nav && Nav->IsNavigationBuildInProgress(),
			static_cast<int32>(GetCharacterMovement()->MovementMode), GetCharacterMovement()->MaxWalkSpeed);
		TryRefreshMissingNavigation();
		return false;
	}
	return true;
}

bool ARTSUnit::TryRefreshMissingNavigation()
{
	const double Now = GetWorldTimeForTraffic();
	if (ActorHasTag(TEXT("RTS_CampaignRestoreStaged")) || !IsAlive() || GetCharacterMovement()->MovementMode != MOVE_Walking || GetCharacterMovement()->MaxWalkSpeed <= 0) { return false; }
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Feet = GetActorLocation() - FVector(0,0,Half);
	FNavLocation P;
	if (!Nav || Nav->ProjectPointToNavigation(Feet,P,FVector(50,50,100))) { return false; }
	FCollisionQueryParams Q(SCENE_QUERY_STAT(MissingNavigationSupport),false,this);
	FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FHitResult Ground;
	if (!GetWorld()->LineTraceSingleByObjectType(Ground,Feet+FVector(0,0,12),Feet-FVector(0,0,30),FCollisionObjectQueryParams(ECC_WorldStatic),Q)
		|| Ground.ImpactNormal.Z < .7f || GetWorld()->OverlapAnyTestByObjectType(GetActorLocation(),FQuat::Identity,Objects,
			FCollisionShape::MakeCapsule(FMath::Max(1.f,GetSimpleCollisionRadius()-1),Half-3),Q)) { return false; }
	if (Now >= NextMissingNavRefresh)
	{
		NextMissingNavRefresh = Now + 10;
		Nav->AddDirtyArea(FBox(Feet-FVector(120,120,100),Feet+FVector(120,120,200)),ENavigationDirtyFlag::All,TEXT("RTS missing travel start"));
		UE_LOG(LogRTS,Display,TEXT("RTS_MISSING_NAV_REFRESH unit=%s feet=%s collisionUnchanged=1"),*GetName(),*Feet.ToCompactString());
	}
	return true;
}

void ARTSUnit::ReceiveGroupMove(const TSharedRef<FRTSMoveGroup>& Group, const FVector& InWaitingPoint)
{
	if (!IsAlive())
	{
		return;
	}
	EndDeadlockUnstick();
	EndCongestionRecovery();
	Acquisition->NotifyNewOrder();
	Gatherer->NotifyNewOrder();
	FRTSOrder Order;
	Order.Type = ERTSOrderType::Move;
	Order.TargetLocation = Group->Center;
	// Cancel the old trip now; the new trip starts only after the shared NavMesh check.
	StopAIMovement();
	LeaveMoveGroup();
	CurrentOrder = Order;
	NextRepathTime = 0.0;
	BuildingApproachTarget.Reset();
	Attack->CancelAttack();
	AttackAnimEndTime = 0.0;

	MoveGroup = Group;
	// Registration gives this unit a lane and the exact coordinate it will take, so the
	// whole column can queue up in the real arrival order instead of jostling for slots.
	GroupMember = Group->RegisterMember(GetFormationUnitType(), GetActorLocation());
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))
		{ Crowd->SetPreciseArrival(true); }
	}
	GroupWaitingPoint = InWaitingPoint;
	GroupSlot = INDEX_NONE;
	GroupMoveStarted = 0.0;
	bGroupApproachStarted = false;
	NextGroupMoveAttempt = 0.0;
}

void ARTSUnit::TickGroupMove()
{
	ARTSUnitAIController* AI = Cast<ARTSUnitAIController>(GetController());
	if (!AI || !MoveGroup->PrepareNavigation(GetWorld()))
	{
		if (MoveGroup->bNavigationFailed)
		{
			StopAIMovement();
			LeaveMoveGroup();
			FinishOrder();
		}
		return;
	}
	UpdateFormationCollision();
	URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent());
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector Location = GetActorLocation();
	const bool bIdle = AI->GetMoveStatus() == EPathFollowingStatus::Idle;
	if (GroupMoveStarted == 0.0)
	{
		GroupMoveStarted = Now;
	}
	// A queued waypoint is a pass-through, not a formation assembly stop.
	if (OrderQueue.Num() > 0 && CurrentOrder.Type == ERTSOrderType::Move
		&& FVector::DistSquared2D(Location, MoveGroup->RequestedGoal) <= FMath::Square(220.f))
	{
		LeaveMoveGroup();
		FinishOrder();
		return;
	}

	if (MoveGroup->bLaneEntry)
	{
		// One file per column: the shared group decides whether this unit may still
		// advance, claim its row or settle. The lane gate owns the spacing, and crowd
		// avoidance is both wider than a lane and the reason followers crawl at the
		// corridor mouth, so queueing units follow their lane like settling units do:
		// plain path following, real capsule collision, no local avoidance steering.
		if (!MoveGroup->bLaneLayoutDone) { MoveGroup->FinalizeLaneLayout(); }
		if (MoveGroup->bLaneEntry)
		{
			if (!bLaneSeparationOff && Crowd)
			{
				StopAIMovement();
				Crowd->SetFormationSettling(true);
				bLaneSeparationOff = true;
			}
			MoveGroup->PublishMemberDepth(GroupMember, Location);
			TickLaneEntry();
			return;
		}
	}
	const bool bWasFormationStalled=bFormationStalled;
	bFormationStalled = bGroupApproachStarted
		&& FormationProgress.Observe(CurrentOrder.TargetLocation, GetRemainingFormationDistance(), Now);
	if(bFormationStalled && !bWasFormationStalled)
	{
		UE_LOG(LogRTS,Display,TEXT("RTS_MOVE_STALL unit=%s location=%s goal=%s remaining=%.1f slot=%d yielding=%d mode=%d speed=%.1f velocity=%.1f"),
			*GetName(),*Location.ToCompactString(),*CurrentOrder.TargetLocation.ToCompactString(),GetRemainingFormationDistance(),GroupSlot,
			IsYieldingToTraffic(),static_cast<int32>(GetCharacterMovement()->MovementMode),GetCharacterMovement()->MaxWalkSpeed,GetVelocity().Size2D());
	}
	if (Now - GroupMoveStarted > GroupMoveTimeout)
	{
		UE_LOG(LogRTS, Warning, TEXT("%s: group move timed out; slot %d was not reached."), *GetName(), GroupSlot);
		StopAIMovement();
		LeaveMoveGroup();
		FinishOrder();
		return;
	}

	if (GroupSlot == INDEX_NONE)
	{
		if (!MoveGroup->CanApproach(GetFormationUnitType()))
		{
			if (Now >= NextGroupMoveAttempt)
			{
				if (!bGroupApproachStarted || bIdle || !CurrentOrder.TargetLocation.Equals(GroupWaitingPoint, 20.0))
				{
					CurrentOrder.TargetLocation = GroupWaitingPoint;
					FormationWaypoints.Reset();
					RequestMoveTo(GroupWaitingPoint, 20.0f);
					bGroupApproachStarted = true;
				}
				NextGroupMoveAttempt = Now + 0.5;
			}
			return;
		}
		// Strict phase boundary: neither a blocked path nor an early success is a
		// reason to allocate a slot while the unit is still outside the rectangle.
		if (MoveGroup->IsInsideClaimArea(Location))
		{
			if (!RTSLocalFormationPath::TryBeginPlanning(this)) { return; }
			GroupSlot = MoveGroup->ClaimSlot(Location, GetFormationUnitType());
			if (GroupSlot != INDEX_NONE)
			{
				SlotClaimLocation = Location;
				CurrentOrder.TargetLocation = MoveGroup->Slots[GroupSlot];
				StopAIMovement();
				if (Crowd) { Crowd->SetFormationSettling(true); }
				RTSLocalFormationPath::Find(this, CurrentOrder.TargetLocation, FormationWaypoints);
				if (FormationWaypoints.IsEmpty()) { FormationWaypoints.Add(CurrentOrder.TargetLocation); }
				if (RequestMoveTo(FormationWaypoints[0], SlotAcceptanceRadius)) { BeginFormationRoute(); }
				bGroupApproachStarted = true;
				NextGroupMoveAttempt = Now + 0.5;
				return;
			}
		}
		// Consume committed local waypoints every frame; only route recomputation is throttled.
		if (FormationWaypoints.Num() > 1 && FVector::DistSquared2D(Location, FormationWaypoints[0]) <= FMath::Square(SlotTolerance))
		{
			FormationWaypoints.RemoveAt(0);
			RequestMoveTo(FormationWaypoints[0], SlotAcceptanceRadius);
		}
		if (Now >= NextGroupMoveAttempt)
		{
			const int32 Hint = MoveGroup->FindFreeSlot(Location, GetFormationUnitType());
			FVector Approach = Hint == INDEX_NONE ? MoveGroup->Center : MoveGroup->Slots[Hint];
			if (MoveGroup->bCoordinateEntry && Hint != INDEX_NONE)
			{
				const double Depth = FVector::DotProduct(MoveGroup->Slots[Hint] - MoveGroup->Center, MoveGroup->Forward);
				Approach = MoveGroup->Center + MoveGroup->Forward * Depth
					- MoveGroup->Right * (MoveGroup->HalfWidth + MoveGroup->ClaimMargin - 12.0f);
			}
			const bool bRouteCommitted = !FormationWaypoints.IsEmpty() && !bFormationStalled && !bIdle;
			const bool bChanged = !bRouteCommitted && (!bGroupApproachStarted || !CurrentOrder.TargetLocation.Equals(Approach, 20.0));
			if (bRouteCommitted) { Approach = CurrentOrder.TargetLocation; }
			const bool bStalled = bGroupApproachStarted && bFormationStalled;
			const bool bEntryRoute = (MoveGroup->bCoordinateEntry || MoveGroup->bCompactLayout) && FormationWaypoints.IsEmpty()
				&& FVector::DistSquared2D(Location, Approach) < FMath::Square(1000.0);
			if (bChanged || bIdle || bStalled || bEntryRoute)
			{
				if (!RTSLocalFormationPath::TryBeginPlanning(this)) { return; }
				if (bChanged) { FormationWaypoints.Reset(); }
				// Route around parked rows. A validated local detour can use precise
				// following even before claiming; Crowd may otherwise reject its short corners.
				if (MoveGroup->bCoordinateEntry && FVector::DistSquared2D(Location, Approach) < FMath::Square(1200.0))
				{
					RTSLocalFormationPath::Find(this, Approach, FormationWaypoints, MoveGroup.Get());
				}
				if (MoveGroup->bCompactLayout && !MoveGroup->bCoordinateEntry
					&& FVector::DistSquared2D(Location, Approach) < FMath::Square(1000.0))
				{
					RTSLocalFormationPath::Find(this, Approach, FormationWaypoints);
				}
				if (!MoveGroup->bCoordinateEntry && bStalled && FVector::DistSquared2D(Location, Approach) < FMath::Square(1000.0)
					&& !RTSLocalFormationPath::Find(this, Approach, FormationWaypoints))
				{
					double BestLength = TNumericLimits<double>::Max();
					TArray<FVector> BestPath;
					for (int32 FrontSign : {-1, 1})
					{
						for (int32 SideSign : {-1, 1})
						{
							const FVector Entry = MoveGroup->Center
								+ MoveGroup->Forward * FrontSign * (MoveGroup->HalfDepth + MoveGroup->ClaimMargin - 8.0f)
								+ MoveGroup->Right * SideSign * (MoveGroup->HalfWidth + MoveGroup->ClaimMargin - 8.0f);
							TArray<FVector> Path;
							if (!RTSLocalFormationPath::Find(this, Entry, Path)) { continue; }
							double Length = 0.0;
							FVector Previous = Location;
							for (const FVector& Point : Path) { Length += FVector::Dist2D(Previous, Point); Previous = Point; }
							if (Length < BestLength) { BestLength = Length; BestPath = MoveTemp(Path); }
						}
					}
					FormationWaypoints = MoveTemp(BestPath);
				}
				// A compact slot can fit the real capsule but fail the local graph's
				// extra 3cm margin. Crowd must not stop outside an otherwise clear slot.
				if (MoveGroup->bCompactLayout && FormationWaypoints.IsEmpty())
				{
					const float Height = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
					const FVector End(Approach.X, Approach.Y, Location.Z + 3);
					FCollisionQueryParams Query(SCENE_QUERY_STAT(CompactPhysicalEntry), false, this);
					FVector NavHit;
					FNavLocation Projected;
					UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
					if (Nav && Nav->ProjectPointToNavigation(Approach, Projected, FVector(15, 15, 60))
						&& FVector::Dist2D(Approach, Projected.Location) <= 10
						&& FMath::Abs(Approach.Z - Projected.Location.Z) <= 20
						&& !UNavigationSystemV1::NavigationRaycast(GetWorld(), Location - FVector(0, 0, Height), Projected.Location, NavHit, nullptr, GetController())
						&& !GetWorld()->SweepTestByChannel(Location + FVector(0, 0, 3), End, FQuat::Identity, ECC_Pawn,
							FCollisionShape::MakeCapsule(GetSimpleCollisionRadius(), Height - 2), Query)
						&& !GetWorld()->OverlapBlockingTestByChannel(End, FQuat::Identity, ECC_Pawn,
							FCollisionShape::MakeCapsule(GetSimpleCollisionRadius(), Height - 2), Query))
					{ FormationWaypoints.Add(Approach); }
				}
				if (Crowd && (bStalled || (MoveGroup->bCompactLayout && !MoveGroup->bCoordinateEntry)) && !FormationWaypoints.IsEmpty())
				{
					StopAIMovement();
					Crowd->SetFormationSettling(true);
				}
				bGroupApproachStarted = true;
				CurrentOrder.TargetLocation = Approach;
				if (RequestMoveTo(FormationWaypoints.IsEmpty() ? Approach : FormationWaypoints[0],
					MoveGroup->bCoordinateEntry || !FormationWaypoints.IsEmpty() ? SlotAcceptanceRadius : 20.0f)) { BeginFormationRoute(); }
			}
			NextGroupMoveAttempt = MoveGroup->bCompactLayout ? Now + 0.2 : Now + 1.0 + (GetUniqueID() % 5) * 0.07;
		}
		return;
	}

	const FVector Slot = MoveGroup->Slots[GroupSlot];
	if (FVector::DistSquared2D(Location, Slot) <= FMath::Square(SlotTolerance)
		&& FMath::Abs(Location.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Slot.Z) < 80.0f)
	{
		StopAIMovement();
		GetCharacterMovement()->StopMovementImmediately();
		if (Crowd) { Crowd->SetCrowdSimulationState(ECrowdSimulationState::ObstacleOnly); }
		// Retain the group and reservation: later arrivals cannot claim this coordinate.
		MoveGroup->MarkSettled(GroupSlot);
		if (YieldPhase == 3) { YieldPhase = 0; YieldRequester.Reset(); }
		FinishOrder();
		return;
	}
	if (!FormationWaypoints.IsEmpty()
		&& FVector::DistSquared2D(Location, FormationWaypoints[0]) <= FMath::Square(SlotTolerance))
	{
		FormationWaypoints.RemoveAt(0);
		if (!FormationWaypoints.IsEmpty()) { RequestMoveTo(FormationWaypoints[0], SlotAcceptanceRadius); }
		return;
	}
	// Refresh the local detour only when a path was blocked, not on every tick.
	if ((bIdle || bFormationStalled) && Now >= NextGroupMoveAttempt)
	{
		if (!RTSLocalFormationPath::TryBeginPlanning(this)) { return; }
		bool bDirect = false;
		if (MoveGroup->bCompactLayout)
		{
			const float Height = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
			UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
			FNavLocation Projected; FVector NavHit;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(CompactSlotDirect), false, this);
			const FVector End(Slot.X, Slot.Y, Location.Z + 3);
			const FCollisionShape Shape = FCollisionShape::MakeCapsule(GetSimpleCollisionRadius(), Height - 2);
			bDirect = Nav && Nav->ProjectPointToNavigation(Slot, Projected, FVector(15, 15, 60))
				&& FVector::Dist2D(Slot, Projected.Location) <= 10 && FMath::Abs(Slot.Z - Projected.Location.Z) <= 20
				&& !UNavigationSystemV1::NavigationRaycast(GetWorld(), Location - FVector(0, 0, Height), Projected.Location, NavHit, nullptr, GetController())
				&& !GetWorld()->SweepTestByChannel(Location + FVector(0, 0, 3), End, FQuat::Identity, ECC_Pawn, Shape, Query)
				&& !GetWorld()->OverlapBlockingTestByChannel(End, FQuat::Identity, ECC_Pawn, Shape, Query);
		}
		FormationWaypoints.Reset();
		if (!bDirect) { RTSLocalFormationPath::Find(this, Slot, FormationWaypoints); }
		if (FormationWaypoints.IsEmpty()) { FormationWaypoints.Add(Slot); }
		if (RequestMoveTo(FormationWaypoints[0], SlotAcceptanceRadius)) { BeginFormationRoute(); }
		NextGroupMoveAttempt = MoveGroup->bCompactLayout ? Now + 0.2 : Now + 1.0 + (GetUniqueID() % 5) * 0.07;
	}
}

void ARTSUnit::LeaveMoveGroup()
{
	if (MoveGroup.IsValid())
	{
		// Lane entry owns its coordinate through the member record; the legacy row
		// layout keeps releasing by type quota.
		if (GroupMember != INDEX_NONE) { MoveGroup->DeactivateMember(GroupMember); }
		if (!MoveGroup->bLaneEntry) { MoveGroup->RemoveMember(GetFormationUnitType(), GroupSlot); }
	}
	MoveGroup.Reset();
	GroupSlot = INDEX_NONE;
	GroupMember = INDEX_NONE;
	SlotClaimLocation = FVector::ZeroVector;
	GroupMoveStarted = 0.0;
	bGroupApproachStarted = false;
	NextGroupMoveAttempt = 0.0;
	FormationProgress.Reset();
	EndCongestionRecovery();
	bFormationStalled = false;
	bLaneSeparationOff = false;
	FormationWaypoints.Reset();
	FormationRouteSnapshot.Reset();
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))
		{
			// SetCrowdSimulationState requires an idle path follower.
			AI->StopMovement();
			Crowd->SetPreciseArrival(false);
			Crowd->SetCrowdSimulationState(ECrowdSimulationState::Enabled);
			Crowd->SetFormationSettling(false);
		}
	}
}

void ARTSUnit::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	EndDeadlockUnstick();
	EndTrafficPass();
	EndCongestionRecovery();
	LeaveMoveGroup();
	Super::EndPlay(EndPlayReason);
}

bool ARTSUnit::RequestBuildingAttackApproach(const ARTSBuilding* Building)
{
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav || !Building) { return false; }
	auto* AI=Cast<AAIController>(GetController());if(!AI)return false;
	const double Now=GetWorld()->GetTimeSeconds();
	if(BuildingApproachTarget.Get()==Building && AI->GetMoveStatus()==EPathFollowingStatus::Moving)
	{
		if(FVector::DistSquared2D(BuildingApproachProgress,GetActorLocation())>FMath::Square(8.)){BuildingApproachProgress=GetActorLocation();BuildingApproachProgressTime=Now;}
		if(Now-BuildingApproachProgressTime<2)return true;
	}
	const FVector Extent = ARTSBuilding::CollisionExtent(Building->GetBuildingId(), Building->GetFootprint()) * Building->GetActorScale3D().GetAbs();
	const FVector Center = Building->GetActorLocation();
	const FQuat Rotation = Building->GetActorQuat();
	const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float Half = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const float Gap = FMath::Clamp(Attack->GetRange() * .25f, 10.f, 100.f);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(RTSBuildingAttackPosition), false, this);
	// In-range wall slots are preferred. When neighbouring enemy buildings seal every
	// in-range slot, the unit walks to a reachable outer slot and approaches again.
	double Best = DBL_MAX, BestOuter = DBL_MAX; FVector Goal, OuterGoal;TArray<FVector> Candidates, OuterCandidates;
	const float MaxOuter = Attack->GetRange() + 400.f;
	for (int32 Ring=0;Ring<5;++Ring) for (int32 Side = 0; Side < 4; ++Side) for (int32 Slot = -4; Slot <= 4; ++Slot)
	{
		const float Along = Slot / 4.f;
		const float Margin=FMath::Clamp(Gap+Ring*22.f, 4.f, MaxOuter);
		const FVector Local = Side < 2 ? FVector((Side == 0 ? 1 : -1) * (Extent.X + Radius + Margin), Along * Extent.Y, 0)
			: FVector(Along * Extent.X, (Side == 2 ? 1 : -1) * (Extent.Y + Radius + Margin), 0);
		const FVector Point = Center + Rotation.RotateVector(Local); FNavLocation P;
		if (!Nav->ProjectPointToNavigation(Point, P, FVector(25,25,120)) || FVector::Dist2D(P.Location, Point) > 20
			|| FMath::Abs(P.Location.Z-Center.Z)>65) { continue; }
		const FVector LocalFeet = Rotation.UnrotateVector(P.Location-Center);
		const double Edge = FVector2D(FMath::Max(0.,FMath::Abs(LocalFeet.X)-Extent.X),FMath::Max(0.,FMath::Abs(LocalFeet.Y)-Extent.Y)).Size()-Radius;
		if (GetWorld()->OverlapBlockingTestByChannel(P.Location+FVector(0,0,Half+2),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(Radius,Half),Query)) { continue; }
		FCollisionQueryParams Sight(Query);Sight.AddIgnoredActor(Building);FHitResult Hit;
		if(GetWorld()->LineTraceSingleByChannel(Hit,P.Location+FVector(0,0,Half),URTSAttackComponent::AimLocation(Building),ECC_Visibility,Sight))continue;
		if (Edge <= Attack->GetRange()-5) { Candidates.Add(P.Location); }
		else if (Edge <= MaxOuter) { OuterCandidates.Add(P.Location); }
	}
	Candidates.Sort([this](const FVector& A,const FVector& B){return FVector::DistSquared2D(GetActorLocation(),A)<FVector::DistSquared2D(GetActorLocation(),B);});
	OuterCandidates.Sort([this](const FVector& A,const FVector& B){return FVector::DistSquared2D(GetActorLocation(),A)<FVector::DistSquared2D(GetActorLocation(),B);});
	// A short list is enough: the nearest reachable slot on either ring wins.
	Candidates.Append(OuterCandidates);
	for(int32 I=0;I<Candidates.Num() && I<12;++I)
	{
		auto* Path=Nav->FindPathToLocationSynchronously(GetWorld(),GetActorLocation(),Candidates[I],this);
		if(!Path || !Path->IsValid() || Path->IsPartial())continue;
		const FVector LocalFeet = Rotation.UnrotateVector(Candidates[I]-Center);
		const double Edge = FVector2D(FMath::Max(0.,FMath::Abs(LocalFeet.X)-Extent.X),FMath::Max(0.,FMath::Abs(LocalFeet.Y)-Extent.Y)).Size()-Radius;
		const double Score=Path->GetPathLength();
		if(Edge<=Attack->GetRange()-5){if(Score<Best){Best=Score;Goal=Candidates[I];}}
		else if(Score<BestOuter){BestOuter=Score;OuterGoal=Candidates[I];}
	}
	if(Best==DBL_MAX && BestOuter==DBL_MAX)return false;
	if(Best==DBL_MAX)Goal=OuterGoal;
	if(auto* Crowd=Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))Crowd->SetPreciseArrival(true);
	BuildingApproachTarget=const_cast<ARTSBuilding*>(Building);BuildingApproachProgress=GetActorLocation();BuildingApproachProgressTime=Now;
	return RequestMoveTo(Goal,5);
}

void ARTSUnit::TickAttackOrder(float DeltaSeconds)
{
	AActor* Target = Acquisition->ResolveAttackTarget(CurrentOrder.TargetActor.Get());
	const URTSHealthComponent* TargetHealth = Target ? Target->FindComponentByClass<URTSHealthComponent>() : nullptr;

	if (!TargetHealth || !URTSAttackComponent::IsHostileTarget(this, Target))
	{
		// Let the current swing finish visually, then go idle.
		if (!Attack->IsSwinging())
		{
			FinishOrder();
			StopAIMovement();
		}
		return;
	}

	AAIController* AI = Cast<AAIController>(GetController());
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bHasSight = Attack->HasLineOfSight(Target);

	if (Attack->IsInRange(Target) && bHasSight)
	{
		if (AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle)
		{
			AI->StopMovement();
		}
		if(AI)if(auto* Crowd=Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))Crowd->SetPreciseArrival(false);
		FaceTowards(Target->GetActorLocation(), DeltaSeconds);
		Attack->TryStartAttack(Target);
	}
	else if (!Attack->IsSwinging() && !IsHoldingAutomaticPosition() && Now >= NextRepathTime && AI)
	{
		NextRepathTime = Now + ChaseRepathInterval;
		if (const auto* Building = Cast<ARTSBuilding>(Target))
		{
			RequestBuildingAttackApproach(Building);
			return;
		}
		if(auto* Crowd=Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))Crowd->SetPreciseArrival(false);
		const float Acceptance = bHasSight ? FMath::Max(Attack->GetRange() * 0.5f - GetSimpleCollisionRadius(), 5.0f) : 5.0f;
		AI->MoveToActor(Target, Acceptance, /*bStopOnOverlap*/ true,
			/*bUsePathfinding*/ true, /*bCanStrafe*/ false, /*FilterClass*/ nullptr, /*bAllowPartialPath*/ true);
	}
}

void ARTSUnit::FaceTowards(const FVector& WorldLocation, float DeltaSeconds)
{
	const FVector Direction = (WorldLocation - GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return;
	}
	const FRotator Desired(0.0, Direction.Rotation().Yaw, 0.0);
	SetActorRotation(FMath::RInterpConstantTo(GetActorRotation(), Desired, DeltaSeconds, 720.0f));
}

void ARTSUnit::StopAIMovement()
{
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
	}
}

void ARTSUnit::UpdateAnimation()
{
	const double Now = GetWorld()->GetTimeSeconds();

	ERTSUnitAnimState Desired = ERTSUnitAnimState::Idle;
	if (Now < AttackAnimEndTime)
	{
		Desired = ERTSUnitAnimState::Attack;
	}
	else if (GetVelocity().SizeSquared2D() > MovingSpeedSq)
	{
		Desired = ERTSUnitAnimState::Move;
	}
	else if (Gatherer->IsWorking())
	{
		Desired = ERTSUnitAnimState::Attack;
		if (AnimState == ERTSUnitAnimState::Attack && !GetMesh()->IsPlaying())
		{ PlayAnimState(ERTSUnitAnimState::Attack); }
	}

	if (Desired != AnimState)
	{
		PlayAnimState(Desired);
	}
}

void ARTSUnit::PlayAnimState(ERTSUnitAnimState NewState)
{
	AnimState = NewState;
	if (!UnitData)
	{
		return;
	}

	UAnimSequence* Anim = nullptr;
	bool bLoop = true;
	switch (NewState)
	{
	case ERTSUnitAnimState::Idle:   Anim = UnitData->IdleAnim; break;
	case ERTSUnitAnimState::Move:   Anim = UnitData->MoveAnim; break;
	case ERTSUnitAnimState::Attack: Anim = UnitData->AttackAnim; bLoop = false; break;
	case ERTSUnitAnimState::Death:  Anim = UnitData->DeathAnim; bLoop = false; break;
	default: break;
	}

	if (Anim)
	{
		GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		GetMesh()->PlayAnimation(Anim, bLoop);
	}
}

void ARTSUnit::HandleAttackStarted(AActor* Target)
{
	EndDeadlockUnstick();
	const float Length = (UnitData && UnitData->AttackAnim) ? UnitData->AttackAnim->GetPlayLength() : 0.5f;
	AttackAnimEndTime = GetWorld()->GetTimeSeconds() + Length;
	// Force a restart even if the previous swing animation is still playing.
	PlayAnimState(ERTSUnitAnimState::Attack);
}

void ARTSUnit::HandleDied(URTSHealthComponent* HealthComp, AActor* Killer)
{
	EndDeadlockUnstick();
	EndTrafficPass();
	EndCongestionRecovery();
	OrderQueue.Reset();
	ClearAttackMoveGoal();
	CurrentOrder = FRTSOrder();
	Attack->CancelAttack();
	StopAIMovement();

	UCharacterMovementComponent* Move = GetCharacterMovement();
	LeaveMoveGroup();

	Move->SetAvoidanceEnabled(false);
	Move->DisableMovement();
	// Corpses must not keep pushing living crowd agents around.
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(AI->GetPathFollowingComponent()))
		{
			Crowd->SetCrowdSimulationState(ECrowdSimulationState::Disabled);
		}
	}
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	SetSelected(false);
	PlayAnimState(ERTSUnitAnimState::Death);
	OnUnitDied.Broadcast(this);
	SetLifeSpan(CorpseLifetime);
}

void ARTSUnit::TickLaneEntry()
{
	ARTSUnitAIController* AI = Cast<ARTSUnitAIController>(GetController());
	if (!AI) { return; }
	URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent());
	const double Now = GetWorld()->GetTimeSeconds();
	const FVector Location = GetActorLocation();
	const bool bIdle = AI->GetMoveStatus() == EPathFollowingStatus::Idle;
	if (Now - GroupMoveStarted > GroupMoveTimeout)
	{
		UE_LOG(LogRTS, Warning, TEXT("%s: lane %d timed out; slot %d was not reached."), *GetName(), GetFormationLane(), GroupSlot);
		StopAIMovement();
		LeaveMoveGroup();
			FinishOrder();
		return;
	}
	const int32 Lane = GetFormationLane();
	const int32 Planned = MoveGroup->PlannedSlotOf(GroupMember);
	if (!MoveGroup->Slots.IsValidIndex(Planned))
	{
		UE_LOG(LogRTS, Warning, TEXT("%s: lane %d has no planned coordinate."), *GetName(), Lane);
		StopAIMovement();
		LeaveMoveGroup();
			FinishOrder();
		return;
	}

	if (GroupSlot == INDEX_NONE)
	{
		const int32 Claimed = MoveGroup->ClaimPlannedSlot(GroupMember, Location);
		if (Claimed != INDEX_NONE)
		{
			GroupSlot = Claimed;
			SlotClaimLocation = Location;
			CurrentOrder.TargetLocation = MoveGroup->Slots[Claimed];
			StopAIMovement();
			if (Crowd) { Crowd->SetFormationSettling(true); }
			RequestMoveTo(CurrentOrder.TargetLocation, SlotAcceptanceRadius);
			bGroupApproachStarted = true;
			FormationProgress.Reset();
			NextLaneRequest = Now + 0.5;
			return;
		}
		// Queue in the lane: advance only as far as the follower limit, and never
		// reverse or overshoot the row that is still reserved for this unit.
		const float MyDepth = static_cast<float>(FVector::DotProduct(Location - MoveGroup->Center, MoveGroup->Forward));
		const float Limit = FMath::Min(MoveGroup->LaneAdvanceLimit(GroupMember), MoveGroup->SlotDepth(Planned));
		const float TargetDepth = FMath::Max(MyDepth, Limit);
		const FVector Target = MoveGroup->LanePoint(Lane, TargetDepth);
		const bool bTargetMoved = !CurrentOrder.TargetLocation.Equals(Target, 15.0);
		if ((bIdle || bTargetMoved) && Now >= NextLaneRequest)
		{
			CurrentOrder.TargetLocation = Target;
			RequestMoveTo(Target, 20.0f);
			NextLaneRequest = Now + 0.3;
		}
		return;
	}

	const FVector Slot = MoveGroup->Slots[GroupSlot];
	if (FVector::DistSquared2D(Location, Slot) <= FMath::Square(SlotTolerance)
		&& FMath::Abs(Location.Z - GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - Slot.Z) < 80.0f)
	{
		StopAIMovement();
		GetCharacterMovement()->StopMovementImmediately();
		if (Crowd) { Crowd->SetCrowdSimulationState(ECrowdSimulationState::ObstacleOnly); }
		// Retain the group and reservation: later arrivals cannot claim this coordinate.
		MoveGroup->MarkSettled(GroupSlot);
		if (YieldPhase == 3) { YieldPhase = 0; YieldRequester.Reset(); }
		FinishOrder();
		return;
	}
	if (bIdle && Now >= NextLaneRequest)
	{
		CurrentOrder.TargetLocation = Slot;
		RequestMoveTo(Slot, SlotAcceptanceRadius);
		NextLaneRequest = Now + 0.3;
	}
}

void ARTSUnit::TickDeadlockUnstick()
{
	const double Now = GetWorldTimeForTraffic();
	const FVector Location = GetActorLocation();
	const AAIController* UnitController = Cast<AAIController>(GetController());
	const UPathFollowingComponent* Follow = UnitController ? UnitController->GetPathFollowingComponent() : nullptr;
	const bool bTravelOrder = CurrentOrder.Type == ERTSOrderType::Move || CurrentOrder.Type == ERTSOrderType::Gather
		|| CurrentOrder.Type == ERTSOrderType::Build || CurrentOrder.Type == ERTSOrderType::ReturnResources;
	const bool bYieldTravel = YieldPhase == 1 || YieldPhase == 3;
	const bool bMoving = UnitController && UnitController->GetMoveStatus() == EPathFollowingStatus::Moving;
	const bool bEligible = IsAlive() && (bTravelOrder || bYieldTravel) && bMoving && YieldPhase != 2
		&& GetCharacterMovement()->MaxWalkSpeed > 0 && GetCharacterMovement()->MovementMode != MOVE_None
		&& !TrafficPassPartner.IsValid() && !(Attack && Attack->IsSwinging())
		&& !(Gatherer && (Gatherer->IsWorking() || Gatherer->IsWaitingForWorkPosition()));
	if (!bUnstickActive)
	{
		if (!bEligible) { UnstickAnchor = Location; UnstickLastMoveTime = Now; return; }
		if (FVector::Dist2D(Location, UnstickAnchor) > 20.0)
		{ UnstickAnchor = Location; UnstickLastMoveTime = Now; return; }
		if (Now - UnstickLastMoveTime < 2.0 || Now < UnstickCooldownUntil) { return; }
	}
	bool bNeighbour = false;
	bool bOverlapping = false;
	bool bHostile = false;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (*It == this || !It->IsAlive() || FMath::Abs(It->GetActorLocation().Z - Location.Z) > 100) { continue; }
		const double Distance = FVector::Dist2D(Location, It->GetActorLocation());
		const double Combined = GetSimpleCollisionRadius() + It->GetSimpleCollisionRadius();
		bOverlapping |= Distance < Combined + 2;
		bNeighbour |= It->GetTeam() == GetTeam() && Distance < Combined + 160;
		bHostile |= It->GetTeam() != GetTeam() && Distance < 400;
	}
	if (bUnstickActive)
	{
		if (!bEligible || bHostile || Now >= UnstickUntil
			|| (!bOverlapping && FVector::Dist2D(Location, UnstickAnchor) >= 60.0)) { EndDeadlockUnstick(); }
		else { UnstickLastMoveTime = Now; return; }
	}
	if (!bEligible || bHostile || !bNeighbour)
	{ UnstickAnchor = Location; UnstickLastMoveTime = Now; return; }
	if (FVector::Dist2D(Location, UnstickAnchor) > 20.0)
	{ UnstickAnchor = Location; UnstickLastMoveTime = Now; return; }
	if (Now - UnstickLastMoveTime < 2.0 || Now < UnstickCooldownUntil) { return; }
	// A partial route or an invalid destination must never grant a collision waiver.
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const FVector Feet = Location - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	UNavigationPath* Path = Nav && Follow ? Nav->FindPathToLocationSynchronously(GetWorld(), Feet, Follow->GetPathDestination()) : nullptr;
	UnstickCooldownUntil = Now + 3.0;
	if (!Path || !Path->IsValid() || Path->IsPartial()) { return; }
	BeginDeadlockUnstick();
}

void ARTSUnit::BeginDeadlockUnstick()
{
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	if (!Capsule) { return; }
	const double Now = GetWorldTimeForTraffic();
	bUnstickActive = true;
	++UnstickCount;
	UnstickAnchor = GetActorLocation();
	UnstickLastMoveTime = Now;
	UnstickUntil = Now + 0.75;
	UnstickCooldownUntil = Now + 3.0;
	UnstickPawnResponse = Capsule->GetCollisionResponseToChannel(ECC_Pawn);
	// Only unit blocking is waived. World/static/building responses remain unchanged.
	Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{ bUnstickHadRVO = Move->bUseRVOAvoidance; Move->SetAvoidanceEnabled(false); }
	if (const AAIController* UnitAI = Cast<AAIController>(GetController()))
	{
		if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(UnitAI->GetPathFollowingComponent()))
		{ Crowd->SetDeadlockTravel(true); }
	}
	UE_LOG(LogRTS, Display, TEXT("RTS_UNSTICK unit=%s yield=%d count=%u"), *GetName(), YieldPhase, UnstickCount);
}

void ARTSUnit::EndDeadlockUnstick()
{
	if (!bUnstickActive) { return; }
	bUnstickActive = false;
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{ Capsule->SetCollisionResponseToChannel(ECC_Pawn, UnstickPawnResponse); }
	if (UCharacterMovementComponent* Move = GetCharacterMovement()) { Move->SetAvoidanceEnabled(bUnstickHadRVO); }
	if (const AAIController* UnitAI = Cast<AAIController>(GetController()))
	{
		if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(UnitAI->GetPathFollowingComponent()))
		{ Crowd->SetDeadlockTravel(false); }
	}
	UnstickAnchor = GetActorLocation();
	UnstickLastMoveTime = GetWorldTimeForTraffic();
}
