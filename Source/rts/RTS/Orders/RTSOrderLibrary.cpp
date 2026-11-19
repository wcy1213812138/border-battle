#include "Orders/RTSOrderLibrary.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "Orders/RTSMoveGroup.h"
#include "NavigationSystem.h"
#include "Engine/World.h"

namespace
{
	constexpr float FormationSpacing = 120.0f;  // minimum slot spacing (cm)
	constexpr float SpacingPerRadius = 3.6f;    // slot spacing relative to the widest unit's radius

	TArray<ARTSUnit*> AliveUnits(const TArray<ARTSUnit*>& Units)
	{
		TArray<ARTSUnit*> Result;
		Result.Reserve(Units.Num());
		for (ARTSUnit* Unit : Units)
		{
			if (IsValid(Unit) && Unit->IsAlive())
			{
				Result.Add(Unit);
			}
		}
		return Result;
	}
}

TArray<FVector> URTSOrderLibrary::ComputeFormationSlots(const FVector& Goal, const FVector& Facing, int32 Count, float Spacing)
{
	TArray<FVector> Slots;
	if (Count <= 0)
	{
		return Slots;
	}

	FVector Forward = Facing.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);

	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
	const int32 Rows = FMath::DivideAndRoundUp(Count, Columns);

	Slots.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const int32 Row = Index / Columns;
		const int32 Col = Index % Columns;
		// Last row may be shorter; center it too.
		const int32 ColsInRow = (Row == Rows - 1) ? Count - Row * Columns : Columns;
		const float X = ((Rows - 1) * 0.5f - Row) * Spacing;
		const float Y = (Col - (ColsInRow - 1) * 0.5f) * Spacing;
		Slots.Add(Goal + Forward * X + Right * Y);
	}
	return Slots;
}

void URTSOrderLibrary::IssueMoveOrder(const UObject* WorldContext, const TArray<ARTSUnit*>& Units, const FVector& Goal, bool bQueued)
{
	TArray<ARTSUnit*> Movers = AliveUnits(Units);
	if (Movers.IsEmpty())
	{
		return;
	}
	if (bQueued)
	{
		// Shift queue: keep the active order and walk to this goal once the previous step ends.
		FRTSOrder Queued;
		Queued.Type = ERTSOrderType::Move;
		Queued.TargetLocation = Goal;
		for (ARTSUnit* Unit : Movers) { Unit->QueueOrder(Queued); }
		return;
	}
	for (ARTSUnit* Unit : Movers) { Unit->ClearQueuedOrders(); }
	// One unit needs no formation rectangle. Layout fallback can otherwise move its
	// destination hundreds of centimetres backwards and report success at its start.
	if (Movers.Num() == 1)
	{
		FRTSOrder Move;
		Move.Type = ERTSOrderType::Move;
		Move.TargetLocation = Goal;
		Movers[0]->ReceiveOrder(Move);
		return;
	}

	FVector Centroid = FVector::ZeroVector;
	for (const ARTSUnit* Unit : Movers)
	{
		Centroid += Unit->GetActorLocation();
	}
	Centroid /= Movers.Num();

	FVector Forward = (Goal - Centroid).GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::ForwardVector;
	}
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);

	// Spacing follows the widest unit so rings and bodies never overlap.
	float MaxRadius = 0.0f;
	for (const ARTSUnit* Unit : Movers)
	{
		MaxRadius = FMath::Max(MaxRadius, Unit->GetStandingCollisionRadius());
	}
	// Unit type changes row layout, never the formation's final density.
	const float Spacing = FMath::Max(FormationSpacing, MaxRadius * SpacingPerRadius);

	const int32 Count = Movers.Num();
	TArray<FVector> Slots = ComputeFormationSlots(Goal, Forward, Count, Spacing);

	// One shared rectangle of slots. Units initially travel to its NavMesh-projected
	// center using crowd avoidance, then claim a free slot as they reach the area.
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
	const int32 Rows = FMath::DivideAndRoundUp(Count, Columns);

	TSharedRef<FRTSMoveGroup> Group = MakeShared<FRTSMoveGroup>();
	Group->Center = Goal;
	Group->RequestedGoal = Goal;
	Group->bHasRequestedGoal = true;
	Group->Forward = Forward;
	Group->Right = Right;
	Group->HalfDepth = (Rows - 1) * 0.5f * Spacing;
	Group->HalfWidth = (Columns - 1) * 0.5f * Spacing;
	Group->ClaimMargin = Spacing * 0.5f;
	Group->CollisionRadius = 0;
	for (const ARTSUnit* Unit : Movers) { Group->CollisionRadius = FMath::Max(Group->CollisionRadius, Unit->GetFormationCollisionRadius()); }
	Group->Slots = MoveTemp(Slots);
	Group->Claimed.Init(false, Count);
	TArray<FName> UnitTypes;
	UnitTypes.Reserve(Count);
	for (const ARTSUnit* Unit : Movers) { UnitTypes.Add(Unit->GetFormationUnitType()); }
	Group->BuildTypeLayout(UnitTypes, Columns);

	Group->LaneSpacing = Spacing;
	Group->Settled.Init(false, Count);
	TSet<FName> DistinctTypes(UnitTypes);
	Group->bCoordinateEntry = DistinctTypes.Num() > 1;
	if (Group->bCoordinateEntry) { Group->ClaimMargin += MaxRadius * 2.0f; }
	// Temporary approach positions keep blocked lanes away from the entrance.
	// These are not final slots and do not create any reservation.
	for (int32 i = 0; i < Movers.Num(); ++i)
	{
		const float Side = i % 2 == 0 ? -1.0f : 1.0f;
		FVector WaitingPoint = Goal + Right * Side * (Group->HalfWidth + Spacing * (2.0f + (i / 2) % 3))
			- Forward * (Group->HalfDepth + Spacing * (1.0f + i / 6));
		if (Group->bCoordinateEntry)
		{
			if (UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(WorldContext->GetWorld()))
			{
				// Avoid waiting on another floor when the outside lanes touch a ramp/platform.
				FNavLocation Projected;
				bool bFound = false;
				for (int32 Attempt = 0; Attempt < 8; ++Attempt)
				{
					const FVector Candidate = WaitingPoint - Right * Side * Spacing * (Attempt % 2)
						- Forward * Spacing * (Attempt / 2);
					if (Nav->ProjectPointToNavigation(Candidate, Projected, FVector(50, 50, 80))
						&& FMath::Abs(Projected.Location.Z - Goal.Z) < 60.0)
					{
						WaitingPoint = Projected.Location;
						bFound = true;
						break;
					}
				}
				if (!bFound) { WaitingPoint = Movers[i]->GetActorLocation(); }
			}
		}
		Movers[i]->ReceiveGroupMove(Group, WaitingPoint);
	}
}

void URTSOrderLibrary::IssueAttackOrder(const TArray<ARTSUnit*>& Units, AActor* Target, bool bQueued)
{
	if (!IsValid(Target))
	{
		return;
	}
	FRTSOrder Order;
	Order.Type = ERTSOrderType::Attack;
	Order.TargetActor = Target;
	for (ARTSUnit* Unit : AliveUnits(Units))
	{
		if (Unit == Target) { continue; }
		if (bQueued) { Unit->QueueOrder(Order); }
		else { Unit->ClearQueuedOrders(); Unit->ReceiveOrder(Order); }
	}
}

void URTSOrderLibrary::IssueAttackMoveOrder(const TArray<ARTSUnit*>& Units, const FVector& Goal)
{
	FRTSOrder Order;
	Order.Type = ERTSOrderType::AttackMove;
	Order.TargetLocation = Goal;
	for (ARTSUnit* Unit : AliveUnits(Units))
	{
		Unit->ClearQueuedOrders();
		Unit->ReceiveOrder(Order);
	}
}

void URTSOrderLibrary::IssueStopOrder(const TArray<ARTSUnit*>& Units)
{
	FRTSOrder Order;
	Order.Type = ERTSOrderType::Stop;
	for (ARTSUnit* Unit : AliveUnits(Units))
	{
		Unit->ClearQueuedOrders();
		Unit->ReceiveOrder(Order);
	}
}

void URTSOrderLibrary::IssueWorkOrder(const TArray<ARTSUnit*>& Units, AActor* Target, bool bQueued)
{
	if (!IsValid(Target)) { return; }
	for (ARTSUnit* Unit : AliveUnits(Units))
	{
		if (!Unit->GetUnitData() || !Unit->GetUnitData()->bWorker) { continue; }
		FRTSOrder Order;
		Order.TargetActor = Target;
		if (const ARTSResourceNode* Node = Cast<ARTSResourceNode>(Target))
		{
			if (Node->GetRemaining() <= 0 && !Node->IsRenewable()) { continue; }
			Order.Type = ERTSOrderType::Gather;
		}
		else if (const ARTSBuilding* Building = Cast<ARTSBuilding>(Target))
		{
			if (!Building->IsAlive() || Building->GetTeam() != Unit->GetTeam()) { continue; }
			if (Building->IsComplete() && Building->GetBuildingId() == TEXT("Farm") && Building->GetFoodSource())
			{ IssueWorkOrder({Unit}, Building->GetFoodSource(), bQueued); continue; }
			if (!Building->IsComplete()) { Order.Type = ERTSOrderType::Build; }
			else if (Building->IsDropOff()) { Order.Type = ERTSOrderType::ReturnResources; }
			else { continue; }
		}
		else { continue; }
		if (bQueued) { Unit->QueueOrder(Order); }
		else { Unit->ClearQueuedOrders(); Unit->ReceiveOrder(Order); }
	}
}
