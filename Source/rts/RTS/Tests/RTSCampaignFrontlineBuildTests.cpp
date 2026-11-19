// Focused pure regression for campaign worker-built Barracks rules. No PIE/world:
// footprint bounds and the guide/count/cost gate are shared with the HUD button.
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Core/RTSHumanController.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Campaign/RTSCampaignMapSpace.h"
#include "Campaign/RTSCampaign.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignFootprintBoundsTest,
	"MiniRTS.Campaign.FrontlineBarracks.FootprintBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSCampaignFootprintBoundsTest::RunTest(const FString&)
{
	const float Footprint = 240.f;
	const double Half = Footprint * 0.5;

	TestTrue(TEXT("Footprint inside near the current min corner"),
		ARTSHumanController::IsCampaignFootprintInsideMap(
			FVector(RTSCampaignMapSpace::MinX + Half + 1.0, RTSCampaignMapSpace::MinY + Half + 1.0, 0.0), Footprint));
	TestTrue(TEXT("Footprint inside near the current max corner"),
		ARTSHumanController::IsCampaignFootprintInsideMap(
			FVector(RTSCampaignMapSpace::MaxX - Half - 1.0, RTSCampaignMapSpace::MaxY - Half - 1.0, 0.0), Footprint));
	TestFalse(TEXT("Footprint crossing min X is rejected"),
		ARTSHumanController::IsCampaignFootprintInsideMap(
			FVector(RTSCampaignMapSpace::MinX + Half - 1.0, RTSCampaignMapSpace::MinY + Half + 1.0, 0.0), Footprint));
	TestFalse(TEXT("Footprint crossing max Y is rejected"),
		ARTSHumanController::IsCampaignFootprintInsideMap(
			FVector(RTSCampaignMapSpace::MaxX - Half - 1.0, RTSCampaignMapSpace::MaxY - Half + 1.0, 0.0), Footprint));
	TestFalse(TEXT("Invalid footprint or coordinates are rejected"),
		ARTSHumanController::IsCampaignFootprintInsideMap(FVector::ZeroVector, -1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignFrontlineGateTest,
	"MiniRTS.Campaign.FrontlineBarracks.GuideCountAndCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRTSCampaignFrontlineGateTest::RunTest(const FString&)
{
	FRTSBalanceRow Barracks;
	Barracks.Kind = TEXT("Building");
	Barracks.WoodCost = 100;
	Barracks.FoodCost = 0;
	Barracks.BuildSeconds = 8.f;
	Barracks.Footprint = 240.f;
	FString Error;

	TestFalse(TEXT("Construction waits for guide acknowledgement"),
		ARTSHumanController::CanStartCampaignBarracksBuild(0, 1, 500, 500, Barracks, Error));
	TestTrue(TEXT("Locked message is explicit"), !Error.IsEmpty());

	TestTrue(TEXT("Worker construction is available during the tutorial"),
		ARTSHumanController::CanStartCampaignBarracksBuild(1, 1, 500, 500, Barracks, Error));

	TestTrue(TEXT("Guide step 7 with room and exact wood cost is allowed"),
		ARTSHumanController::CanStartCampaignBarracksBuild(7, ARTSHumanController::CampaignFriendlyBuildingLimit - 1,
			Barracks.WoodCost, Barracks.FoodCost, Barracks, Error));
	TestTrue(TEXT("Allowed message is clear"), Error.IsEmpty());

	TestFalse(TEXT("The 30-building save limit blocks the button"),
		ARTSHumanController::CanStartCampaignBarracksBuild(7, ARTSHumanController::CampaignFriendlyBuildingLimit,
			500, 500, Barracks, Error));
	TestTrue(TEXT("Limit message is explicit"), !Error.IsEmpty());

	TestFalse(TEXT("One wood short is rejected"),
		ARTSHumanController::CanStartCampaignBarracksBuild(7, 5, Barracks.WoodCost - 1, 500, Barracks, Error));
	TestTrue(TEXT("Cost message is explicit"), !Error.IsEmpty());

	Barracks.FoodCost = 50;
	TestFalse(TEXT("One food short is rejected"),
		ARTSHumanController::CanStartCampaignBarracksBuild(7, 5, 500, 49, Barracks, Error));
	TestTrue(TEXT("Exact food cost is allowed"),
		ARTSHumanController::CanStartCampaignBarracksBuild(7, 5, 500, 50, Barracks, Error));

	TestEqual(TEXT("Shared save-validator building limit"), ARTSHumanController::CampaignFriendlyBuildingLimit, 30);
	return true;
}
#endif
