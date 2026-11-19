#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Orders/RTSReplanBudget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSReplanBudgetTest, "MiniRTS.Congestion.LocalReplanBudget", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRTSReplanBudgetTest::RunTest(const FString&)
{
	FRTSReplanBudget Budget;
	TestTrue(TEXT("First job admitted"), Budget.TryBegin(1, 0));
	TestTrue(TEXT("Second job admitted"), Budget.TryBegin(2, 0));
	TestFalse(TEXT("Third job deferred, not granted above frame budget"), Budget.TryBegin(3, 0));
	TestFalse(TEXT("Fourth job queued"), Budget.TryBegin(4, 0));
	TestTrue(TEXT("Candidate searches share an already admitted job"), Budget.TryBegin(1, 0));
	TestFalse(TEXT("Earlier units cannot overtake waiting jobs"), Budget.TryBegin(1, 1));
	TestTrue(TEXT("Oldest waiting job admitted next frame"), Budget.TryBegin(3, 1));
	TestTrue(TEXT("Next waiter admitted"), Budget.TryBegin(4, 1));
	TestFalse(TEXT("Same frame still capped"), Budget.TryBegin(5, 1));
	TestFalse(TEXT("Cancelled queue head briefly retains position"), Budget.TryBegin(5, 2));
	TestTrue(TEXT("Cancelled head expires without permanent blockage"), Budget.TryBegin(5, 4));

	FRTSReplanBudget Crowd;
	TArray<int32> Grants; Grants.Init(0, 60);
	for (uint64 Frame = 0; Frame < 60; ++Frame)
	{
		int32 Count = 0;
		for (uint32 Unit = 0; Unit < 60; ++Unit)
		{
			if (Crowd.TryBegin(Unit, Frame)) { ++Count; ++Grants[Unit]; }
		}
		TestTrue(TEXT("60 requesters never exceed two new jobs per frame"), Count <= FRTSReplanBudget::JobsPerFrame);
	}
	for (int32 Count : Grants) { TestEqual(TEXT("Every persistent requester receives fair service"), Count, 2); }
	FRTSReplanBudget OtherWorld;
	TestTrue(TEXT("Independent world starts with its own budget"), OtherWorld.TryBegin(100, 0));
	return true;
}
#endif
