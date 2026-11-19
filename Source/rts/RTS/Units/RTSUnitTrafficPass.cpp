#include "Units/RTSUnit.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

bool ARTSUnit::RequestTrafficPassMove()
{
	AAIController* AI = Cast<AAIController>(GetController());
	if (!AI) { return false; }
	// Only traverse the segment validated below; never detour with collision ignored.
	FAIMoveRequest Request(TrafficPassExit);
	Request.SetAcceptanceRadius(3);
	Request.SetReachTestIncludesAgentRadius(false);
	Request.SetUsePathfinding(false);
	Request.SetProjectGoalLocation(false);
	Request.SetAllowPartialPath(false);
	return AI->MoveTo(Request).Code != EPathFollowingRequestResult::Failed;
}

bool ARTSUnit::TryTrafficPass(ARTSUnit* Blocker, const FVector& TravelDirection)
{
	const double Now = GetWorldTimeForTraffic();
	const FVector Direction = TravelDirection.GetSafeNormal2D();
	if (!IsValid(Blocker) || Blocker == this || !IsAlive() || !Blocker->IsAlive()
		|| Blocker->GetTeam() != GetTeam() || CurrentOrder.Type != ERTSOrderType::Move
		|| TrafficPassPartner.IsValid() || Blocker->TrafficPassPartner.IsValid()
		|| Now < TrafficPassCooldownUntil || Now < Blocker->TrafficPassCooldownUntil
		|| Direction.IsNearlyZero() || GetCharacterMovement()->MaxWalkSpeed <= 0
		|| GetCharacterMovement()->MovementMode == MOVE_None
		|| Blocker->GetVelocity().SizeSquared2D() > 625 || YieldPhase || Blocker->YieldPhase) { return false; }
	UCapsuleComponent* Capsule = GetCapsuleComponent();
	UCapsuleComponent* Other = Blocker->GetCapsuleComponent();
	if (Capsule->GetMoveIgnoreActors().Contains(Blocker) || Other->GetMoveIgnoreActors().Contains(this)) { return false; }
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float Combined = Radius + Other->GetScaledCapsuleRadius();
	const FVector Relative = Blocker->GetActorLocation() - GetActorLocation();
	if (Relative.Size2D() > Combined + 100 || FMath::Abs(Relative.Z) > 60
		|| FVector::DotProduct(Relative, Direction) < 0) { return false; }
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Start = GetActorLocation() + FVector(0, 0, 3);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(TrafficPassBlocker), false, this);
	FHitResult Hit;
	// The first physical obstruction must be precisely this friendly unit, not a wall.
	if (!GetWorld()->SweepSingleByChannel(Hit, Start, Start + Direction * (Relative.Size2D() + Combined), FQuat::Identity,
		ECC_Pawn, FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - 1), HalfHeight - 2), Query)
		|| Hit.GetActor() != Blocker) { return false; }
	FVector Exit = Blocker->GetActorLocation() + Direction * (Combined + 12);
	if (FVector::DotProduct(CurrentOrder.TargetLocation - Exit, Direction) < 0)
	{
		Exit = FVector(CurrentOrder.TargetLocation.X, CurrentOrder.TargetLocation.Y, GetActorLocation().Z);
		if (FVector::DotProduct(Exit - Blocker->GetActorLocation(), Direction) < 0
			|| FVector::Dist2D(Exit, Blocker->GetActorLocation()) < Combined + 2) { return false; }
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	FNavLocation Projected;
	const FVector Feet = Exit - FVector(0, 0, HalfHeight);
	if (!Nav || Nav->IsNavigationBuildInProgress()
		|| !Nav->ProjectPointToNavigation(Feet, Projected, FVector(15, 15, 60))
		|| FVector::Dist2D(Feet, Projected.Location) > 12
		|| FMath::Abs(Feet.Z - Projected.Location.Z) > 20) { return false; }
	FVector NavHit;
	if (UNavigationSystemV1::NavigationRaycast(GetWorld(), Start - FVector(0, 0, HalfHeight + 3),
		Projected.Location, NavHit, nullptr, GetController())) { return false; }
	Query.AddIgnoredActor(Blocker);
	const FVector End(Projected.Location.X, Projected.Location.Y, GetActorLocation().Z + 3);
	const FCollisionShape Shape = FCollisionShape::MakeCapsule(FMath::Max(1.f, Radius - 0.5f), HalfHeight - 2);
	if (GetWorld()->SweepTestByChannel(Start, End, FQuat::Identity, ECC_Pawn, Shape, Query)
		|| GetWorld()->OverlapBlockingTestByChannel(End, FQuat::Identity, ECC_Pawn, Shape, Query)) { return false; }

	TrafficPassPartner = Blocker; Blocker->TrafficPassPartner = this;
	bTrafficPassOwner = true; Blocker->bTrafficPassOwner = false;
	TrafficPassOrigin = GetActorLocation(); TrafficPassExit = End - FVector(0, 0, 3);
	TrafficPassUntil = Now + 2.0;
	TrafficPassCooldownUntil = Blocker->TrafficPassCooldownUntil = Now + 5.0;
	Capsule->IgnoreActorWhenMoving(Blocker, true); Other->IgnoreActorWhenMoving(this, true);
	StopAIMovement();
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))
		{ bTrafficPassWasSettling = Crowd->IsFormationSettling(); Crowd->SetFormationSettling(true); }
	}
	++TrafficPassCount;
	if (!RequestTrafficPassMove()) { EndTrafficPass(true); return false; }
	return true;
}

void ARTSUnit::EndTrafficPass(bool bResumeOrder)
{
	ARTSUnit* Partner = TrafficPassPartner.Get(true); // EndPlay may already have marked it pending kill.
	if (!bTrafficPassOwner)
	{
		if (Partner) { Partner->EndTrafficPass(true); }
		TrafficPassPartner.Reset();
		return;
	}
	StopAIMovement();
	// Prefer a swept exit or retreat. Never teleport through a third actor to restore.
	const float Combined = Partner ? GetSimpleCollisionRadius() + Partner->GetSimpleCollisionRadius() + 2 : 0;
	if (Partner && IsAlive() && Partner->IsAlive() && !IsActorBeingDestroyed() && !Partner->IsActorBeingDestroyed()
		&& FVector::Dist2D(GetActorLocation(), Partner->GetActorLocation()) < Combined)
	{
		TArray<FVector> Candidates = {TrafficPassExit, TrafficPassOrigin};
		for (int32 Index = 0; Index < 16; ++Index)
		{
			const double Angle = Index * 2.0 * PI / 16;
			Candidates.Add(Partner->GetActorLocation() + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Combined + 6));
		}
		const float Height = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FCollisionQueryParams Query(SCENE_QUERY_STAT(TrafficPassRestore), false, this);
		Query.AddIgnoredActor(Partner);
		const FCollisionShape Shape = FCollisionShape::MakeCapsule(FMath::Max(1.f, GetSimpleCollisionRadius() - 0.5f), Height - 2);
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		for (FVector Candidate : Candidates)
		{
			Candidate.Z = GetActorLocation().Z;
			FNavLocation Projected;
			const FVector Feet = Candidate - FVector(0, 0, Height);
			FVector NavHit;
			if (!Nav || !Nav->ProjectPointToNavigation(Feet, Projected, FVector(12, 12, 30))
				|| FVector::Dist2D(Feet, Projected.Location) > 10
				|| FMath::Abs(Feet.Z - Projected.Location.Z) > 20
				|| UNavigationSystemV1::NavigationRaycast(GetWorld(), GetActorLocation() - FVector(0, 0, Height),
					Projected.Location, NavHit, nullptr, GetController())
				|| FVector::Dist2D(Candidate, Partner->GetActorLocation()) < Combined
				|| GetWorld()->SweepTestByChannel(GetActorLocation() + FVector(0, 0, 3), Candidate + FVector(0, 0, 3),
					FQuat::Identity, ECC_Pawn, Shape, Query)
				|| GetWorld()->OverlapBlockingTestByChannel(Candidate + FVector(0, 0, 3), FQuat::Identity, ECC_Pawn, Shape, Query)) { continue; }
			FHitResult Hit;
			GetCapsuleComponent()->MoveComponent(Candidate - GetActorLocation(), GetActorQuat(), true, &Hit);
			if (FVector::Dist2D(GetActorLocation(), Partner->GetActorLocation()) >= Combined) { break; }
		}
	}
	// Even if dynamically surrounded, bound the exception and let CharacterMovement
	// resolve residual penetration with physical collision rather than an unbounded ignore.
	if (Partner)
	{
		GetCapsuleComponent()->IgnoreActorWhenMoving(Partner, false);
		Partner->GetCapsuleComponent()->IgnoreActorWhenMoving(this, false);
		Partner->TrafficPassPartner.Reset(); Partner->bTrafficPassOwner = false;
	}
	TrafficPassPartner.Reset(); bTrafficPassOwner = false;
	GetCharacterMovement()->StopMovementImmediately();
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (URTSCrowdFollowingComponent* Crowd = Cast<URTSCrowdFollowingComponent>(AI->GetPathFollowingComponent()))
		{ Crowd->SetFormationSettling(bTrafficPassWasSettling); }
	}
	FormationProgress.Reset(); TrafficProgress.Reset(); FormationWaypoints.Reset(); NextGroupMoveAttempt = 0;
	if (bResumeOrder && CurrentOrder.Type == ERTSOrderType::Move && !MoveGroup.IsValid() && IsAlive() && !IsActorBeingDestroyed())
	{ RequestMoveTo(CurrentOrder.TargetLocation, 5); }
}

bool ARTSUnit::TickTrafficPass()
{
	ARTSUnit* Partner = TrafficPassPartner.Get();
	if (!bTrafficPassOwner) { return false; }
	const bool bFinished = !Partner || !Partner->IsAlive() || Partner->GetTeam() != GetTeam()
		|| CurrentOrder.Type != ERTSOrderType::Move || GetWorldTimeForTraffic() >= TrafficPassUntil
		|| FVector::Dist2D(GetActorLocation(), TrafficPassExit) < 8;
	if (bFinished) { EndTrafficPass(true); return false; }
	if (const AAIController* AI = Cast<AAIController>(GetController()); AI && AI->GetMoveStatus() == EPathFollowingStatus::Idle)
	{ RequestTrafficPassMove(); }
	return true;
}
