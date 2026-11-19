#include "Units/RTSUnit.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Economy/RTSGathererComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void ARTSUnit::UpdateFormationCollision()
{
	if (TrafficPassPartner.IsValid()) { return; }
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	const bool bForming = MoveGroup.IsValid()
		&& (GroupSlot != INDEX_NONE || FVector::Dist2D(GetActorLocation(), MoveGroup->Center) < 1000);
	const float Desired = bForming ? GetFormationCollisionRadius() : StandingCapsuleRadius;
	if (FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(), Desired)) { return; }
	if (!bForming)
	{
		const double Now = GetWorldTimeForTraffic();
		if (Now < NextCollisionRestoreCheck) { return; }
		NextCollisionRestoreCheck = Now + 0.2;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(FormationCollisionRestore), false, this);
		if (GetWorld()->OverlapBlockingTestByProfile(GetActorLocation(), FQuat::Identity, Capsule->GetCollisionProfileName(),
			FCollisionShape::MakeCapsule(Desired * Capsule->GetShapeScale(), Capsule->GetScaledCapsuleHalfHeight() - 1), Query)) { return; }
	}
	Capsule->SetCapsuleRadius(Desired, true);
}

double ARTSUnit::GetWorldTimeForTraffic() const
{
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0;
}

double ARTSUnit::GetRemainingPathDistance() const
{
	const AAIController* AI = Cast<AAIController>(GetController());
	const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
	if (!Follow || !Follow->GetPath().IsValid()) { return FVector::Dist2D(GetActorLocation(), CurrentOrder.TargetLocation); }
	const TArray<FNavPathPoint>& Points = Follow->GetPath()->GetPathPoints();
	FVector Previous = GetActorLocation();
	double Remaining = 0;
	for (int32 Index = static_cast<int32>(Follow->GetNextPathIndex()); Index < Points.Num(); ++Index)
	{
		Remaining += FVector::Dist2D(Previous, Points[Index].Location);
		Previous = Points[Index].Location;
	}
	return Remaining;
}

double ARTSUnit::GetRemainingFormationDistance() const
{
	double Remaining = GetRemainingPathDistance();
	for (int32 Point = 1; Point < FormationWaypoints.Num(); ++Point)
	{
		Remaining += FVector::Dist2D(FormationWaypoints[Point - 1], FormationWaypoints[Point]);
	}
	return Remaining;
}

void ARTSUnit::BeginFormationRoute()
{
	// Local graph vertices are stable. Do not reward a retry of the same blocked
	// route (notably compact entry retries at 0.2s) with a new stall deadline.
	TArray<FVector> Route = FormationWaypoints;
	if (Route.IsEmpty()) { Route.Add(CurrentOrder.TargetLocation); }
	bool bSameRoute = Route.Num() == FormationRouteSnapshot.Num() && FormationProgress.Goal.Equals(CurrentOrder.TargetLocation, 25);
	for (int32 Point = 0; bSameRoute && Point < Route.Num(); ++Point)
	{
		bSameRoute = Route[Point].Equals(FormationRouteSnapshot[Point], 1);
	}
	if (bSameRoute) { return; }
	FormationRouteSnapshot = MoveTemp(Route);
	// Replanning can legitimately lengthen a route. Give the committed detour one
	// observation window instead of comparing it with the blocked shortcut forever.
	const double Now = GetWorldTimeForTraffic();
	const double Remaining = GetRemainingFormationDistance();
	FormationProgress.BeginRoute(CurrentOrder.TargetLocation, Remaining, Now);
	TrafficProgress.BeginRoute(CurrentOrder.TargetLocation, Remaining, Now);
	bFormationStalled = false;
}

void ARTSUnit::EndCongestionRecovery()
{
	EndTrafficPass();
	if (YieldPhase) { StopAIMovement(); }
	YieldPhase = 0;
	YieldRequester.Reset();
	TrafficWaitUntil = 0;
	TrafficProgress.Reset();
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))
		{
			if (AI->GetMoveStatus() == EPathFollowingStatus::Idle) { Crowd->SetFormationSettling(MoveGroup.IsValid() && GroupSlot != INDEX_NONE); }
		}
	}
}

bool ARTSUnit::TryYieldTo(ARTSUnit* Requester, const FVector& TravelDirection)
{
	const double Now = GetWorldTimeForTraffic();
	const FVector Direction = TravelDirection.GetSafeNormal2D();
	if (!IsValid(Requester) || Requester == this || !Requester->IsAlive() || !IsAlive()
		|| Requester->GetTeam() != GetTeam() || Direction.IsNearlyZero() || YieldPhase
		|| TrafficPassPartner.IsValid() || Requester->TrafficPassPartner.IsValid()
		|| Now < YieldCooldownUntil || CurrentOrder.Type != ERTSOrderType::None
		|| GetCombatStance() == ERTSCombatStance::Guard || GetCharacterMovement()->MaxWalkSpeed <= 0
		|| GetCharacterMovement()->MovementMode == MOVE_None || GetVelocity().SizeSquared2D() > 625
		|| (Gatherer && Gatherer->IsWorking()) || Attack->IsSwinging()) { return false; }
	// Members of the same formation settle through compact collision, not visible shuffling.
	if (MoveGroup.IsValid() && MoveGroup == Requester->MoveGroup) { return false; }
	const AAIController* RequestAI = Cast<AAIController>(Requester->GetController());
	if (!RequestAI || (RequestAI->GetMoveStatus() != EPathFollowingStatus::Moving
		&& !(Requester->MoveGroup.IsValid() && Requester->CurrentOrder.Type == ERTSOrderType::Move)) || Requester->IsTrafficSidesteppingOrWaiting()) { return false; }
	const float Combined = GetSimpleCollisionRadius() + Requester->GetSimpleCollisionRadius();
	const FVector Relative = GetActorLocation() - Requester->GetActorLocation();
	if (Relative.SizeSquared2D() > FMath::Square(Combined + 150) || FMath::Abs(Relative.Z) > 80
		|| FVector::DotProduct(Relative, Direction) < -10) { return false; }
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav || Nav->IsNavigationBuildInProgress()) { return false; }
	const FVector Origin = GetActorLocation() - FVector(0, 0, GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Direction);
	const float Preferred = FVector::DotProduct(Relative, Side) >= 0 ? 1.f : -1.f;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic); Objects.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TrafficYield), false, this);
	const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector Lift(0, 0, HalfHeight + 3);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(GetSimpleCollisionRadius() + 2, HalfHeight - 2);
	// The requester may already be touching us. Analytically permit only separation
	// from it, while sweeping against all other actors (no movement ignore is added).
	Query.AddIgnoredActor(Requester);
	for (float SideMargin : {4.f, 8.f})
	{
	for (float Sign : {Preferred, -Preferred})
	{
		for (float Back : {0.f, -0.25f, 0.25f})
		{
			const float ExistingSide = FVector::DotProduct(Relative, Side) * Sign;
			const float NeededStep = FMath::Max(0.f, Combined + SideMargin - ExistingSide);
			if (NeededStep < 2) { continue; } // Already clear: do not manufacture a sidestep.
			const FVector Candidate = Origin + (Side * Sign + Direction * Back) * NeededStep;
			FNavLocation Projected;
			if (!Nav->ProjectPointToNavigation(Candidate, Projected, FVector(15, 15, 60))
				|| FVector::Dist2D(Candidate, Projected.Location) > 15 || FMath::Abs(Projected.Location.Z - Origin.Z) > 40) { continue; }
			const FVector Step = (Projected.Location - Origin) * FVector(1, 1, 0);
			const FVector ToRequester = (Requester->GetActorLocation() - GetActorLocation()) * FVector(1, 1, 0);
			const double ClosestT = FMath::Clamp(FVector::DotProduct(ToRequester, Step) / FMath::Max(Step.SizeSquared(), 0.01), 0.0, 1.0);
			if ((ToRequester - Step * ClosestT).SizeSquared() < FMath::Min(ToRequester.SizeSquared(), FMath::Square(static_cast<double>(Combined))) - 0.1
				|| FVector::Dist2D(Projected.Location, Requester->GetActorLocation()) < Combined + SideMargin
				|| FVector::DotProduct(Projected.Location + Lift - Requester->GetActorLocation(), Side) * Sign < Combined + SideMargin - 1) { continue; }
			bool bReserved = false;
			for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
			{
				if (*It == this || !It->IsAlive()) { continue; }
				if (It->YieldPhase && FVector::Dist2D(Projected.Location, It->YieldPoint) < GetSimpleCollisionRadius() + It->GetSimpleCollisionRadius() + 12) { bReserved = true; break; }
				if (*It != Requester && FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) < 400)
				{
					const AAIController* OtherAI = Cast<AAIController>(It->GetController());
					if (It->YieldPhase || (OtherAI && OtherAI->GetMoveStatus() == EPathFollowingStatus::Moving))
					{
						const FVector A = It->GetActorLocation() * FVector(1, 1, 0);
						const FVector Target = It->YieldPhase ? It->YieldPoint : OtherAI->GetPathFollowingComponent()->GetCurrentTargetLocation();
						const FVector B = A + ((Target * FVector(1, 1, 0)) - A).GetClampedToMaxSize(180);
						const float Clearance = GetSimpleCollisionRadius() + It->GetSimpleCollisionRadius() + 6;
						FVector ClosestA, ClosestB;
						FMath::SegmentDistToSegmentSafe(Origin * FVector(1, 1, 0), Projected.Location * FVector(1, 1, 0), A, B, ClosestA, ClosestB);
						if (FVector::DistSquared(ClosestA, ClosestB) < FMath::Square(Clearance)) { bReserved = true; break; }
					}
				}
				if (It->MoveGroup.IsValid())
				{
					for (int32 Slot = 0; Slot < It->MoveGroup->Slots.Num(); ++Slot)
					{
						if (It->MoveGroup->Claimed[Slot] && FVector::Dist2D(Projected.Location, It->MoveGroup->Slots[Slot]) < Combined + 10) { bReserved = true; break; }
					}
				}
				if (bReserved) { break; }
			}
			FCollisionQueryParams CandidateQuery = Query;
			bool bCrowdedStart = false;
			for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
			{
				if (*It == this || *It == Requester || !It->IsAlive()) { continue; }
				const FVector Delta = (It->GetActorLocation() - GetActorLocation()) * FVector(1, 1, 0);
				const double Clearance = GetSimpleCollisionRadius() + It->GetSimpleCollisionRadius() + 2;
				if (FMath::Abs(It->GetActorLocation().Z - GetActorLocation().Z) > HalfHeight * 2 || Delta.SizeSquared() > FMath::Square(Clearance)) { continue; }
				// Inflated query capsules can overlap at the start. Only permit a
				// separating segment; real movement still blocks every neighbour.
				if (FVector::DotProduct(Delta, Step) > 0 || (Delta - Step).SizeSquared() < FMath::Square(Clearance))
				{ bCrowdedStart = true; break; }
				CandidateQuery.AddIgnoredActor(*It);
			}
			FVector Hit;
			if (bReserved || bCrowdedStart || UNavigationSystemV1::NavigationRaycast(GetWorld(), Origin, Projected.Location, Hit, nullptr, GetController())
				|| GetWorld()->SweepTestByObjectType(Origin + Lift, Projected.Location + Lift, FQuat::Identity, Objects, Shape, CandidateQuery)
				|| GetWorld()->OverlapAnyTestByObjectType(Projected.Location + Lift, FQuat::Identity, Objects, Shape, CandidateQuery)) { continue; }
			UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), Origin, Projected.Location);
			if (!Path || !Path->IsValid() || Path->IsPartial()) { continue; }
			YieldOrigin = Origin; YieldDirection = Direction; YieldPoint = Projected.Location;
			YieldRequester = Requester; YieldStarted = Now; YieldPhase = 1;
			YieldWaitUntil = Now + 0.6; YieldCooldownUntil = Now + 12;
			StopAIMovement();
			if (AAIController* AI = Cast<AAIController>(GetController()))
			{
				if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent())) { Crowd->SetFormationSettling(true); }
			}
			if (!RequestMoveTo(YieldPoint, 5)) { EndCongestionRecovery(); return false; }
			if (MoveGroup.IsValid() && MoveGroup->Settled.IsValidIndex(GroupSlot)) { MoveGroup->Settled[GroupSlot] = false; }
			++TrafficYieldCount;
			return true;
		}
	}
	}
	return false;
}

void ARTSUnit::TickTrafficYield()
{
	const double Now = GetWorldTimeForTraffic();
	const AAIController* AI = Cast<AAIController>(GetController());
	if (!IsAlive() || !AI || CurrentOrder.Type != ERTSOrderType::None || GetCombatStance() == ERTSCombatStance::Guard)
	{ EndCongestionRecovery(); return; }
	if (YieldPhase == 1)
	{
		if (FVector::Dist2D(GetActorLocation(), YieldPoint) <= 15)
		{
			StopAIMovement(); GetCharacterMovement()->StopMovementImmediately(); YieldPhase = 2;
		}
		else if (Now - YieldStarted > 3 || AI->GetMoveStatus() == EPathFollowingStatus::Idle)
		{
			StopAIMovement(); YieldPhase = 2;
		}
	}
	if (YieldPhase == 2)
	{
		const ARTSUnit* Requester = YieldRequester.Get();
		const bool bPassed = !Requester || !Requester->IsAlive() || Requester->CurrentOrder.Type == ERTSOrderType::None
			|| FVector::DotProduct(Requester->GetActorLocation() - YieldOrigin, YieldDirection) > GetSimpleCollisionRadius() + Requester->GetSimpleCollisionRadius() + 35
			|| FVector::Dist2D(Requester->GetActorLocation(), YieldOrigin) > 350;
		if (Now < YieldWaitUntil || (!bPassed && Now - YieldStarted < 6)) { return; }
		if (MoveGroup.IsValid() && MoveGroup->Slots.IsValidIndex(GroupSlot))
		{
			// Retain the reservation while temporarily yielding; re-enter through normal settling.
			YieldPhase = 3;
			CurrentOrder.Type = ERTSOrderType::Move;
			CurrentOrder.TargetLocation = MoveGroup->Slots[GroupSlot];
			FormationWaypoints.Reset(); FormationProgress.Reset();
			NextGroupMoveAttempt = 0; GroupMoveStarted = Now; bGroupApproachStarted = true;
			YieldRequester.Reset();
		}
		else { EndCongestionRecovery(); }
	}
}

void ARTSUnit::TickCongestionRecovery()
{
	// Returning to a reserved slot (phase 3) is normal travel and can itself be blocked.
	if (IsTrafficSidesteppingOrWaiting()) { return; }
	if (TrafficPassPartner.IsValid()) { return; }
	const double Now = GetWorldTimeForTraffic();
	const AAIController* AI = Cast<AAIController>(GetController());
	const UPathFollowingComponent* Follow = AI ? AI->GetPathFollowingComponent() : nullptr;
	const bool bTravelling = AI && (AI->GetMoveStatus() == EPathFollowingStatus::Moving || (MoveGroup.IsValid() && bGroupApproachStarted))
		&& CurrentOrder.Type != ERTSOrderType::None
		&& !(Gatherer && (Gatherer->IsWorking() || Gatherer->IsWaitingForWorkPosition()))
		&& !(CurrentOrder.Type == ERTSOrderType::Attack && Attack->IsInRange(CurrentOrder.TargetActor.Get()));
	if (!bTravelling) { TrafficProgress.Reset(); TrafficWaitUntil = 0; return; }
	const FVector ProgressGoal = MoveGroup.IsValid() ? CurrentOrder.TargetLocation : Follow->GetPathDestination();
	const bool bCompact = MoveGroup.IsValid() && MoveGroup->bCompactLayout;
	if (!TrafficProgress.Observe(ProgressGoal, MoveGroup.IsValid() ? GetRemainingFormationDistance() : GetRemainingPathDistance(), Now, bCompact ? 0.65 : 1.2) || Now < NextTrafficSample) { return; }
	NextTrafficSample = Now + (bCompact ? 0.2 : 0.4);
	const FVector Direction = ((AI->GetMoveStatus() == EPathFollowingStatus::Moving ? Follow->GetCurrentTargetLocation() : CurrentOrder.TargetLocation) - GetActorLocation()).GetSafeNormal2D();
	ARTSUnit* Nearest = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (*It == this || !It->IsAlive() || It->GetTeam() != GetTeam()) { continue; }
		const FVector Delta = It->GetActorLocation() - GetActorLocation();
		const double Along = FVector::DotProduct(Delta, Direction);
		const float Combined = GetSimpleCollisionRadius() + It->GetSimpleCollisionRadius();
		if (Along < 0 || Along > Combined + 100 || FMath::Abs(Delta.Z) > 80
			|| (Delta - Direction * Along).SizeSquared2D() > FMath::Square(Combined + 12)) { continue; }
		if (It->YieldPhase && It->YieldRequester.Get() == this)
		{
			TrafficWaitUntil = Now + 0.8;
			NextGroupMoveAttempt = FMath::Max(NextGroupMoveAttempt, TrafficWaitUntil);
			return;
		}
		if (Delta.SizeSquared2D() < BestDistance && It->GetVelocity().SizeSquared2D() <= 625)
		{ Nearest = *It; BestDistance = Delta.SizeSquared2D(); }
	}
	if (Nearest && Nearest->TryYieldTo(this, Direction))
	{
		// Keep the current path while the blocker performs its sidestep.
		TrafficWaitUntil = Now + 1.2;
		NextGroupMoveAttempt = FMath::Max(NextGroupMoveAttempt, TrafficWaitUntil);
	}
	else if (Nearest)
	{
		TryTrafficPass(Nearest, Direction);
	}
}
