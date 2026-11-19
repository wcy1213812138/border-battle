#include "Orders/RTSMoveGroup.h"
#include "Core/RTSTypes.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"

namespace
{
	// >1 makes early arrivals prefer the far (front) rows, so later units fill in behind
	// them instead of having to push through the units that already stopped.
	constexpr float FrontRowPreference = 1.5f;
}

void FRTSMoveGroup::BuildTypeLayout(const TArray<FName>& UnitTypes, int32 Columns)
{
	SlotUnitTypes.Reset();
	if (Columns <= 0 || UnitTypes.Num() != Slots.Num()) { return; }
	TMap<FName, int32> Remaining;
	for (FName Type : UnitTypes) { ++Remaining.FindOrAdd(Type); }
	TArray<FName> Types;
	Remaining.GetKeys(Types);
	// Stable type blocks, independent of selection/arrival order. Larger groups
	// first is only a packing rule, not a melee/ranged gameplay priority.
	Types.Sort([&Remaining](FName A, FName B)
	{
		return Remaining[A] != Remaining[B] ? Remaining[A] > Remaining[B] : A.LexicalLess(B);
	});
	// Emit all homogeneous full rows before combining remainders. This also
	// keeps consecutive full rows of the same type together.
	for (FName Type : Types)
	{
		const int32 FullRowCount = Remaining[Type] / Columns * Columns;
		for (int32 i = 0; i < FullRowCount; ++i) { SlotUnitTypes.Add(Type); }
		Remaining[Type] -= FullRowCount;
	}
	while (SlotUnitTypes.Num() < Slots.Num())
	{
		const int32 Capacity = FMath::Min(Columns, Slots.Num() - SlotUnitTypes.Num());
		int32 Room = Capacity;
		while (Room > 0)
		{
			int32 Best = INDEX_NONE;
			for (int32 i = 0; i < Types.Num(); ++i)
			{
				const int32 Count = Remaining[Types[i]];
				if (Count <= 0) { continue; }
				if (Best == INDEX_NONE) { Best = i; continue; }
				const int32 BestCount = Remaining[Types[Best]];
				if (Room == Capacity)
				{
					// Full rows first; otherwise keep the largest remainder together.
					if (Count > BestCount) { Best = i; }
				}
				else
				{
					// Prefer a remainder that fits, so finishing a mixed row doesn't
					// split another type when a smaller complete group would fit.
					const bool bFits = Count <= Room;
					const bool bBestFits = BestCount <= Room;
					if ((bFits && !bBestFits) || (bFits == bBestFits && Count > BestCount)) { Best = i; }
				}
			}
			check(Best != INDEX_NONE);
			const FName Type = Types[Best];
			const int32 Take = FMath::Min(Room, Remaining[Type]);
			for (int32 i = 0; i < Take; ++i) { SlotUnitTypes.Add(Type); }
			Remaining[Type] -= Take;
			Room -= Take;
		}
	}
}

bool FRTSMoveGroup::PrepareNavigation(UWorld* World)
{
	if (bNavigationReady || bNavigationFailed || !World)
	{
		return bNavigationReady;
	}
	const double Now = World->GetTimeSeconds();
	if (Now < NextNavigationCheck)
	{
		return false;
	}
	NextNavigationCheck = Now + 0.25;
	if (NavigationWaitStarted < 0.0)
	{
		NavigationWaitStarted = Now;
	}
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || !Nav->IsInitialized() || Nav->IsNavigationBuildInProgress()
		|| !Nav->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		if (Now - NavigationWaitStarted > 60.0)
		{
			bNavigationFailed = true;
			UE_LOG(LogRTS, Warning, TEXT("Group move timed out waiting for a ready NavMesh."));
		}
		return false;
	}

	FNavLocation Projected;
	if (!Nav->ProjectPointToNavigation(Center, Projected, FVector(300.0, 300.0, 500.0)))
	{
		bNavigationFailed = true;
		UE_LOG(LogRTS, Warning, TEXT("Group goal %s has no nearby walkable NavMesh."), *Center.ToCompactString());
		return false;
	}
	const FVector RequestedCenter = Projected.Location;
	const FVector Offset = RequestedCenter - Center;
	TArray<FVector> Original = Slots;
	for (FVector& Point : Original) { Point += Offset; }
	auto Validate = [&](const TArray<FVector>& Points)
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		for (const FVector& Point : Points)
		{
			FNavLocation P;
			if (!Nav->ProjectPointToNavigation(Point, P, FVector(15, 15, 60))
				|| FVector::Dist2D(Point, P.Location) > 10 || FMath::Abs(P.Location.Z - RequestedCenter.Z) > 40
				|| World->OverlapAnyTestByObjectType(Point + FVector(0, 0, 82), FQuat::Identity, Objects, FCollisionShape::MakeCapsule(CollisionRadius + 2, 78))) { return false; }
		}
		for (const FVector& Point : Points)
		{
			UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, RequestedCenter, Point);
			if (!Path || !Path->IsValid() || Path->IsPartial()) { return false; }
		}
		return true;
	};
	int32 ValidCount = 0;
	if (Validate(Original)) { Center = RequestedCenter; Slots = MoveTemp(Original); ValidCount = Slots.Num(); }
	else
	{
		// Prefer a denser layout at the clicked point before moving the destination away.
		const TArray<FName> Types = SlotUnitTypes;
		const float OriginalSpacing = LaneSpacing;
		const float CompactSpacing = FMath::Max(CollisionRadius * 2 + 16, OriginalSpacing * 0.5f);
		const int32 MaxColumns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Slots.Num())));
		for (int32 Back = 0; Back <= 8 && ValidCount == 0; ++Back)
		{
			for (float Scale : {1.f, 0.85f, 0.7f, 0.5f})
			{
				if (ValidCount) { break; }
				const float Spacing = FMath::Max(CompactSpacing, OriginalSpacing * Scale);
				for (int32 Columns = MaxColumns; Columns >= 1 && ValidCount == 0; --Columns)
				{
					const int32 Rows = FMath::DivideAndRoundUp(Slots.Num(), Columns);
					const FVector CandidateCenter = RequestedCenter - Forward * Back * Spacing;
					TArray<FVector> Candidate;
					for (int32 i = 0; i < Slots.Num(); ++i)
					{
						const int32 Row = i / Columns;
						const int32 InRow = FMath::Min(Columns, Slots.Num() - Row * Columns);
						Candidate.Add(CandidateCenter + Forward * ((Rows - 1) * 0.5 - Row) * Spacing
							+ Right * ((i % Columns) - (InRow - 1) * 0.5) * Spacing);
					}
					if (!Validate(Candidate)) { continue; }
					Slots = MoveTemp(Candidate); Center = CandidateCenter; ValidCount = Slots.Num();
					LaneSpacing = Spacing; bCompactLayout = Spacing < OriginalSpacing - 1;
					HalfDepth = (Rows - 1) * 0.5f * Spacing; HalfWidth = (Columns - 1) * 0.5f * Spacing;
					bCoordinateEntry = bCoordinateEntry && Columns > 1 && !bCompactLayout;
					ClaimMargin = Spacing * 0.5f + (bCoordinateEntry ? CollisionRadius * 2 : 0);
					BuildTypeLayout(Types, Columns);
				}
			}
		}
	}
	// Narrow pockets: capsules cannot pass each other inside the corridor, so entry
	// runs one file per column (see ResolveLaneGeometry).
	ResolveLaneGeometry();
	Navigable.Init(ValidCount > 0, Slots.Num());
	bNavigationReady = ValidCount > 0;
	bNavigationFailed = !bNavigationReady;
	UE_LOG(LogRTS, Display, TEXT("Group NavMesh ready: %d/%d distinct slots, center %s."),
		ValidCount, Slots.Num(), *Center.ToCompactString());
	return bNavigationReady;
}

bool FRTSMoveGroup::IsInsideClaimArea(const FVector& Location) const
{
	const FVector Local = Location - Center;
	return FMath::Abs(FVector::DotProduct(Local, Forward)) <= HalfDepth + ClaimMargin
		&& FMath::Abs(FVector::DotProduct(Local, Right)) <= HalfWidth + ClaimMargin
		&& FMath::Abs(Local.Z) < 250.0f;
}

int32 FRTSMoveGroup::ClaimSlot(const FVector& From, FName UnitType)
{
	if (!IsInsideClaimArea(From))
	{
		return INDEX_NONE;
	}
	int32 Best = FindFreeSlot(From, UnitType);
	if (bCoordinateEntry && Best != INDEX_NONE)
	{
		const FVector Delta = From - Slots[Best];
		// Enter at the correct row rather than claiming while cutting across others.
		if (FMath::Abs(FVector::DotProduct(Delta, Forward)) > LaneSpacing * 0.35f
			|| FVector::DotProduct(From - Center, Right) > -HalfWidth - ClaimMargin * 0.35f) { Best = INDEX_NONE; }
	}
	if (Best != INDEX_NONE)
	{
		Claimed[Best] = true;
	}
	return Best;
}

bool FRTSMoveGroup::IsSlotOpen(int32 Index) const
{
	if (bLaneEntry)
	{
		// Lane entry: only the deeper neighbour of the same column is a barrier, and
		// it stops being one as soon as it has physically passed this row.
		return LaneGateOpen(Index);
	}
	if (bCompactLayout)
	{
		// Keep rear rows from parking across the only path to the front rows.
		for (int32 i = 0; i < Slots.Num(); ++i)
		{
			if ((Navigable.IsEmpty() || Navigable[i]) && !Settled[i]
				&& FVector::DotProduct(Slots[i] - Slots[Index], Forward) > LaneSpacing * 0.5f) { return false; }
		}
		return true;
	}
	if (!bCoordinateEntry) { return true; }
	// Rows enter from their left flank and fill right-to-left independently.
	// A parked neighbour in another row is never a completion barrier.
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (i == Index || (Navigable.Num() && !Navigable[i]) || Settled[i]) { continue; }
		const FVector Delta = Slots[i] - Slots[Index];
		if (FVector::DotProduct(Delta, Right) > LaneSpacing * 0.5f
			&& FMath::Abs(FVector::DotProduct(Delta, Forward)) < LaneSpacing * 0.5f) { return false; }
	}
	return true;
}

bool FRTSMoveGroup::CanApproach(FName UnitType) const
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (!Claimed[i] && (SlotUnitTypes.IsEmpty() || SlotUnitTypes[i] == UnitType)
			&& (Navigable.IsEmpty() || Navigable[i]) && IsSlotOpen(i)) { return true; }
	}
	return false;
}

void FRTSMoveGroup::MarkSettled(int32 Index)
{
	if (Settled.IsValidIndex(Index)) { Settled[Index] = true; }
}

int32 FRTSMoveGroup::FindFreeSlot(const FVector& From, FName UnitType) const
{
	int32 Best = INDEX_NONE;
	double BestScore = TNumericLimits<double>::Max();
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Claimed[i] || (Navigable.Num() && !Navigable[i])
			|| (SlotUnitTypes.Num() && SlotUnitTypes[i] != UnitType)
			|| !IsSlotOpen(i))
		{
			continue;
		}
		const double Distance = FVector::Dist2D(From, Slots[i]);
		const double Depth = FVector::DotProduct(Slots[i] - Center, Forward);
		const double Score = Distance - FrontRowPreference * Depth;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = i;
		}
	}
	return Best;
}

void FRTSMoveGroup::ReleaseSlot(int32 Index)
{
	if (Claimed.IsValidIndex(Index))
	{
		Claimed[Index] = false;
		if (Settled.IsValidIndex(Index)) { Settled[Index] = true; } // released/departed units must not block later rows
	}
}

void FRTSMoveGroup::RemoveMember(FName UnitType, int32 ClaimedIndex)
{
	if (!bCoordinateEntry) { ReleaseSlot(ClaimedIndex); return; }
	int32 Index = ClaimedIndex;
	if (!Slots.IsValidIndex(Index))
	{
		// Remove the last unclaimed quota of this type, preserving earlier full rows.
		for (int32 i = Slots.Num() - 1; i >= 0; --i)
		{
			if (!Claimed[i] && !Settled[i] && SlotUnitTypes[i] == UnitType) { Index = i; break; }
		}
	}
	if (Slots.IsValidIndex(Index))
	{
		ReleaseSlot(Index);
		if (Navigable.IsEmpty()) { Navigable.Init(true, Slots.Num()); }
		Navigable[Index] = false;
	}
}

float FRTSMoveGroup::SlotDepth(int32 Slot) const
{
	return Slots.IsValidIndex(Slot) ? static_cast<float>(FVector::DotProduct(Slots[Slot] - Center, Forward)) : 0.0f;
}

FVector FRTSMoveGroup::LanePoint(int32 Lane, float Depth) const
{
	float Lateral = 0.0f;
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (SlotColumn.IsValidIndex(Slot) && SlotColumn[Slot] == Lane)
		{
			Lateral = static_cast<float>(FVector::DotProduct(Slots[Slot] - Center, Right));
			break;
		}
	}
	return Center + Forward * Depth + Right * Lateral;
}

int32 FRTSMoveGroup::RegisterMember(FName UnitType, const FVector& Start)
{
	const int32 Member = MemberType.Num();
	MemberType.Add(UnitType);
	MemberStart.Add(Start);
	MemberLane.Add(INDEX_NONE);
	MemberSlot.Add(INDEX_NONE);
	MemberActive.Add(true);
	MemberDepth.Add(static_cast<float>(FVector::DotProduct(Start - Center, Forward)));
	if (SlotMember.Num() != Slots.Num()) { SlotMember.Init(INDEX_NONE, Slots.Num()); }
	return Member;
}

int32 FRTSMoveGroup::PlannedSlotOf(int32 Member) const
{
	return MemberSlot.IsValidIndex(Member) ? MemberSlot[Member] : INDEX_NONE;
}

void FRTSMoveGroup::FinalizeLaneLayout()
{
	if (bLaneLayoutDone) { return; }
	bLaneLayoutDone = true;
	const int32 Members = MemberStart.Num();
	if (!bLaneEntry || LaneCount <= 0 || Members == 0 || Members != Slots.Num()) { bLaneEntry = false; return; }
	const int32 Rows = Slots.Num() / LaneCount;
	if (Rows * LaneCount != Slots.Num()) { bLaneEntry = false; return; }
	SlotMember.Init(INDEX_NONE, Slots.Num());
	for (int32 Member = 0; Member < Members; ++Member)
	{
		MemberLane[Member] = INDEX_NONE;
		MemberSlot[Member] = INDEX_NONE;
		MemberDepth[Member] = static_cast<float>(FVector::DotProduct(MemberStart[Member] - Center, Forward));
	}
	// Lanes follow the real lateral order so nobody crosses a neighbour's column.
	TArray<int32> Order;
	for (int32 Member = 0; Member < Members; ++Member) { Order.Add(Member); }
	Order.Sort([this](int32 A, int32 B)
	{
		const double LateralA = FVector::DotProduct(MemberStart[A] - Center, Right);
		const double LateralB = FVector::DotProduct(MemberStart[B] - Center, Right);
		return LateralA != LateralB ? LateralA < LateralB : A < B;
	});
	TArray<int32> Fill;
	Fill.Init(0, LaneCount);
	for (int32 Index = 0; Index < Order.Num(); ++Index)
	{
		const int32 Member = Order[Index];
		const double Lateral = FVector::DotProduct(MemberStart[Member] - Center, Right);
		int32 Best = INDEX_NONE;
		double BestCost = TNumericLimits<double>::Max();
		for (int32 Lane = 0; Lane < LaneCount; ++Lane)
		{
			if (Fill[Lane] >= Rows) { continue; }
			const double LaneLateral = FVector::DotProduct(LanePoint(Lane, 0.0f) - Center, Right);
			const double Cost = FMath::Abs(LaneLateral - Lateral);
			if (Cost < BestCost) { BestCost = Cost; Best = Lane; }
		}
		if (Best == INDEX_NONE) { bLaneEntry = false; return; }
		MemberLane[Member] = Best;
		++Fill[Best];
	}
	// Rows follow the real queue: the member closest to the goal takes the deepest row.
	for (int32 Lane = 0; Lane < LaneCount; ++Lane)
	{
		TArray<int32> LaneMembers;
		for (int32 Member = 0; Member < Members; ++Member)
		{
			if (MemberLane[Member] == Lane) { LaneMembers.Add(Member); }
		}
		LaneMembers.Sort([this](int32 A, int32 B)
		{
			const double DepthA = FVector::DotProduct(MemberStart[A] - Center, Forward);
			const double DepthB = FVector::DotProduct(MemberStart[B] - Center, Forward);
			return DepthA != DepthB ? DepthA > DepthB : A < B;
		});
		for (int32 Rank = 0; Rank < LaneMembers.Num(); ++Rank)
		{
			const int32 Member = LaneMembers[Rank];
			for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
			{
				if (SlotColumn[Slot] != Lane || SlotRow[Slot] != Rank || SlotMember[Slot] != INDEX_NONE) { continue; }
				SlotMember[Slot] = Member;
				MemberSlot[Member] = Slot;
				if (SlotUnitTypes.IsValidIndex(Slot)) { SlotUnitTypes[Slot] = MemberType[Member]; }
				break;
			}
		}
	}
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (SlotMember[Slot] == INDEX_NONE) { bLaneEntry = false; return; }
	}
	UE_LOG(LogRTS, Display, TEXT("Lane entry layout: %d lanes, gap %.1fcm, queue order kept per lane."), LaneCount, LaneGap);
}

void FRTSMoveGroup::PublishMemberDepth(int32 Member, const FVector& Location)
{
	if (!MemberDepth.IsValidIndex(Member) || !MemberActive.IsValidIndex(Member) || !MemberActive[Member]) { return; }
	const float Depth = static_cast<float>(FVector::DotProduct(Location - Center, Forward));
	MemberDepth[Member] = FMath::Max(MemberDepth[Member], Depth);
}

void FRTSMoveGroup::DeactivateMember(int32 Member)
{
	if (!MemberActive.IsValidIndex(Member)) { return; }
	MemberActive[Member] = false;
	const int32 Slot = PlannedSlotOf(Member);
	if (Slots.IsValidIndex(Slot))
	{
		if (SlotMember.IsValidIndex(Slot)) { SlotMember[Slot] = INDEX_NONE; }
		ReleaseSlot(Slot);
	}
	MemberSlot[Member] = INDEX_NONE;
}

bool FRTSMoveGroup::LaneGateOpen(int32 Slot) const
{
	if (!bLaneEntry || !SlotRow.IsValidIndex(Slot) || !SlotColumn.IsValidIndex(Slot)) { return true; }
	const int32 Lane = SlotColumn[Slot];
	const int32 Rank = SlotRow[Slot];
	const float Need = SlotDepth(Slot) + LaneGap;
	for (int32 Other = 0; Other < Slots.Num(); ++Other)
	{
		if (!SlotColumn.IsValidIndex(Other) || SlotColumn[Other] != Lane || SlotRow[Other] >= Rank) { continue; }
		if (Navigable.Num() && !Navigable[Other]) { continue; }
		// A deeper unit that already stopped inside its own tolerance counts as passed:
		// its final coordinate lies behind this row, so it can never block the gap.
		if (Settled.IsValidIndex(Other) && Settled[Other]) { continue; }
		const int32 Member = SlotMember.IsValidIndex(Other) ? SlotMember[Other] : INDEX_NONE;
		if (Member == INDEX_NONE || !MemberDepth.IsValidIndex(Member)) { continue; }
		if (!MemberActive.IsValidIndex(Member) || !MemberActive[Member]) { continue; }
		if (MemberDepth[Member] < Need) { return false; }
	}
	return true;
}

float FRTSMoveGroup::LaneAdvanceLimit(int32 Member) const
{
	const int32 MySlot = PlannedSlotOf(Member);
	if (MySlot == INDEX_NONE) { return TNumericLimits<float>::Max(); }
	float Limit = TNumericLimits<float>::Max();
	const int32 Lane = SlotColumn[MySlot];
	const int32 Rank = SlotRow[MySlot];
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (SlotColumn[Slot] != Lane || SlotRow[Slot] >= Rank) { continue; }
		if (Navigable.Num() && !Navigable[Slot]) { continue; }
		if (Settled.IsValidIndex(Slot) && Settled[Slot]) { continue; }
		const int32 Other = SlotMember.IsValidIndex(Slot) ? SlotMember[Slot] : INDEX_NONE;
		if (Other == INDEX_NONE || Other == Member || !MemberActive.IsValidIndex(Other) || !MemberActive[Other]) { continue; }
		Limit = FMath::Min(Limit, MemberDepth[Other] - LaneGap);
	}
	return Limit;
}

float FRTSMoveGroup::LaneFrontierOf(int32 Lane) const
{
	float Frontier = TNumericLimits<float>::Max();
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (!SlotColumn.IsValidIndex(Slot) || SlotColumn[Slot] != Lane || Settled.IsValidIndex(Slot) && Settled[Slot]) { continue; }
		const int32 Member = SlotMember.IsValidIndex(Slot) ? SlotMember[Slot] : INDEX_NONE;
		if (Member == INDEX_NONE || !MemberActive.IsValidIndex(Member) || !MemberActive[Member]) { continue; }
		Frontier = FMath::Min(Frontier, MemberDepth[Member] - LaneGap);
	}
	return Frontier;
}

int32 FRTSMoveGroup::LaneGateCount() const
{
	int32 Open = 0;
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (LaneGateOpen(Slot)) { ++Open; }
	}
	return Open;
}

bool FRTSMoveGroup::CanApproachInLane(int32 Lane) const
{
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		if (!SlotColumn.IsValidIndex(Slot) || SlotColumn[Slot] != Lane) { continue; }
		if (!Claimed[Slot] && (Navigable.IsEmpty() || Navigable[Slot]) && LaneGateOpen(Slot)) { return true; }
	}
	return false;
}

int32 FRTSMoveGroup::ClaimPlannedSlot(int32 Member, const FVector& From)
{
	if (!bLaneEntry || !MemberSlot.IsValidIndex(Member)) { return INDEX_NONE; }
	const int32 Slot = MemberSlot[Member];
	if (!Slots.IsValidIndex(Slot) || Claimed[Slot] || !LaneGateOpen(Slot) || !IsInsideClaimArea(From)) { return INDEX_NONE; }
	Claimed[Slot] = true;
	return Slot;
}

void FRTSMoveGroup::ResolveLaneGeometry()
{
	SlotColumn.Init(INDEX_NONE, Slots.Num());
	SlotRow.Init(INDEX_NONE, Slots.Num());
	SlotMember.Init(INDEX_NONE, Slots.Num());
	LaneCount = 0;
	bLaneEntry = false;
	bLaneLayoutDone = false;
	if (!bCompactLayout || Slots.Num() == 0) { return; }
	// Columns are the distinct lateral coordinates; rows are their depth order.
	TArray<float> Laterals;
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		const float Lateral = static_cast<float>(FVector::DotProduct(Slots[Index] - Center, Right));
		int32 Lane = INDEX_NONE;
		for (int32 Known = 0; Known < Laterals.Num(); ++Known)
		{
			if (FMath::Abs(Laterals[Known] - Lateral) < 2.0f) { Lane = Known; break; }
		}
		if (Lane == INDEX_NONE) { Lane = Laterals.Add(Lateral); }
		SlotColumn[Index] = Lane;
	}
	LaneCount = Laterals.Num();
	for (int32 Lane = 0; Lane < LaneCount; ++Lane)
	{
		TArray<int32> InLane;
		for (int32 Index = 0; Index < Slots.Num(); ++Index)
		{
			if (SlotColumn[Index] == Lane) { InLane.Add(Index); }
		}
		InLane.Sort([this](int32 A, int32 B) { return SlotDepth(A) > SlotDepth(B); });
		for (int32 Row = 0; Row < InLane.Num(); ++Row) { SlotRow[InLane[Row]] = Row; }
	}
	// One capsule gap, but still opening before the deeper neighbour settles.
	LaneGap = FMath::Min(LaneSpacing - 8.0f, FMath::Max(CollisionRadius * 2.0f + 8.0f, 56.0f));
	bLaneEntry = LaneCount > 1 && Slots.Num() > LaneCount;
}
