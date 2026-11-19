// Focused display-only regression for HUD target guide lines. A multi-selection group
// move must converge on the shared requested destination; a solo selection must show
// the live slot/detour. Pure data: no PIE, world or rendering.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/RTSTypes.h"
#include "Orders/RTSMoveGroup.h"
#include "UI/RTSHUD.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMoveGuideSharedVsSoloTest,
	"MiniRTS.UI.MoveGuideTargets.SharedGroupVsSolo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMoveGuideSharedVsSoloTest::RunTest(const FString&)
{
	FRTSMoveGroup Group;
	Group.Center = FVector(1500.0, -400.0, 20.0);
	Group.Slots.Add(Group.Center);
	Group.Slots.Add(Group.Center + FVector(144.0, 0.0, 0.0));
	Group.Slots.Add(Group.Center + FVector(0.0, 144.0, 0.0));

	FRTSOrder Move;
	Move.Type = ERTSOrderType::Move;
	// ReceiveGroupMove and TickGroupMove rewrite this to waiting points, approaches or slots.
	Move.TargetLocation = FVector(1700.0, -300.0, 20.0);

	FVector Target = FVector::ZeroVector;

	if (!TestTrue(TEXT("Multi-selection move draws a guide"),
		ARTSHUD::ResolveMoveGuideTarget(Move, &Group, /*bMultipleSelected=*/true, Target)))
	{
		return false;
	}
	TestTrue(TEXT("All lines converge on the one requested destination"),
		Target.Equals(Group.Center, 0.1f));
	TestFalse(TEXT("The shared destination is not this unit's slot or detour"),
		Target.Equals(Move.TargetLocation, 0.1f));

	if (!TestTrue(TEXT("Solo selection move still draws a guide"),
		ARTSHUD::ResolveMoveGuideTarget(Move, &Group, /*bMultipleSelected=*/false, Target)))
	{
		return false;
	}
	TestTrue(TEXT("Solo selection shows the actual slot/detour target"),
		Target.Equals(Move.TargetLocation, 0.1f));
	TestFalse(TEXT("Solo selection does not fall back to the group goal"),
		Target.Equals(Group.Center, 0.1f));

	Group.RequestedGoal = Group.Center;
	Group.bHasRequestedGoal = true;
	Group.Center += FVector(-500,0,0); // A nav fallback moves the actual formation, not player intent.
	TestTrue(TEXT("Multi-selection preserves raw click after formation fallback"),
		ARTSHUD::ResolveMoveGuideTarget(Move,&Group,true,Target) && Target.Equals(Group.RequestedGoal,.1));
	TestTrue(TEXT("Solo still shows its live target after fallback"),
		ARTSHUD::ResolveMoveGuideTarget(Move,&Group,false,Target) && Target.Equals(Move.TargetLocation,.1));

	// A plain individual move has no group and must keep its own destination even if
	// several unrelated units happen to be selected.
	Move.TargetLocation = FVector(-400.0, 600.0, 20.0);
	TestTrue(TEXT("Individual move without a group keeps its requested destination"),
		ARTSHUD::ResolveMoveGuideTarget(Move, nullptr, /*bMultipleSelected=*/true, Target)
		&& Target.Equals(Move.TargetLocation, 0.1f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMoveGuideIndependentAndClearedTest,
	"MiniRTS.UI.MoveGuideTargets.IndependentGroupsAndStaleMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSMoveGuideIndependentAndClearedTest::RunTest(const FString&)
{
	FRTSMoveGroup FirstGroup;
	FirstGroup.Center = FVector(1500.0, -400.0, 20.0);
	FirstGroup.Slots.Add(FirstGroup.Center);
	FirstGroup.Slots.Add(FirstGroup.Center + FVector(144.0, 0.0, 0.0));

	FRTSMoveGroup SecondGroup;
	SecondGroup.Center = FVector(-800.0, 900.0, 20.0);
	SecondGroup.Slots.Add(SecondGroup.Center);
	SecondGroup.Slots.Add(SecondGroup.Center + FVector(144.0, 0.0, 0.0));

	FRTSOrder FirstMove;
	FirstMove.Type = ERTSOrderType::Move;
	FirstMove.TargetLocation = FirstGroup.Slots[1];

	FRTSOrder SecondMove;
	SecondMove.Type = ERTSOrderType::Move;
	SecondMove.TargetLocation = SecondGroup.Slots[1];

	FVector Target = FVector::ZeroVector;

	// Two groups issued at different clicks never share one global destination.
	TestTrue(TEXT("First independent group converges on its own goal"),
		ARTSHUD::ResolveMoveGuideTarget(FirstMove, &FirstGroup, /*bMultipleSelected=*/true, Target)
		&& Target.Equals(FirstGroup.Center, 0.1f));
	TestTrue(TEXT("Second independent group converges on its own goal"),
		ARTSHUD::ResolveMoveGuideTarget(SecondMove, &SecondGroup, /*bMultipleSelected=*/true, Target)
		&& Target.Equals(SecondGroup.Center, 0.1f));
	TestFalse(TEXT("Independent groups do not share a destination"),
		FirstGroup.Center.Equals(SecondGroup.Center, 0.1f));

	// Settled units keep their shared reservation but have no Move order; Stop and
	// Attack must clear the blue guide instead of reusing the lingering group.
	FRTSOrder Stop;
	Stop.Type = ERTSOrderType::Stop;
	TestFalse(TEXT("Stop clears the move guide"),
		ARTSHUD::ResolveMoveGuideTarget(Stop, &FirstGroup, /*bMultipleSelected=*/true, Target));

	FRTSOrder Attack;
	Attack.Type = ERTSOrderType::Attack;
	TestFalse(TEXT("Attack does not draw a stale blue move guide"),
		ARTSHUD::ResolveMoveGuideTarget(Attack, &FirstGroup, /*bMultipleSelected=*/true, Target));

	FRTSOrder Settled;
	TestFalse(TEXT("A retained reservation without a move order draws nothing"),
		ARTSHUD::ResolveMoveGuideTarget(Settled, &FirstGroup, /*bMultipleSelected=*/true, Target));

	FRTSOrder AttackMove; AttackMove.Type=ERTSOrderType::AttackMove; AttackMove.TargetLocation=FVector(600,700,20);
	TestTrue(TEXT("Attack-move keeps a visible destination guide"),ARTSHUD::ResolveMoveGuideTarget(AttackMove,&FirstGroup,true,Target) && Target.Equals(AttackMove.TargetLocation,.1));
	return true;
}
#endif
