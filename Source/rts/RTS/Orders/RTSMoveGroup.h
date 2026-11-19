// MiniRTS - shared formation for one group move order.
// Units keep crowd avoidance in transit and may claim exactly one free coordinate
// only after entering the destination rectangle. Reservations survive arrival and
// are released by a new order, death or destruction. Shared by TSharedPtr.

#pragma once

#include "CoreMinimal.h"

struct FRTSMoveGroup
{
	FVector Center = FVector::ZeroVector;
	// Presentation-only intent, never rewritten by nav projection/compact formation fallback.
	FVector RequestedGoal = FVector::ZeroVector;
	bool bHasRequestedGoal = false;
	FVector Forward = FVector::ForwardVector;   // direction of travel
	FVector Right = FVector::RightVector;
	float HalfDepth = 0.0f;                     // slot rectangle half-size along Forward
	float HalfWidth = 0.0f;                     // slot rectangle half-size along Right
	float ClaimMargin = 0.0f;                   // extra band around the rectangle where claiming starts

	TArray<FVector> Slots;
	TArray<bool> Claimed;

	/** Type quotas only; individual units still claim their coordinate on arrival. */
	TArray<FName> SlotUnitTypes;
	void BuildTypeLayout(const TArray<FName>& UnitTypes, int32 Columns);

	/** Entry is coordinated per lane, never behind a whole-row completion barrier. */
	bool bCoordinateEntry = false;
	float LaneSpacing = 144.0f;
	float CollisionRadius = 28.f;
	bool bCompactLayout = false;
	TArray<bool> Settled;

	// ---- Lane entry (narrow pockets where capsules cannot pass each other) ----
	// One file per column, ordered by the real queue: slots are handed out from the
	// deepest row backwards in arrival order, so a column never has to be overtaken.
	// A unit may only occupy its row once every deeper unit of its own column has
	// physically passed it; that replaces the old "whole front row settled" barrier.
	bool bLaneEntry = false;
	int32 LaneCount = 0;
	TArray<int32> SlotColumn;
	TArray<int32> SlotRow;
	/** Clearance a lane keeps between neighbours: one capsule plus slack. */
	float LaneGap = 64.0f;
	/** Registered member index -> lane, planned slot, live depth along Forward. */
	TArray<int32> MemberLane;
	TArray<int32> MemberSlot;
	TArray<int32> SlotMember;
	TArray<float> MemberDepth;
	TArray<bool> MemberActive;
	int32 RegisterMember(FName UnitType, const FVector& Start);
	/**
	 * One-shot. Lanes follow the real lateral order and rows follow the real queue
	 * order, so a column never has to be overtaken. Called by the first member that
	 * finds the navigation ready.
	 */
	void FinalizeLaneLayout();
	bool bLaneLayoutDone = false;
	/** Publish the member's depth along Forward; monotonic, so a lane only advances. */
	void PublishMemberDepth(int32 Member, const FVector& Location);
	/** Dead/departed/cancelled members stop holding their lane and slot. */
	void DeactivateMember(int32 Member);
	/** True when every deeper member of this slot's column has already passed it. */
	bool LaneGateOpen(int32 Slot) const;
	/** Deepest point this lane may still reach before the next follower arrives. */
	float LaneFrontierOf(int32 Lane) const;
	int32 LaneGateCount() const;
	FVector LanePoint(int32 Lane, float Depth) const;
	bool CanApproach(FName UnitType) const;
	bool CanApproachInLane(int32 Lane) const;
	void MarkSettled(int32 Index);
	bool IsSlotOpen(int32 Index) const;
	/** Recover lanes and depth ranks from the final slots; compact layouts only. */
	void ResolveLaneGeometry();

	/** Wait for the world's NavMesh before resolving the approach point and slots. */
	bool PrepareNavigation(UWorld* World);
	bool bNavigationFailed = false;

private:
	bool bNavigationReady = false;
	double NextNavigationCheck = 0.0;
	double NavigationWaitStarted = -1.0;
	TArray<bool> Navigable;
	TArray<FName> MemberType;
	TArray<FVector> MemberStart;

public:

	/** True if Location is inside the slot rectangle grown by ClaimMargin. */
	bool IsInsideClaimArea(const FVector& Location) const;
	/** Remove one departing unit's quota; does not allocate a slot to an arriving unit. */
	void RemoveMember(FName UnitType, int32 ClaimedIndex);

	/** Claims the best free slot for a unit at From. Returns INDEX_NONE if all are taken. */
	int32 ClaimSlot(const FVector& From, FName UnitType = NAME_None);

	/** Approach hint only: does not reserve a coordinate outside the rectangle. */
	int32 FindFreeSlot(const FVector& From, FName UnitType = NAME_None) const;

	/** The exact coordinate reserved for this member inside its lane. */
	int32 ClaimPlannedSlot(int32 Member, const FVector& From);
	int32 PlannedSlotOf(int32 Member) const;
	/** Depth this member may still advance to before catching its lane neighbour. */
	float LaneAdvanceLimit(int32 Member) const;
	float SlotDepth(int32 Slot) const;
	/** Type registered for this member, for tracing and tests. */
	FName MemberUnitType(int32 Member) const { return MemberType.IsValidIndex(Member) ? MemberType[Member] : NAME_None; }

	void ReleaseSlot(int32 Index);
};
