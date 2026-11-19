#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Orders/RTSMoveGroup.h"
#include "Orders/RTSOrderLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMixedFormationLayoutTest, "MiniRTS.Movement.MixedTypeRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMixedFormationLayoutTest::RunTest(const FString& Parameters)
{
	const FName A(TEXT("TypeA")), B(TEXT("TypeB")), C(TEXT("TypeC"));
	auto Verify = [&](const TArray<FName>& Types, int32 ExpectedMixedRows)
	{
		const int32 Count = Types.Num();
		const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Count)));
		FRTSMoveGroup Group;
		Group.HalfDepth = Group.HalfWidth = 1000.0f;
		Group.Slots = URTSOrderLibrary::ComputeFormationSlots(FVector::ZeroVector, FVector::ForwardVector, Count, 144.0f);
		Group.Claimed.Init(false, Count);
		Group.BuildTypeLayout(Types, Columns);
		TestEqual(TEXT("Exactly one type quota per real unit"), Group.SlotUnitTypes.Num(), Count);
		int32 MixedRows = 0;
		for (int32 Row = 0; Row < Count; Row += Columns)
		{
			TSet<FName> RowTypes;
			for (int32 i = Row; i < FMath::Min(Row + Columns, Count); ++i) { RowTypes.Add(Group.SlotUnitTypes[i]); }
			MixedRows += RowTypes.Num() > 1;
		}
		TestEqual(TEXT("Same types share rows as expected"), MixedRows, ExpectedMixedRows);
		TArray<FName> Reversed;
		for (int32 i = Count - 1; i >= 0; --i) { Reversed.Add(Types[i]); }
		FRTSMoveGroup Other;
		Other.Slots = Group.Slots;
		Other.BuildTypeLayout(Reversed, Columns);
		TestTrue(TEXT("Layout does not depend on selection order"), Other.SlotUnitTypes == Group.SlotUnitTypes);
		TestEqual(TEXT("Unknown type cannot steal a different type's slot"), Group.FindFreeSlot(FVector::ZeroVector, TEXT("Unknown")), INDEX_NONE);
		TestEqual(TEXT("Typed claim still requires entry into rectangle"), Group.ClaimSlot(FVector(10000, 0, 0), Types[0]), INDEX_NONE);
		TSet<int32> Taken;
		for (FName Type : Reversed)
		{
			const int32 Hint = Group.FindFreeSlot(FVector::ZeroVector, Type);
			TestTrue(TEXT("Approach hint does not reserve a slot"), Hint != INDEX_NONE && !Group.Claimed[Hint]);
			const int32 Slot = Group.ClaimSlot(FVector::ZeroVector, Type);
			if (!TestTrue(TEXT("Every unit claims a unique valid coordinate"), Slot != INDEX_NONE && !Taken.Contains(Slot))) { return; }
			Taken.Add(Slot);
			TestTrue(TEXT("Arrival order cannot mix the type quotas"), Group.SlotUnitTypes[Slot] == Type);
		}
		TestEqual(TEXT("All units assigned, no extra coordinates"), Taken.Num(), Count);
		const int32 Released = 0;
		Group.ReleaseSlot(Released);
		TestEqual(TEXT("Release preserves type ownership"), Group.ClaimSlot(FVector::ZeroVector, Group.SlotUnitTypes[Released]), Released);
	};

	Verify({A,A,A,B,B,B}, 0);                     // 6: two homogeneous rows of three
	Verify({C,A,B,A,B,A,B}, 0);                   // 7: 3 + 3 + 1
	Verify({A,B,A,B,A,B,A,A}, 0);                 // 8: A3 + B3 + A2, no forced mixing
	Verify({A,B,C,A,B,C,A,B,C,A,B,C,A,A,A,A}, 0); // 16: 8 + 4 + 4, all full rows
	Verify({A,A,A,A,B,B,C,C}, 1);                 // 8: A3 + B2/A1 + C2
	Verify({A,A,A,A,A,B,B,B,B,B,C,C,C,C,C,C}, 1); // 16: full rows first, remainders share one row
	Verify({A}, 0);
	Verify({A,A,A,A,A,A,A}, 0);                   // single type unchanged
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCompactRowsTest, "MiniRTS.Movement.ParallelLaneEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSCompactRowsTest::RunTest(const FString& Parameters)
{
	FRTSMoveGroup Group;
	const FName A(TEXT("A")), B(TEXT("B"));
	Group.Slots = URTSOrderLibrary::ComputeFormationSlots(FVector::ZeroVector, FVector::ForwardVector, 4, 144.0f);
	Group.Claimed.Init(false, 4);
	Group.Settled.Init(false, 4);
	Group.SlotUnitTypes = {A, A, B, B};
	Group.LaneSpacing = 144.0f;
	Group.bCoordinateEntry = true;
	Group.HalfDepth = Group.HalfWidth = 72.0f;
	Group.ClaimMargin = 152.0f;
	TestTrue(TEXT("Both rows can approach immediately"), Group.CanApproach(A) && Group.CanApproach(B));
	TestEqual(TEXT("Cannot claim outside rectangle"), Group.ClaimSlot(FVector(-1000,0,0), A), INDEX_NONE);
	const int32 First = Group.ClaimSlot(FVector(72,-200,0), A);
	const int32 Rear = Group.ClaimSlot(FVector(-72,-200,0), B);
	TestTrue(TEXT("Different rows claim concurrently with neither row settled"), First == 1 && Rear == 3);
	TestFalse(TEXT("Same-row entry remains clear until its far unit arrives"), Group.CanApproach(A));
	Group.MarkSettled(Rear);
	TestTrue(TEXT("Rear row advances independently of front row"), Group.CanApproach(B));
	Group.RemoveMember(A, First);
	TestTrue(TEXT("Departed member does not block its own row"), Group.CanApproach(A));
	Group.RemoveMember(B, INDEX_NONE);
	TestEqual(TEXT("Departure removes only the unclaimed quota"), Group.FindFreeSlot(FVector::ZeroVector, B), INDEX_NONE);
	return true;
}
#endif
