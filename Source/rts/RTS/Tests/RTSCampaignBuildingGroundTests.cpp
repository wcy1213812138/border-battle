#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Campaign/RTSCampaignBuildingGround.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignTerrain.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFixedGroundSitePolicy,"MiniRTS.Persistence.FixedBuildingGroundPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSFixedGroundSitePolicy::RunTest(const FString&)
{
 using namespace RTSCampaignBuildingGround;
 for(int32 F=0;F<4;++F)for(FName Kind:{FName("TownHall"),FName("Barracks"),FName("ArcheryRange")})
 {
  FVector Site;TestTrue(TEXT("Known economic site resolves"),FormulaSite(F,Kind,Site));
  TestTrue(TEXT("Exact old enemy site eligible"),IsFormulaSite(1,F,Kind,Site,0));
  TestFalse(TEXT("Player building never migrates"),IsFormulaSite(0,F,Kind,Site,0));
  TestFalse(TEXT("Different XY never migrates"),IsFormulaSite(1,F,Kind,Site+FVector(2,0,0),0));
  TestFalse(TEXT("Arbitrary Z never migrates"),IsFormulaSite(1,F,Kind,Site+FVector(0,0,2),0));
  TestFalse(TEXT("Rotated arbitrary site never migrates"),IsFormulaSite(1,F,Kind,Site,15));
 }
 FVector Site;
 TestFalse(TEXT("Negative faction rejected"),FormulaSite(-1,TEXT("TownHall"),Site));
 TestFalse(TEXT("Out-of-range faction rejected"),FormulaSite(4,TEXT("TownHall"),Site));
 TestFalse(TEXT("Other building kinds never migrate"),FormulaSite(2,TEXT("Tower"),Site));
 FormulaSite(2,TEXT("ArcheryRange"),Site);
 TestTrue(TEXT("Previous slope relocation retained"),Site.Equals(RTSCampaignTerrain::Ground(RTSCampaignFactions::Base[2]+FVector(1000,1100,0)),.001));
 TestFalse(TEXT("Old steep slope is not a near-flat ground migration"),IsFormulaSite(1,2,TEXT("ArcheryRange"),RTSCampaignTerrain::Ground(RTSCampaignFactions::Base[2]+FVector(0,3100,0)),0));
 TestFalse(TEXT("Missing world fails closed"),Sample(nullptr,TEXT("TownHall"),Site,0,Site));
 AddInfo(TEXT("FIXED_BUILDING_GROUND_POLICY_OK fixed_sites=12 player=0 arbitrary=0 steep=0"));return true;
}
#endif
