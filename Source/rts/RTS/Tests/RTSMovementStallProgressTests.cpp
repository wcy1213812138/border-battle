// The same pure-data checks can run without loading/linking the game module.
#if defined(RTS_MOVEMENT_STALL_STANDALONE) || WITH_DEV_AUTOMATION_TESTS
#include "Orders/RTSMovementProgress.h"

namespace
{
	template<typename TCheck>
	void CheckRouteProgress(TCheck&& Check)
	{
		const FVector Goal(1000, 0, 0);
		FRTSMovementProgress Progress;
		Check("Initial observation is not stalled", !Progress.Observe(Goal, 100, 0));
		Check("Blocked shortcut stalls", Progress.Observe(Goal, 100, 2));
		Check("Same-goal longer route inherits stale minimum without rebasing", Progress.Observe(Goal, 600, 2.1));
		Progress.BeginRoute(Goal, 600, 2.1);
		Check("Committed detour gets a fresh window", !Progress.Observe(Goal, 600, 2.2));
		Check("Progress on a longer detour counts before beating old shortcut", !Progress.Observe(Goal, 580, 3.2));
		Check("Continued detour progress keeps its path alive", !Progress.Observe(Goal, 560, 4.3));
		Check("A truly blocked replacement still stalls", Progress.Observe(Goal, 560, 6));

		Progress.BeginRoute(Goal, 600, 10);
		Check("Rebase does not credit oscillation", !Progress.Observe(Goal, 605, 10.8));
		Check("Oscillation still expires the new window", Progress.Observe(Goal, 599, 11.7));
		Progress.BeginRoute(Goal, 600, 20);
		Check("Sub-threshold motion accumulates", !Progress.Observe(Goal, 594, 20.8));
		Check("Cumulative meaningful motion refreshes deadline", !Progress.Observe(Goal, 588, 21.5));
		Check("Compact traffic timer also gets its full window", !Progress.Observe(Goal, 588, 22.1, 0.65));
		Check("Compact traffic still detects a stopped detour", Progress.Observe(Goal, 588, 22.2, 0.65));
		Progress.Reset();
		Check("New order resets observation", !Progress.Observe(Goal, 900, 30));
		Check("Destination change resets observation", !Progress.Observe(FVector(2000, 0, 0), 900, 40));
	}
}

#if defined(RTS_MOVEMENT_STALL_STANDALONE)
#include <cstdio>
int main()
{
	int Checks = 0, Failures = 0;
	CheckRouteProgress([&](const char* Name, bool bPassed)
	{
		++Checks;
		if (!bPassed) { ++Failures; std::printf("FAIL: %s\n", Name); }
	});
	std::printf("Movement route progress: %d checks, %d failures\n", Checks, Failures);
	return Failures == 0 ? 0 : 1;
}
#else
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSRouteRebaseTest, "MiniRTS.Congestion.RouteProgressBaseline", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FRTSRouteRebaseTest::RunTest(const FString&)
{
	CheckRouteProgress([this](const char* Name, bool bPassed) { TestTrue(UTF8_TO_TCHAR(Name), bPassed); });
	return true;
}
#endif
#endif
