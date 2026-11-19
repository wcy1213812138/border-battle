#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Economy/RTSResourceComponent.h"
#include "Core/RTSTypes.h"
#include "Buildings/RTSBuilding.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Units/RTSUnit.h"
#include "Orders/RTSOrderLibrary.h"
#include "Orders/RTSLocalFormationPath.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "Match/RTSMatchSubsystem.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignProgression.h"

URTSGathererComponent::URTSGathererComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}
void URTSGathererComponent::SetStatus(const FString& Text)
{
	if (Status != Text) { Status = Text; OnWorkChanged.Broadcast(); }
}
FRTSGathererSnapshot URTSGathererComponent::CaptureEconomy() const
{
	FRTSGathererSnapshot S;S.Carried=Carried;S.CargoType=CargoType;S.bReturning=bReturning;S.WorkElapsed=WorkElapsed;return S;
}
bool URTSGathererComponent::IsValidEconomy(const FRTSGathererSnapshot& S,int32 Capacity)
{
	return Capacity>=0 && S.Carried>=0 && S.Carried<=Capacity && S.CargoType<ERTSResource::Count && FMath::IsFinite(S.WorkElapsed) && S.WorkElapsed>=0 && S.WorkElapsed<=3600;
}
bool URTSGathererComponent::RestoreEconomy(const FRTSGathererSnapshot& S)
{
	const auto* Unit=Cast<ARTSUnit>(GetOwner());
	if(!Unit || !Unit->IsAlive() || !Unit->GetUnitData() || !Unit->GetUnitData()->bWorker || !IsValidEconomy(S,Unit->GetUnitData()->CarryCapacity))return false;
	ResetApproach();Carried=S.Carried;CargoType=S.CargoType;bReturning=S.bReturning;WorkElapsed=S.WorkElapsed;bWorking=false;
	SetStatus(Carried>0?TEXT("携货任务已恢复"):TEXT("工作任务已恢复，等待工位"));OnWorkChanged.Broadcast();return true;
}
void URTSGathererComponent::NotifyNewOrder()
{
	bWorking = false; bReturning = false; WorkElapsed = 0;
	bWaitingForNodeSlot = false;
	ResetApproach();
	SetStatus(TEXT("空闲"));
}
ARTSBuilding* URTSGathererComponent::FindDropOff() const
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	ARTSBuilding* Best = nullptr;
	double Distance = TNumericLimits<double>::Max();
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
  if(auto* C=ARTSCampaign::Get(this);C && C->GetActorFaction(Unit)!=C->GetActorFaction(*It))continue;
		const double D = FVector::DistSquared2D(Unit->GetActorLocation(), It->GetActorLocation());
		if (It->GetTeam() == Unit->GetTeam() && It->IsDropOff() && D < Distance) { Best = *It; Distance = D; }
	}
	return Best;
}
void URTSGathererComponent::ResetApproach()
{
	if (bWorkPreciseArrival)
	{
		if (const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner()))
		{
			if (const AAIController* AI = Cast<AAIController>(Unit->GetController()))
			{
				if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))
				{ Crowd->SetPreciseArrival(false); }
			}
		}
	}
	bWorkPreciseArrival = false;
	ApproachTarget.Reset(); ApproachPath.Reset(); NextPlanTime = 0;
	bHasWorkPosition = false; bWaitingForWorkPosition = false;
	BestStepDistance = TNumericLimits<double>::Max();
	AvoidBlockedWorkUntil = 0;
}

bool URTSGathererComponent::RequestApproachStep()
{
	const ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	AAIController* AI = Unit ? Cast<AAIController>(Unit->GetController()) : nullptr;
	if (!AI || ApproachPath.IsEmpty()) { return false; }
	if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))
	{ Crowd->SetPreciseArrival(true); bWorkPreciseArrival = true; }
	LastProgressTime = GetWorld()->GetTimeSeconds();
	BestStepDistance = FVector::Dist2D(Unit->GetActorLocation(), ApproachPath[0]);
	// Intermediate turns are handed off before arrival, while Crowd remains active.
	FAIMoveRequest Request(ApproachPath[0]);
	Request.SetAcceptanceRadius(5);
	Request.SetReachTestIncludesAgentRadius(false);
	Request.SetAllowPartialPath(false);
	Request.SetUsePathfinding(true);
	return AI->MoveTo(Request).Code != EPathFollowingRequestResult::Failed;
}

bool URTSGathererComponent::Approach(AActor* Target, float Radius, bool bOuterWaitOnly)
{
	ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	AAIController* AI = Unit ? Cast<AAIController>(Unit->GetController()) : nullptr;
	if (!AI || !Target) { ResetApproach(); return false; }
	const ARTSResourceNode* Resource = Cast<ARTSResourceNode>(Target);
	const bool bFood = Resource && Resource->GetResourceType() == ERTSResource::Food;
	const float ExtraReach = FMath::Clamp(Resource ? (bFood ? FoodReachExtra : WoodReachExtra) : BuildingReachExtra, 0.f, 200.f);
	Radius += ExtraReach;
	const float PositionTolerance = bFood ? 85.f : 60.f;
	const FVector Location = Unit->GetActorLocation();
	const FVector Delta = Location - Target->GetActorLocation();
	const double Now = GetWorld()->GetTimeSeconds();
	// Work orders use this planner rather than ARTSUnit::RequestMoveTo. Repair a
	// missing start tile without moving the worker or loosening any collision.
	if (AI->GetMoveStatus() == EPathFollowingStatus::Idle && Delta.Size2D() > Radius + Unit->GetSimpleCollisionRadius() + 45)
	{ Unit->TryRefreshMissingNavigation(); }
	if (ApproachTarget.Get() != Target || !ApproachTargetLocation.Equals(Target->GetActorLocation(), 30))
	{
		ResetApproach(); ApproachTarget = Target; ApproachTargetLocation = Target->GetActorLocation();
	}
	if (bOuterWaitOnly && bHasWorkPosition && !bWaitingForWorkPosition) { bHasWorkPosition=false;ApproachPath.Reset();NextPlanTime=0; }
	// Occupy a stable, exclusive working position, not the shared target centre.
	if (!bOuterWaitOnly && bHasWorkPosition && !bWaitingForWorkPosition && FVector::Dist2D(Location, WorkPosition) <= PositionTolerance
		&& Delta.Size2D() <= Radius + Unit->GetSimpleCollisionRadius() + 45 && FMath::Abs(Delta.Z) < 180)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSWorkSight), false, Unit);
		Params.AddIgnoredActor(Target);
		FHitResult Hit;
		FCollisionObjectQueryParams Obstacles;
		Obstacles.AddObjectTypesToQuery(ECC_WorldStatic); Obstacles.AddObjectTypesToQuery(ECC_WorldDynamic);
		if (!GetWorld()->LineTraceSingleByObjectType(Hit, Location, Target->GetActorLocation() + FVector(0, 0, 80), Obstacles, Params))
		{ AI->StopMovement(); ApproachPath.Reset(); LastProgressTime = Now; return true; }
		// The old position became obstructed. Release it rather than walking back to
		// the same unusable point indefinitely.
		bHasWorkPosition=false;ApproachPath.Reset();NextPlanTime=0;
	}
	if (!ApproachPath.IsEmpty())
	{
		// At 350 cm/s this hands off ~45 cm before a turn, rather than braking to zero.
		const double Handoff = FMath::Max(30.0, Unit->GetVelocity().Size2D() * 0.13);
		bool bAdvanced = false;
		while (ApproachPath.Num() > 1 && FVector::Dist2D(Location, ApproachPath[0]) <= Handoff)
		{ ApproachPath.RemoveAt(0); bAdvanced = true; }
		if (bAdvanced)
		{
			if (RequestApproachStep()) { return false; }
			ApproachPath.Reset();
		}
		else
		{
			const double Distance = FVector::Dist2D(Location, ApproachPath[0]);
			if (Distance < BestStepDistance - 8) { BestStepDistance = Distance; LastProgressTime = Now; }
			const bool bMoving = AI->GetMoveStatus() == EPathFollowingStatus::Moving;
			if (bMoving && Now - LastProgressTime < 1.5) { return false; }
			if (bWaitingForWorkPosition && Now < NextPlanTime) { return false; }
			// A genuine stalled/failed path is replanned, not every movement tick.
			if (Now - LastProgressTime > 1.5 && bHasWorkPosition)
			{
				BlockedWorkPosition = WorkPosition; AvoidBlockedWorkUntil = Now + 4;
				bHasWorkPosition = false;
			}
			ApproachPath.Reset();
		}
	}
	if (Now < NextPlanTime) { return false; }
	NextPlanTime = Now + 0.65 + (Unit->GetUniqueID() % 7) * 0.035;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav) { return false; }
	auto Reserved = [&](const FVector& Point)
	{
		for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
		{
			if (*It == Unit || !It->IsAlive()) { continue; }
			const URTSGathererComponent* Other = It->GetGatherer();
			// Physical work space is exclusive even when workers approach different nearby targets.
			if (Other && Other->bHasWorkPosition && FMath::Abs(Other->WorkPosition.Z - Point.Z) < 100.f
				&& FVector::Dist2D(Other->WorkPosition, Point) < Unit->GetSimpleCollisionRadius() + It->GetSimpleCollisionRadius() + 18) { return true; }
		}
		return false;
	};
	auto Route = [&](const FVector& Point, TArray<FVector>& PathPoints)
	{
		UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), Location, Point, Unit);
		if (!Path || !Path->IsValid() || Path->IsPartial()) { return false; }
		PathPoints = Path->PathPoints;
		if (!PathPoints.IsEmpty()) { PathPoints.RemoveAt(0); }
		if (PathPoints.IsEmpty()) { PathPoints.Add(Point); }
		return true;
	};
	if (bHasWorkPosition && !bWaitingForWorkPosition && Now - LastProgressTime > 3.0)
	{ bHasWorkPosition = false; }
	const bool bWasWaiting = bWaitingForWorkPosition;
	if (!bHasWorkPosition || bWasWaiting)
	{
		// Collect the near work slots, then test each one against the real capsule
		// detour. Picking only the straight-line nearest slot made a worker loop
		// forever when one stationary neighbour blocked that single corridor.
		struct FCandidate { double Score; FVector Position; };
		TArray<FCandidate> Inner, Outer;
		// Prefer the inner work ring; overflow workers wait outside the traffic apron.
		// Evaluate both rings: a wedged worker may need the outer apron even when
		// inner slots exist, because its corridor to them is blocked by a neighbour.
		for (int32 Ring = 0; Ring < 2; ++Ring)
		{
			const double WorkRadius = Radius + Unit->GetSimpleCollisionRadius() + (Ring == 0 ? 32 : 220);
			const int32 Positions = Cast<ARTSBuilding>(Target) ? 32 : FMath::Max(6, FMath::FloorToInt(2 * PI * WorkRadius / 105));
			for (int32 i = 0; i < Positions; ++i)
			{
				const double Angle = 2 * PI * i / Positions;
				FVector Offset = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * WorkRadius;
				if (const ARTSBuilding* Building = Cast<ARTSBuilding>(Target))
				{
					const FVector Extent = ARTSBuilding::CollisionExtent(Building->GetBuildingId(), Building->GetFootprint());
					const FVector Direction(FMath::Cos(Angle), FMath::Sin(Angle), 0);
					const double EdgeMargin = 100.0 + ExtraReach * 0.5;
					const double Scale = 1.0 / FMath::Max(FMath::Abs(Direction.X) / (Extent.X + EdgeMargin), FMath::Abs(Direction.Y) / (Extent.Y + EdgeMargin));
					Offset = Building->GetActorRotation().RotateVector(Direction * (Scale + (Ring == 1 ? 190 : 0)));
				}
				const FVector Point = Target->GetActorLocation() + Offset;
				FNavLocation P;
				if (!Nav->ProjectPointToNavigation(Point, P, FVector(20, 20, 160)) || FVector::Dist2D(Point, P.Location) > 15
					|| FVector::Dist2D(P.Location, Target->GetActorLocation()) > (Ring == 0 && Cast<ARTSBuilding>(Target) ? Radius + Unit->GetSimpleCollisionRadius() + 40 : WorkRadius + 15)
					|| FMath::Abs(P.Location.Z - (Target->GetActorLocation().Z - (Cast<ARTSResourceNode>(Target) ? 90 : 0))) > 60
					|| Reserved(P.Location)) { continue; }
				if (Now < AvoidBlockedWorkUntil && FVector::DistSquared2D(P.Location,BlockedWorkPosition)<FMath::Square(110.)) { continue; }
				FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSWorkPosition), false, Unit);
				if (GetWorld()->OverlapBlockingTestByChannel(P.Location + FVector(0, 0, 82), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(42, 78), Params)) { continue; }
				FCollisionQueryParams SightParams(SCENE_QUERY_STAT(RTSWorkCandidateSight), false, Unit);
				SightParams.AddIgnoredActor(Target);
				FCollisionObjectQueryParams SightObjects;SightObjects.AddObjectTypesToQuery(ECC_WorldStatic);SightObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
				FHitResult SightHit;
				if(GetWorld()->LineTraceSingleByObjectType(SightHit,P.Location+FVector(0,0,Unit->GetSimpleCollisionHalfHeight()),Target->GetActorLocation()+FVector(0,0,80),SightObjects,SightParams))continue;
				(Ring == 0 ? Inner : Outer).Add({ FVector::DistSquared2D(Location, P.Location), P.Location });
			}
		}
		Inner.Sort([](const FCandidate& A, const FCandidate& B){ return A.Score < B.Score; });
		Outer.Sort([](const FCandidate& A, const FCandidate& B){ return A.Score < B.Score; });
		TArray<FCandidate> Order;
		const int32 InnerCount = bOuterWaitOnly ? 0 : Inner.Num();
		if (!bOuterWaitOnly) Order = Inner;
		Order.Append(Outer);
		if (Order.IsEmpty())
		{
			bWaitingForWorkPosition = true; AI->StopMovement(); return false;
		}
		// Test several slots, not just the nearest: the nearest one may be behind a
		// stationary collector. Outer slots are the documented back-out position.
		int32 Solved = INDEX_NONE;
		for (int32 Index = 0; Index < Order.Num() && Index < 6; ++Index)
		{
			TArray<FVector> Detour;
			if (!RTSLocalFormationPath::TryBeginPlanning(Unit)) { ApproachPath.Reset(); AI->StopMovement(); NextPlanTime = Now + .15; return false; }
			const bool bLocalSolved = RTSLocalFormationPath::Find(Unit, Order[Index].Position, Detour) && !Detour.IsEmpty();
			// A worker standing tens of metres away is not wedged: a failed local detour there
			// only means the corridor is longer than the local planner covers, so follow the
			// real navmesh route instead of stepping sideways (the reported wrong-direction walk
			// to the high-ground mines).
			const bool bFarFromSlot = FVector::Dist2D(Location, Order[Index].Position) > 900.0;
			if (!bLocalSolved && !bFarFromSlot) { continue; }
			TArray<FVector> NavRoute;
			if (!Route(Order[Index].Position, NavRoute) || NavRoute.IsEmpty()) { continue; }
			WorkPosition = Order[Index].Position;
			ApproachPath = bLocalSolved ? MoveTemp(Detour) : MoveTemp(NavRoute);
			bWaitingForWorkPosition = Index >= InnerCount; Solved = Index; break;
		}
		if (Solved == INDEX_NONE)
		{
			// Every candidate corridor is blocked. Step sideways around the wedged
			// neighbour first: a tangent move keeps clear of both the resource and the
			// collector, unlike retrying the same blocked line.
			const FVector Away = (Location - Target->GetActorLocation()).GetSafeNormal2D();
			if (!Away.IsNearlyZero())
			{
				const FVector Side = FVector::CrossProduct(FVector::UpVector, Away);
				for (float Sign : {1.f, -1.f})
				{
					const FVector Step = Location + Side * Sign * 160.f;
					FNavLocation P;
					if (!Nav->ProjectPointToNavigation(Step, P, FVector(25, 25, 120)) || FVector::Dist2D(Step, P.Location) > 25) { continue; }
					FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSWorkEscape), false, Unit);
					Params.AddIgnoredActor(Target);
					if (GetWorld()->SweepTestByChannel(Location + FVector(0, 0, 82), P.Location + FVector(0, 0, 82), FQuat::Identity,
						ECC_Pawn, FCollisionShape::MakeCapsule(42, 78), Params)) { continue; }
					FAIMoveRequest Request(P.Location);
					Request.SetAcceptanceRadius(15);
					Request.SetReachTestIncludesAgentRadius(false);
					Request.SetUsePathfinding(true);
					if (AI->MoveTo(Request).Code != EPathFollowingRequestResult::Failed)
					{
						BlockedWorkPosition = Order[0].Position; AvoidBlockedWorkUntil = Now + 1.5;
						bHasWorkPosition = false; ApproachPath.Reset(); NextPlanTime = Now + .8;
						return false;
					}
				}
			}
			BlockedWorkPosition = Order[0].Position; AvoidBlockedWorkUntil = Now + 1.5;
			bHasWorkPosition = false; ApproachPath.Reset(); AI->StopMovement(); NextPlanTime = Now + .3; return false;
		}
		bHasWorkPosition = true;
		if (bWaitingForWorkPosition && FVector::Dist2D(Location, WorkPosition) <= 25)
		{ AI->StopMovement(); ApproachPath.Reset(); return false; }
	}
	// No complete navmesh route to the reserved slot (e.g. a partial path across a platform edge).
	// Never stand still silently: ask for a pathfinding move that may stop at the closest reachable
	// point, log the case once a second, and retry the full plan shortly after.
	else if (ApproachPath.IsEmpty() && !Route(WorkPosition, ApproachPath))
	{
		if (Now >= NextPlanTime)
		{
			FAIMoveRequest Request(WorkPosition);
			Request.SetAcceptanceRadius(45);
			Request.SetUsePathfinding(true);
			Request.SetAllowPartialPath(true);
			const bool bAccepted = AI->MoveTo(Request).Code != EPathFollowingRequestResult::Failed;
			UE_LOG(LogRTS, Warning, TEXT("RTS_GATHER_PARTIAL_ROUTE unit=%s dist=%.0f accepted=%d"), *Unit->GetName(), FVector::Dist2D(Location, WorkPosition), bAccepted ? 1 : 0);
			LastProgressTime = Now; NextPlanTime = Now + 1.0;
		}
		return false;
	}
	// Re-plan the real-capsule detour to the reserved slot while travelling; far away the
	// navmesh route already in ApproachPath is the honest path to a different platform.
	TArray<FVector> Detour;
	const bool bLocalSolved = RTSLocalFormationPath::TryBeginPlanning(Unit) && RTSLocalFormationPath::Find(Unit, WorkPosition, Detour) && !Detour.IsEmpty();
	if (bLocalSolved) { ApproachPath = MoveTemp(Detour); }
	else if (FVector::Dist2D(Location, WorkPosition) <= 900.0)
	{
		BlockedWorkPosition=WorkPosition;AvoidBlockedWorkUntil=Now+1.5;bHasWorkPosition=false;
		ApproachPath.Reset();AI->StopMovement();NextPlanTime=Now+.3;return false;
	}
	while (ApproachPath.Num() > 1 && FVector::Dist2D(Location, ApproachPath[0]) < 20) { ApproachPath.RemoveAt(0); }
	if (!ApproachPath.IsEmpty() && !RequestApproachStep()) { ApproachPath.Reset(); }
	return false;
}
void URTSGathererComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	if (!Unit || !Unit->IsAlive() || !Unit->GetUnitData() || !Unit->GetUnitData()->bWorker) { bWorking = false; return; }
 if(auto* C=ARTSCampaign::Get(this);C && C->GetActorFaction(Unit)>=0 && (!C->IsFactionEconomyActive(C->GetActorFaction(Unit)) || C->GetGuideStep()<7)){bWorking=false;return;}
	const FRTSOrder& Order = Unit->GetCurrentOrder();
	bWorking = false;
	if (Order.Type == ERTSOrderType::Build)
	{
		ARTSBuilding* Building = Cast<ARTSBuilding>(Order.TargetActor.Get());
		if (!Building || !Building->IsAlive() || Building->GetTeam() != Unit->GetTeam() || Building->IsComplete())
		{ URTSOrderLibrary::IssueStopOrder({Unit}); return; }
		if (Approach(Building, Building->GetFootprint() * 0.71f))
		{ bWorking = true; Building->AddConstructionWork(DeltaTime); SetStatus(TEXT("正在施工")); }
		else { SetStatus(bWaitingForWorkPosition ? TEXT("等待施工位置") : TEXT("前往工地")); }
		return;
	}
	if (Order.Type != ERTSOrderType::Gather && Order.Type != ERTSOrderType::ReturnResources) { return; }
	if (Order.Type == ERTSOrderType::ReturnResources && Carried == 0) { URTSOrderLibrary::IssueStopOrder({Unit}); return; }
	ARTSResourceNode* Node = Cast<ARTSResourceNode>(Order.TargetActor.Get());
	// Campaign nodes are always harvestable (ownership only gates the front); arena nodes keep the team check.
	if(Node && !Node->CanGather(Unit->GetTeam())) { SetStatus(TEXT("该资源点属于其他队伍，采集被拒绝"));URTSOrderLibrary::IssueStopOrder({Unit});return; }
	if (Order.Type == ERTSOrderType::ReturnResources || (Node && Carried > 0 && CargoType != Node->GetResourceType())) { bReturning = true; }
	if (Carried >= Unit->GetUnitData()->CarryCapacity || !Node || Node->GetRemaining() == 0) { bReturning = Carried > 0; }
	if (bReturning)
	{
		ARTSBuilding* Drop = Cast<ARTSBuilding>(Order.TargetActor.Get());
  auto* Campaign=ARTSCampaign::Get(this);
  // An experienced worker (stage 2, level 6+) banks the load at the mine mouth instead of
  // walking every sack back to the hall, so distant nodes stay worth working. Player-only:
  // enemy crews keep their real haul-home cost.
  if (Campaign && Unit->GetTeam() == RTSTeam::Player && Carried > 0
   && Campaign->GetLevel(TEXT("Peasant")) >= 6)
  {
   if (URTSResourceComponent* Stock = Campaign->GetActorAccount(Unit))
   {
    const int32 Cap = RTSCampaignProgression::StoreCapacity(CargoType);
    const int32 Accepted = Stock->Deposit(CargoType, Carried, Cap);
    Carried -= Accepted; OnWorkChanged.Broadcast();
    if (Carried > 0) { SetStatus(TEXT("仓库已满，保留携带货物等待")); return; }
    bReturning = false;
    SetStatus(TEXT("就地入库 · 高阶工人免返程"));
    if (Order.Type == ERTSOrderType::ReturnResources || !Node || (Node->GetRemaining() == 0 && !Node->IsRenewable())) { URTSOrderLibrary::IssueStopOrder({Unit}); }
    return;
   }
  }

  if (!Drop || !Drop->IsDropOff() || Drop->GetTeam()!=Unit->GetTeam() || (Campaign && Campaign->GetActorFaction(Drop)!=Campaign->GetActorFaction(Unit))) { Drop=FindDropOff(); }
		if (!Drop) { SetStatus(TEXT("没有已建成的交付点")); return; }
		SetStatus(TEXT("运回资源"));
		if (Approach(Drop, Drop->GetFootprint() * 0.71f))
		{
			URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
   URTSResourceComponent* Stock = Campaign?Campaign->GetActorAccount(Unit):(Balance?Balance->GetResources(Unit->GetTeam()):nullptr);
			if (Stock && Carried > 0)
			{
				const int32 Cap = RTSCampaignProgression::StoreCapacity(CargoType);
				const int32 Accepted = ARTSCampaign::Get(this) ? Stock->Deposit(CargoType, Carried, Cap) : Carried;
				if (!ARTSCampaign::Get(this)) { Stock->Add(CargoType, Accepted); }
				if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this); Match && CargoType <= ERTSResource::Food) { Match->NotifyGathered(Unit->GetTeam(), CargoType == ERTSResource::Wood, Accepted); }
				Carried -= Accepted; OnWorkChanged.Broadcast();
				if (Carried > 0) { SetStatus(TEXT("仓库已满，保留携带货物等待")); return; }
			}
			bReturning = false;
			if (Order.Type == ERTSOrderType::ReturnResources || !Node || (Node->GetRemaining() == 0 && !Node->IsRenewable())) { URTSOrderLibrary::IssueStopOrder({Unit}); }
		}
		return;
	}
	if (!Node || (Node->GetRemaining() == 0 && !Node->IsRenewable())) { URTSOrderLibrary::IssueStopOrder({Unit}); return; }
	if (!Node->TryReserveGatherer(Unit))
	{
		WorkElapsed=0;bWaitingForNodeSlot=true;Approach(Node,45,true);SetStatus(TEXT("工位已满，绕至外侧空地等候"));return;
	}
	if (bWaitingForNodeSlot) { ResetApproach();bWaitingForNodeSlot=false; }
	if (!Approach(Node, 45)) { SetStatus(bWaitingForWorkPosition ? TEXT("排队等待采集位置") : TEXT("前往资源点")); return; }
	if (Node->GetRemaining() == 0) { WorkElapsed = 0; SetStatus(TEXT("等待资源恢复")); return; }
	SetStatus(TEXT("正在采集")); bWorking = true;
	WorkElapsed += DeltaTime;
	const float Interval = FMath::Max(0.05f, Unit->GetUnitData()->GatherInterval * Node->GetGatherIntervalMultiplier());
	if (WorkElapsed >= Interval)
	{
		WorkElapsed -= Interval;
		CargoType = Node->GetResourceType();
		Carried += Node->Extract(FMath::Min(1, Unit->GetUnitData()->CarryCapacity - Carried));
		OnWorkChanged.Broadcast();
	}
}
