// Focused regression for narrow-pocket lane entry: lanes, queue order, the
// "deeper neighbour has passed" gate and single-claim guarantees. Pure data,
// no PIE, no world, no rendering.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Orders/RTSMoveGroup.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLaneEntryQueueTest, "MiniRTS.Movement.LaneEntryQueue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** Same pocket as the 170cm corridor fixture: 2 columns, 4 rows, 72cm spacing. */
	void BuildPocket(FRTSMoveGroup& Group)
	{
		Group.Center = FVector(500.0, 500.0, 10.0);
		Group.Forward = FVector::ForwardVector;
		Group.Right = FVector(0.0, 1.0, 0.0);
		Group.LaneSpacing = 72.0f;
		Group.CollisionRadius = 28.0f;
		Group.bCompactLayout = true;
		Group.HalfDepth = 108.0f;
		Group.HalfWidth = 36.0f;
		Group.ClaimMargin = 36.0f;
		Group.Slots.Reset();
		for (int32 Row = 0; Row < 4; ++Row)
		{
			for (int32 Column = 0; Column < 2; ++Column)
			{
				Group.Slots.Add(Group.Center + Group.Forward * (108.0f - Row * 72.0f)
					+ Group.Right * (Column == 0 ? -36.0f : 36.0f));
			}
		}
		Group.Claimed.Init(false, Group.Slots.Num());
		Group.Settled.Init(false, Group.Slots.Num());
		Group.SlotUnitTypes.Init(NAME_None, Group.Slots.Num());
		Group.ResolveLaneGeometry();
	}

	/** Member index that owns the given lane/row, or INDEX_NONE. */
	int32 MemberAt(const FRTSMoveGroup& Group, int32 Lane, int32 Row)
	{
		for (int32 Slot = 0; Slot < Group.Slots.Num(); ++Slot)
		{
			if (Group.SlotColumn[Slot] == Lane && Group.SlotRow[Slot] == Row) { return Group.SlotMember[Slot]; }
		}
		return INDEX_NONE;
	}

	FVector AtDepth(const FRTSMoveGroup& Group, float Depth)
	{
		return Group.Center + Group.Forward * Depth;
	}
}

bool FRTSLaneEntryQueueTest::RunTest(const FString&)
{
	const FName Sword(TEXT("Swordman"));
	const FName Archer(TEXT("Archer"));

	// The pocket fixture spawns two files: swords on the left, archers on the right.
	TSharedRef<FRTSMoveGroup> Group = MakeShared<FRTSMoveGroup>();
	BuildPocket(Group.Get());
	TestTrue(TEXT("Compact pocket switches to lane entry"), Group->bLaneEntry);
	TestEqual(TEXT("Pocket has two lanes"), Group->LaneCount, 2);
	TestEqual(TEXT("Lane gap keeps one capsule but still opens"), Group->LaneGap, 64.0f);

	TArray<int32> Members;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float X = 500.0f - 520.0f - Index * 110.0f; // -20, -130, -240, -350
		Members.Add(Group->RegisterMember(Sword, FVector(X, 445.0f, 82.0f)));
		Members.Add(Group->RegisterMember(Archer, FVector(X, 555.0f, 82.0f)));
	}
	Group->FinalizeLaneLayout();
	TestTrue(TEXT("Lane layout complete"), Group->bLaneEntry && Group->bLaneLayoutDone);

	// 1. Lanes follow the real lateral order, so no column ever crosses another.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		TestEqual(TEXT("Sword keeps the left lane"), Group->MemberLane[Members[Index * 2]], 0);
		TestEqual(TEXT("Archer keeps the right lane"), Group->MemberLane[Members[Index * 2 + 1]], 1);
	}
	// 2. Rows follow the real queue: the unit nearest the goal owns the deepest row.
	for (int32 Lane = 0; Lane < 2; ++Lane)
	{
		for (int32 Rank = 0; Rank < 4; ++Rank)
		{
			const int32 Member = MemberAt(Group.Get(), Lane, Rank);
			TestTrue(TEXT("Every row has exactly one member"), Member != INDEX_NONE);
			TestEqual(TEXT("Row matches queue order"), Member, Lane == 0 ? Rank * 2 : Rank * 2 + 1);
		}
	}
	// 3. Every slot carries the type of the unit that will take it, and none is shared.
	TSet<int32> UsedMembers;
	for (int32 Slot = 0; Slot < Group->Slots.Num(); ++Slot)
	{
		const int32 Member = Group->SlotMember[Slot];
		TestTrue(TEXT("Slot member is valid"), Group->MemberSlot.IsValidIndex(Member));
		TestEqual(TEXT("Slot type matches its unit"), Group->SlotUnitTypes[Slot].ToString(),
			Group->MemberUnitType(Member).ToString());
		TestFalse(TEXT("No slot is shared"), UsedMembers.Contains(Member));
		UsedMembers.Add(Member);
	}

	// 4. The gate only waits for the deeper neighbour of the same column.
	TestTrue(TEXT("Deepest row is free to enter"), Group->LaneGateOpen(0));
	TestFalse(TEXT("Row behind a waiting neighbour is closed"), Group->LaneGateOpen(6));
	const int32 DeepestLeft = MemberAt(Group.Get(), 0, 0);
	const int32 SecondLeft = MemberAt(Group.Get(), 0, 1);
	const int32 SecondSlot = Group->MemberSlot[SecondLeft];
	const float SecondDepth = Group->SlotDepth(SecondSlot);

	// Advance the deepest unit to just short of the gate: still closed.
	Group->PublishMemberDepth(DeepestLeft, AtDepth(Group.Get(), SecondDepth + Group->LaneGap - 5.0f));
	TestFalse(TEXT("Gate stays closed while the neighbour has not passed"), Group->LaneGateOpen(SecondSlot));

	// Once it has passed, the follower may enter even before the leader settles.
	Group->PublishMemberDepth(DeepestLeft, AtDepth(Group.Get(), SecondDepth + Group->LaneGap));
	TestTrue(TEXT("Gate opens when the neighbour passed the row"), Group->LaneGateOpen(SecondSlot));

	// A leader that stops inside its own tolerance still counts as passed, otherwise
	// every column waits forever behind a coordinate the leader never reaches exactly.
	Group->PublishMemberDepth(DeepestLeft, AtDepth(Group.Get(), Group->SlotDepth(Group->MemberSlot[DeepestLeft]) - 13.0f));
	Group->MarkSettled(Group->MemberSlot[DeepestLeft]);
	TestTrue(TEXT("Settled leader counts as passed"), Group->LaneGateOpen(SecondSlot));

	// 5. Claiming is exclusive, type matched and still needs the claim rectangle.
	const FVector SlotLocation = Group->Slots[SecondSlot];
	TestTrue(TEXT("Claim succeeds inside the rectangle"), Group->ClaimPlannedSlot(SecondLeft, SlotLocation) == SecondSlot);
	TestEqual(TEXT("Second claim of the same coordinate fails"), Group->ClaimPlannedSlot(SecondLeft, SlotLocation), INDEX_NONE);
	TestEqual(TEXT("A blocked row cannot claim early"), Group->ClaimPlannedSlot(MemberAt(Group.Get(), 0, 3), SlotLocation), INDEX_NONE);

	// 6. Departing, dying or cancelled members release the row and stop blocking.
	const int32 Third = MemberAt(Group.Get(), 0, 2);
	Group->DeactivateMember(SecondLeft);
	TestEqual(TEXT("Released slot is free again"), Group->SlotMember[SecondSlot], INDEX_NONE);
	TestFalse(TEXT("Released slot is unclaimed"), Group->Claimed[SecondSlot]);
	TestTrue(TEXT("Third row is no longer blocked by the departed member"), Group->LaneGateOpen(Group->MemberSlot[Third]));

	// 7. Without a compact pocket the legacy row layout stays in charge.
	FRTSMoveGroup Wide;
	Wide.Center = FVector(500.0, 500.0, 10.0);
	Wide.Forward = FVector::ForwardVector;
	Wide.Right = FVector(0.0, 1.0, 0.0);
	Wide.LaneSpacing = 144.0f;
	Wide.CollisionRadius = 28.0f;
	Wide.bCompactLayout = false;
	Wide.Slots.Add(Wide.Center);
	Wide.Claimed.Init(false, 1);
	Wide.Settled.Init(false, 1);
	Wide.SlotUnitTypes.Init(NAME_None, 1);
	Wide.ResolveLaneGeometry();
	TestFalse(TEXT("Wide layout keeps the legacy entry"), Wide.bLaneEntry);
	return true;
}
#endif
