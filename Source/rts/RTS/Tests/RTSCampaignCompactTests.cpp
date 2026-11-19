#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Campaign/RTSCampaignFactions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignCompactCoordinates,"MiniRTS.Incremental.Compact.Coordinates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSCampaignCompactCoordinates::RunTest(const FString&)
{
 using namespace RTSCampaignMapSpace;
 TestTrue(TEXT("Total area reduced by exactly one third"),FMath::Abs(SizeX*SizeY/(64000.*89600.)-2./3.)<1e-12);
 TestTrue(TEXT("Preserves 7:5"),FMath::Abs(SizeY/SizeX-1.4)<1e-12);
 for(const FVector P:{FVector(-28500,-44800,0),FVector(35500,44800,1400),FVector(3500,0,900),FVector(-3600,0,82),FVector(0,2500,450)})
 {
  const FVector C=FromLegacy(P);
  TestTrue(TEXT("Legacy compact roundtrip"),ToLegacy(C).Equals(P,1e-7));
  TestEqual(TEXT("Never shrink height or capsule offset"),C.Z,P.Z);
  TestTrue(TEXT("Every legacy point stays inside compact map"),ContainsXY(C));
  const FVector2D OldUV((P.Y-LegacyMinY)/(LegacyMaxY-LegacyMinY),(LegacyMaxX-P.X)/(LegacyMaxX-LegacyMinX));
  TestTrue(TEXT("Road, minimap and authored layout retain normalized alignment"),WorldToMap(C).Equals(OldUV,1e-10));
 }
 for(int32 I=0;I<4;++I)
 {
  const FVector P=RTSCampaignFactions::Base[I],Old=RTSCampaignFactions::LegacyBase[I];
  TestTrue(TEXT("Faction remains aligned with its terrain"),ToLegacy(P).Equals(Old,1e-7));
  TestTrue(TEXT("Terrain query follows same XY transform"),FMath::Abs(RTSCampaignTerrain::HeightAt(P.X,P.Y)-RTSCampaignTerrain::LegacyHeightAt(Old.X,Old.Y))<1e-6);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignCompactMigration,"MiniRTS.Incremental.Compact.MigrationOnce",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSCampaignCompactMigration::RunTest(const FString&)
{
 auto* S=NewObject<URTSCampaignSave>();S->Version=10;
 FRTSCampaignUnitSave U;U.Feet=FVector(-23500,-19500,1400);U.HealthFraction=.45f;S->Units.Add(U);
 S->BuildingKinds={FName("Barracks"),FName("Barracks")};S->BuildingPositions={FVector(-3600,-650,0),FVector(12000,2000,0)};S->BuildingHealth={1,.7f};
 FRTSCampaignCampSave C;C.Center=FVector(-3600,-10500,0);S->Camps.Add(C);
 FRTSCampaignDailySiteSave Site;Site.Position=FVector(18000,6500,750);S->DailySites.Add(Site);
 TestTrue(TEXT("v10 migrates"),ARTSCampaign::MigrateCompactLayout(S));
 TestEqual(TEXT("Layout version advanced"),S->Version,11);
 TestTrue(TEXT("Enemy summit remains aligned"),S->Units[0].Feet.Equals(RTSCampaignFactions::Base[2],1e-7));
 TestTrue(TEXT("Additional barracks uses same transform"),S->BuildingPositions[1].Equals(RTSCampaignMapSpace::FromLegacy(FVector(12000,2000,0)),1e-7));
 TestTrue(TEXT("Camp migrated"),S->Camps[0].Center.Equals(RTSCampaignMapSpace::FromLegacy(C.Center),1e-7));
 TestTrue(TEXT("Daily resource migrated"),S->DailySites[0].Position.Equals(RTSCampaignMapSpace::FromLegacy(Site.Position),1e-7));
 TestEqual(TEXT("Injury retained"),S->Units[0].HealthFraction,.45f);
 const auto Feet=S->Units[0].Feet;const auto Buildings=S->BuildingPositions;
 TestTrue(TEXT("Current layout load accepted"),ARTSCampaign::MigrateCompactLayout(S));
 TestTrue(TEXT("Second migration does not shrink twice"),S->Units[0].Feet==Feet && S->BuildingPositions==Buildings);
 auto* Omitted=NewObject<URTSCampaignSave>();FRTSCampaignUnitSave Flat;Flat.Feet=FVector(-23500,-19500,2.15);Omitted->Units.Add(Flat);
 TestTrue(TEXT("Default-omitted historical version migrates safely"),ARTSCampaign::MigrateCompactLayout(Omitted));
 TestTrue(TEXT("Old flat summit feet lifted once, original ground offset retained"),Omitted->Units[0].Feet.Equals(RTSCampaignFactions::Base[2]+FVector(0,0,2.15),.01));
 S->Version=URTSCampaignSave::SnapshotVersion+1;TestFalse(TEXT("Future layout/schema rejected"),ARTSCampaign::MigrateCompactLayout(S));
 return true;
}
#endif
