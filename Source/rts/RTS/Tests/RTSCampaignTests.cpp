#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Tests/RTSAutomationSaveScope.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignStorage.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Campaign/RTSCampaignMinimap.h"
#include "Campaign/RTSCampaignMapView.h"
#include "Camera/RTSCameraPawn.h"
#include "Campaign/RTSCampaignWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Economy/RTSGathererComponent.h"
#include "Units/RTSUnit.h"
#include "Combat/RTSHealthComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSProducerComponent.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Misc/Paths.h"
#include "Materials/Material.h"
#include "Campaign/RTSCampaignPalette.h"
#include "GameFramework/Pawn.h"
#include "Components/StaticMeshComponent.h"
#include "Campaign/RTSCampaignMapSpace.h"
#include "Engine/StaticMesh.h"
#include "Core/RTSHumanController.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Campaign/RTSCampaignProgression.h"
#include "Selection/RTSSelectionComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Border.h"
#include "Components/TextRenderComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "UI/RTSHUD.h"
#include "HAL/FileManager.h"
#include "Camera/RTSCameraPawn.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Tests/RTSCampaignUIAssertions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignCostTest,"MiniRTS.Incremental.CostAndPlans",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRTSCampaignCostTest::RunTest(const FString&)
{
 TestTrue(TEXT("Swordman level two costs wood and food"),[](){auto C=RTSCampaignProgression::UpgradeCost(TEXT("Swordman"),2);return C.FindRef(ERTSResource::Wood)>0 && C.FindRef(ERTSResource::Food)>0;}());
 TestTrue(TEXT("Late upgrade adds gold and mana"),[](){auto C=RTSCampaignProgression::UpgradeCost(TEXT("Swordman"),30);return C.FindRef(ERTSResource::Gold)>0 && C.FindRef(ERTSResource::Mana)>0;}());
 TestEqual(TEXT("Day1 economic army cap split swords"),ARTSCampaign::RaidPlan(0,1).Swords,3);TestEqual(TEXT("Day1 economic army cap split archers"),ARTSCampaign::RaidPlan(0,1).Archers,3);
 TestEqual(TEXT("First economic launch window"),FMath::RoundToInt(ARTSCampaign::RaidPlan(0,1).Launch),150);
 TestTrue(TEXT("Early raids target the base outskirts"),FMath::IsNearlyEqual(FVector::Dist2D(ARTSCampaign::RaidPlan(0,1).Target,ARTSCampaign::HomeLocation()),FVector2D(2200,1400).Size(),.01));
 TestTrue(TEXT("Second day retains an outskirts window"),FVector::Dist2D(ARTSCampaign::RaidPlan(1,1).Target,ARTSCampaign::HomeLocation())>2000);
 TestEqual(TEXT("All factions share the paid roster cap"),ARTSCampaign::RaidPlan(0,0).Swords,3);
 TestEqual(TEXT("Day2 economic ranged cap"),ARTSCampaign::RaidPlan(1,0).Archers,4);
 TestEqual(TEXT("Late economic cap is eighteen total"),ARTSCampaign::RaidPlan(99,1).Swords+ARTSCampaign::RaidPlan(99,1).Archers,18);
 TestEqual(TEXT("Enemy level equals campaign day"),ARTSCampaign::EnemyLevelForDay(0),1);
 TestEqual(TEXT("Day ten enemy reaches level ten"),ARTSCampaign::EnemyLevelForDay(9),10);
 TestEqual(TEXT("Tier bonus raises enemy level"),ARTSCampaign::EnemyLevelForDayAndTier(2,3),5);
 TestEqual(TEXT("Early enemy roster is basic"),ARTSCampaign::EnemyKindForDay(0,0,false),FName("Swordman"));
 TestEqual(TEXT("Early enemy roster adds light infantry"),ARTSCampaign::EnemyKindForDay(0,1,false),FName("Light_Infantry"));
 TestEqual(TEXT("Late enemy roster reaches kings"),ARTSCampaign::EnemyKindForDay(6,7,false),FName("King"));
 TestEqual(TEXT("Late enemy roster reaches mages"),ARTSCampaign::EnemyKindForDay(6,3,true),FName("Mage"));
 TestEqual(TEXT("Early wave budget is one"),ARTSCampaign::EnemyWaveLimit(0),1);
 TestEqual(TEXT("Late campaign wave budget is three"),ARTSCampaign::EnemyWaveLimit(14),3);
 TestTrue(TEXT("Late waves outnumber early waves"),ARTSCampaign::RaidPlan(8,1).Swords>ARTSCampaign::RaidPlan(2,1).Swords);
 TestTrue(TEXT("Later economic raids target the actual headquarters"),ARTSCampaign::RaidPlan(8,1).Target.Equals(ARTSCampaign::HomeLocation(),.01));
 for(const TCHAR* Name:{TEXT("Purple"),TEXT("Yellow"),TEXT("Red"),TEXT("Pink"),TEXT("Teal")})
 {
  const FString Variant=FString(Name)==TEXT("Teal")?TEXT("GreenB"):Name;
  const FString Path=FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Materials/Units/MI_Units_%s.MI_Units_%s"),*Variant,*Variant);
  auto* Material=LoadObject<UMaterialInterface>(nullptr,*Path);
  TestTrue(TEXT("Textured faction atlas supports skeletal and Nanite"),Material && Material->GetMaterial()->CheckMaterialUsage_Concurrent(MATUSAGE_SkeletalMesh) && Material->GetMaterial()->CheckMaterialUsage_Concurrent(MATUSAGE_Nanite));
 }
 TestNotNull(TEXT("Original advisor texture exists"),LoadObject<UTexture2D>(nullptr,TEXT("/Game/RTS/UI/T_AdvisorElara.T_AdvisorElara")));
 const FVector Center(RTSCampaignMapSpace::CenterX,0,0);
 TestTrue(TEXT("Map center"),RTSCampaignMapSpace::WorldToMap(Center).Equals(FVector2D(.5,.5),1e-6));
 TestTrue(TEXT("World +X appears up"),RTSCampaignMapSpace::WorldToMap(Center+FVector(1000,0,0)).Y<.5);
 TestTrue(TEXT("World +Y appears right"),RTSCampaignMapSpace::WorldToMap(Center+FVector(0,1000,0)).X>.5);
 TestTrue(TEXT("Map click roundtrip"),RTSCampaignMapSpace::MapToWorld(RTSCampaignMapSpace::WorldToMap(Center+FVector(7000,6000,0))).Equals(Center+FVector(7000,6000,0),.01));
 TestTrue(TEXT("Compact total area is two thirds of baseline"),FMath::Abs(RTSCampaignMapSpace::SizeX*RTSCampaignMapSpace::SizeY/(64000.*89600.)-2./3.)<1e-12);
 TestTrue(TEXT("Horizontal to vertical ratio 7:5"),FMath::Abs(RTSCampaignMapSpace::SizeY/RTSCampaignMapSpace::SizeX-1.4)<1e-12);
 FRTSCampaignMapView View;const FVector2D Anchor(.6,.4),Before=View.ToMap(Anchor);View.ZoomAt(3,Anchor);
 TestTrue(TEXT("Minimap zoom preserves mouse anchor"),View.ToMap(Anchor).Equals(Before,1e-6));
 TestTrue(TEXT("Zoomed minimap coordinate roundtrip"),View.ToLocal(View.ToMap(Anchor)).Equals(Anchor,1e-6));
 View.ZoomAt(100,Anchor);TestEqual(TEXT("Minimap maximum zoom"),View.Zoom,4.0);
 View.Pan(FVector2D(100,-100));TestTrue(TEXT("Minimap drag stays inside map"),View.Center.X>=.125 && View.Center.Y<=.875);
 View.Reset();TestTrue(TEXT("Minimap reset restores full map"),View.Zoom==1 && View.Center.Equals(FVector2D(.5,.5)));
 TestEqual(TEXT("Red uses the same finite economic cap"),ARTSCampaign::RaidPlan(0,2).Swords,3);
 TestEqual(TEXT("Pink second day ranged cap"),ARTSCampaign::RaidPlan(1,3).Archers,4);
 TestEqual(TEXT("Five minute campaign day"),ARTSCampaign::DaySeconds,300.f);
 bool Seen[8]={false};for(int32 D=0;D<8;++D)for(int32 S=0;S<4;++S)Seen[int32(ARTSCampaign::DailySiteTypeForDay(D,S))]=true;
 int32 Missing=0;for(int32 I=0;I<8;++I)Missing+=Seen[I]?0:1;
 TestEqual(TEXT("Daily slot rotation covers all eight materials"),Missing,0);
 int32 Kinds=0,Levels=0;bool ServiceOk=true,GrowthOk=true,HealerOk=true,CostOk=true;
 for(int32 I=0;I<RTSCampaignProgression::Count;++I)
 {
  const FName K=RTSCampaignProgression::Kind(I);++Kinds;
  ServiceOk&=!RTSCampaignProgression::Service(K).IsNone() && !RTSCampaignProgression::TrainCost(K).IsEmpty();
  if(K==TEXT("Priest") || K==TEXT("HighPriest"))HealerOk&=RTSCampaignProgression::BaseDamage(K)==0 && RTSCampaignProgression::BaseHeal(K)>0;
  for(int32 L=1;L<=30;++L)
  {
   ++Levels;const float M=RTSCampaignProgression::Growth(L);
   GrowthOk&=FMath::IsFinite(M) && RTSCampaignProgression::BaseHealth(K)*M>0 && (L==1 || M>RTSCampaignProgression::Growth(L-1));
   if(L>1)CostOk&=!RTSCampaignProgression::UpgradeCost(K,L).IsEmpty();
  }
 }
 TestEqual(TEXT("Kinds"),Kinds,16);TestEqual(TEXT("Levels"),Levels,480);
 TestTrue(TEXT("Every kind has producer and price"),ServiceOk);
 TestTrue(TEXT("Growth finite and monotonic"),GrowthOk);
 TestTrue(TEXT("Healers heal without damage"),HealerOk);
 TestTrue(TEXT("Every level above one costs something"),CostOk);
 TestEqual(TEXT("Paladin unlock requires crystal"),RTSCampaignProgression::ResearchCost(TEXT("Paladin")).FindRef(ERTSResource::Crystal),140);
 TestEqual(TEXT("Mage unlock requires mana"),RTSCampaignProgression::ResearchCost(TEXT("Mage")).FindRef(ERTSResource::Mana),160);
 TestEqual(TEXT("Commander unlock requires gold"),RTSCampaignProgression::ResearchCost(TEXT("Commander")).FindRef(ERTSResource::Gold),200);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSCampaignIntegrationTest,"MiniRTS.Incremental.GuidanceAndWar",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
namespace {
// Private automation slot: the production save slot stays untouched by test runs.
FString TestSlotStorage;const TCHAR* TestSlot=nullptr;
bool WriteTestSnapshot(URTSCampaignSave* Save){TArray<uint8> Bytes;return RTSCampaignStorage::Encode(Save,Bytes) && UGameplayStatics::SaveDataToSlot(Bytes,TestSlot,0);}
class FCampaignCheck : public IAutomationLatentCommand
{
 FAutomationTestBase* T; double Start=FPlatformTime::Seconds(),Phase=0; int32 Stage=0,PortraitFaction=0; TWeakObjectPtr<ARTSUnit> Worker,PortraitUnit; TWeakObjectPtr<ACameraActor> PortraitCamera; FVector WorkerStart; bool Moved=false,ResearchChecked=false,VineyardChecked=false; TWeakObjectPtr<URTSCampaignWidget> Widget; TStrongObjectPtr<URTSCampaignSave> Baseline;
 bool PauseChecked=false;double PauseRealStart=0,PausedElapsed=0;
 bool SettleChecked=false;double SettleStart=0,SettleAttempt=0;
 bool UIFeedbackChecked=false;
public:
 explicit FCampaignCheck(FAutomationTestBase* In):T(In) {}
 bool Update() override
 {
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr; ARTSCampaign* C=W?ARTSCampaign::Get(W):nullptr;
  if(FPlatformTime::Seconds()-Start>320) { T->AddError(FString::Printf(TEXT("Campaign stage%d timeout guide%d"),Stage,C?C->GetGuideStep():-1)); UGameplayStatics::DeleteGameInSlot(TestSlot,0); return true; }
  if(!C || !C->IsReady()) return false;
  auto* Stock=URTSBalanceSubsystem::Get(W)->GetResources(0);
  if(Stage==0)
  {
   T->TestNotNull(TEXT("TownHall exists at new game"),C->FindServiceBuilding(TEXT("TownHall")));
   for(FName Kind:{FName("Barracks"),FName("ArcheryRange"),FName("ResearchInstitute"),FName("SupportBarracks")})T->TestNull(TEXT("Services must be built by workers"),C->FindServiceBuilding(Kind));
   int32 Nodes=0;bool MeshOk=true,NoCollisionOk=true;
   for(TActorIterator<ARTSResourceNode> It(W);It;++It)
   {
    ++Nodes;TArray<UStaticMeshComponent*> Parts;It->GetComponents(Parts);bool Found=false;
    for(auto* Part:Parts)if(Part->GetStaticMesh())
    {Found|=Part->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Game/RTS/CampaignArt/")) || Part->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Game/RTS/Environment/")) || Part->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Engine/BasicShapes/"));NoCollisionOk&=Part->GetCollisionEnabled()==ECollisionEnabled::NoCollision;}
    MeshOk&=Found;
   }
   T->TestEqual(TEXT("Eight shared and thirty-two faction resource nodes"),Nodes,40);
   T->TestTrue(TEXT("Campaign resource meshes rebuilt"),MeshOk);
   T->TestTrue(TEXT("Resource visuals stay non-colliding"),NoCollisionOk);
   int32 Counts[4]={0,0,0,0},CornerCounts[4]={0,0,0,0},Workers=0;bool FactionOk=true,CornerOk=true,AtlasOk=true,IdleOk=true;
   for(TActorIterator<ARTSUnit> It(W);It;++It)
   {
    if(It->GetTeam()==1) {
     const int32 F=C->GetUnitFaction(*It);if(F<0 || F>3){FactionOk=false;continue;}++Counts[F];
     if(FVector::Dist2D(It->GetActorLocation(),RTSCampaignFactions::Base[F])<2000)++CornerCounts[F];
     CornerOk&=FVector::Dist2D(It->GetActorLocation(),RTSCampaignFactions::Base[F])<5000;
     const int32 Cloth=It->GetMesh()->GetMaterialIndex(TEXT("TT_RTS_Units"));
     AtlasOk&=Cloth!=INDEX_NONE && It->GetMesh()->GetMaterial(Cloth) && It->GetMesh()->GetMaterial(Cloth)->GetName()==TEXT("MI_Units_")+RTSCampaignPalette::Name(F);
    }
    else if(It->GetUnitData()->bWorker) { ++Workers; Worker=*It; IdleOk&=It->GetCurrentOrder().Type==ERTSOrderType::None; }
   }
   T->TestTrue(TEXT("Every enemy carries a faction"),FactionOk);
   T->TestTrue(TEXT("Every guard holds its corner"),CornerOk);
   T->TestTrue(TEXT("Faction atlases assigned"),AtlasOk);
   T->TestTrue(TEXT("Workers start idle"),IdleOk);
   T->TestEqual(TEXT("Workers"),Workers,6);
   const int32 Expected[]={34,38,14,14},Corners[]={20,20,14,14};
   for(int32 F=0;F<4;++F){T->TestEqual(TEXT("Faction garrison"),Counts[F],Expected[F]);T->TestEqual(TEXT("Corner initial garrison"),CornerCounts[F],Corners[F]);}
   auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);bool EdgeOk=true,ReachOk=true;int32 Edges=0;
   for(FVector Edge:{RTSCampaignMapSpace::FromLegacy(FVector(-27500,0,0)),RTSCampaignMapSpace::FromLegacy(FVector(34500,0,0)),RTSCampaignMapSpace::FromLegacy(FVector(3500,-43800,0)),RTSCampaignMapSpace::FromLegacy(FVector(3500,43800,0)),RTSCampaignFactions::Base[0],RTSCampaignFactions::Base[1],RTSCampaignFactions::Base[2],RTSCampaignFactions::Base[3]})
   {
    const FVector Ground=Edge.Z==0?RTSCampaignTerrain::Ground(Edge):Edge;
    ++Edges;FNavLocation Point;EdgeOk&=Nav && Nav->ProjectPointToNavigation(Ground,Point,FVector(150,150,200));
    auto* EdgePath=Nav?Nav->FindPathToLocationSynchronously(W,ARTSCampaign::HomeLocation()+FVector(0,0,10),Ground):nullptr;
    ReachOk&=EdgePath && EdgePath->IsValid() && !EdgePath->IsPartial();
   }
   T->TestEqual(TEXT("Expanded map edges sampled"),Edges,8);
   auto* CampaignCamera=Cast<ARTSCameraPawn>(W->GetFirstPlayerController()->GetPawn());T->TestTrue(TEXT("Campaign main camera can zoom out to 90m"),CampaignCamera && CampaignCamera->GetMaximumZoomDistance()==9000.f);
   T->TestTrue(TEXT("Expanded edges have navigation and are reachable"),EdgeOk && ReachOk);
   FNavLocation Summit;T->TestTrue(TEXT("Southwest red faction spawns on 14m mountain"),Nav && Nav->ProjectPointToNavigation(RTSCampaignFactions::Base[2],Summit,FVector(100,100,100)) && Summit.Location.Z>1300);
   for(const FVector Exit:{FVector(-16000,-19500,0),FVector(-23500,-12500,0)})
   {
    auto* MountainPath=Nav?Nav->FindPathToLocationSynchronously(W,RTSCampaignFactions::Base[2],RTSCampaignMapSpace::FromLegacy(Exit)):nullptr;
    T->TestTrue(TEXT("Mountain summit has connected traversable descent"),MountainPath && MountainPath->IsValid() && !MountainPath->IsPartial());
   }
   for(const FVector Hill:{FVector(-13000,6500,850),FVector(18000,6500,750),FVector(18500,-6500,650),FVector(5000,17500,500)})
   {
    const FVector CompactHill=RTSCampaignMapSpace::FromLegacy(Hill);
    FNavLocation Point;T->TestTrue(TEXT("Frontier hill has real elevated navigation"),Nav && Nav->ProjectPointToNavigation(CompactHill,Point,FVector(150,150,150)) && FMath::Abs(Point.Location.Z-Hill.Z)<100);
    auto* HillPath=Nav?Nav->FindPathToLocationSynchronously(W,ARTSCampaign::HomeLocation()+FVector(0,0,10),CompactHill):nullptr;
    T->TestTrue(TEXT("Frontier hill connected to home"),HillPath && HillPath->IsValid() && !HillPath->IsPartial());
   }
   // Scenery is decoration only, and campaign labels are HUD-drawn: no 3D text may remain anywhere.
   int32 SceneryActors=0,SceneryInstances=0;bool SceneryOk=true,NoTextRender=true,LabelsOk=true;
   TMap<FIntPoint,int32> ScenerySectors;
   for(TActorIterator<AActor> It(W);It;++It)
   {
    TArray<UInstancedStaticMeshComponent*> Parts;It->GetComponents(Parts);if(!Parts.Num()) continue;
    bool IsScenery=false;
    for(auto* Part:Parts) IsScenery|=Part->GetStaticMesh() && Part->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Game/RTS/Environment/"));
    if(!IsScenery) continue;
    ++SceneryActors;
    for(auto* Part:Parts)
    {
     SceneryInstances+=Part->GetInstanceCount();
     SceneryOk&=Part->GetCollisionEnabled()==ECollisionEnabled::NoCollision && !Part->CanEverAffectNavigation() && Part->GetInstanceCount()>0;
     for(int32 Instance=0;Instance<Part->GetInstanceCount();++Instance)
     {
      FTransform Transform;if(!Part->GetInstanceTransform(Instance,Transform,true))continue;
      const FVector P=Transform.GetLocation();if(!RTSCampaignMapSpace::ContainsXY(P))continue;
      const FIntPoint Sector(FMath::Clamp(FMath::FloorToInt((P.X-RTSCampaignMapSpace::MinX)/(RTSCampaignMapSpace::SizeX/12.)),0,11),FMath::Clamp(FMath::FloorToInt((P.Y-RTSCampaignMapSpace::MinY)/(RTSCampaignMapSpace::SizeY/17.)),0,16));
      ++ScenerySectors.FindOrAdd(Sector);
     }
    }
   }
   T->TestEqual(TEXT("Instanced scenery actor"),SceneryActors,1);
   T->TestTrue(TEXT("Sparse scenery leaves open map instead of 29865 dense instances"),SceneryInstances>500 && SceneryInstances<5000);
   int32 DressedSectors=0;for(const auto& Sector:ScenerySectors)if(Sector.Value>=3)++DressedSectors;
   T->TestTrue(TEXT("Sparse landmarks still span the compact map"),DressedSectors>=80);
   UE_LOG(LogTemp,Log,TEXT("CAMPAIGN_SPARSE_SCENERY sectors=%d/204 instances=%d"),DressedSectors,SceneryInstances);
   T->TestTrue(TEXT("Scenery never collides or blocks navigation"),SceneryOk);
   T->TestEqual(TEXT("Decoration count matches instanced scenery"),C->GetDecorationCount(),SceneryInstances);
   for(TActorIterator<AActor> It(W);It;++It) { TArray<UTextRenderComponent*> Texts;It->GetComponents(Texts);NoTextRender&=Texts.Num()==0; }
   for(TActorIterator<ARTSResourceNode> It(W);It;++It) LabelsOk&=It->GetWorldLabelText().Contains(TEXT("级"));
   T->TestTrue(TEXT("No 3D text components in the campaign"),NoTextRender);
   T->TestTrue(TEXT("Resource world labels are Chinese"),LabelsOk);
   if(auto* PC=W->GetFirstPlayerController()) if(auto* HUD=Cast<ARTSHUD>(PC->GetHUD()))
   {
    T->TestTrue(TEXT("HUD uses a runtime CJK font"),HUD->IsCjkFontRuntime());
    T->TestTrue(TEXT("HUD CJK font file exists"),IFileManager::Get().FileExists(*HUD->GetCjkFontFile()));
   }
   auto* Path=Nav->FindPathToLocationSynchronously(W,RTSCampaignMapSpace::FromLegacy(FVector(7900,3600,910)),RTSCampaignMapSpace::FromLegacy(FVector(-1800,2500,10)));
   bool Ramp=false; if(Path) for(auto P:Path->PathPoints){const FVector Legacy=RTSCampaignMapSpace::ToLegacy(P);Ramp|=Legacy.X<1500 && Legacy.Y>1800 && Legacy.Y<3200;}
   T->TestTrue(TEXT("Expanded purple territory connected via west ramp"),Path && Path->IsValid() && !Path->IsPartial() && Ramp);
   TArray<UUserWidget*> Widgets; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W,Widgets,URTSCampaignWidget::StaticClass(),false);
   if(Widgets.Num()) Widget=Cast<URTSCampaignWidget>(Widgets[0]);
   T->TestTrue(TEXT("Campaign UI exists"),Widget.IsValid());
   if(Widget.IsValid())
   {
    bool PanelOk=true,ActionsOk=true,PortraitOk=true;
    for(FName Kind:{FName("TownHall")})
    {
     C->SelectServiceBuilding(Kind);Widget->RefreshSelectedObject();
     PanelOk&=Widget->GetDisplayedBuilding()==Kind;
     ActionsOk&=Widget->IsOnlyServiceVisible(Kind);
     PortraitOk&=Widget->HasBuildingPortrait();
    }
    T->TestTrue(TEXT("Selected building drives right panel"),PanelOk);
    T->TestTrue(TEXT("Only selected service actions visible"),ActionsOk);
    T->TestTrue(TEXT("Matching building images loaded"),PortraitOk);
    if(auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController()))PC->SelectBuilding(nullptr);
    Widget->RefreshSelectedObject();T->TestTrue(TEXT("No selection hides all service actions"),Widget->IsOnlyServiceVisible(NAME_None));
   }
   Phase=W->GetTimeSeconds(); Stage=1; return false;
  }
  if(Stage==1)
  {
   if(W->GetTimeSeconds()-Phase<3) return false;
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignArtWelcome.png"),true,false);
   T->TestEqual(TEXT("No automatic gathering"),Stock->GetAmount(ERTSResource::Wood),500); T->TestTrue(TEXT("Tutorial freezes campaign clock"),C->GetElapsed()==0);
   C->AdvanceGuide(); Worker->SetSelected(true); Stage=2; return false;
  }
  if(Stage==2)
  {
   if(C->GetGuideStep()<2) return false;
   if(!Moved) { WorkerStart=Worker->GetActorLocation(); FRTSOrder O;O.Type=ERTSOrderType::Move;O.TargetLocation=WorkerStart+FVector(0,350,-82);Worker->ReceiveOrder(O);Moved=true; }
   if(FVector::Dist2D(Worker->GetActorLocation(),WorkerStart)<210) return false;
   if(C->GetGuideStep()<3) return false;
   ARTSResourceNode* Wood=nullptr;double BestWood=MAX_dbl;for(TActorIterator<ARTSResourceNode> It(W);It;++It)if(It->GetResourceType()==ERTSResource::Wood){const double D=FVector::DistSquared2D(It->GetActorLocation(),ARTSCampaign::HomeLocation());if(D<BestWood){BestWood=D;Wood=*It;}}
   FRTSOrder O;O.Type=ERTSOrderType::Gather;O.TargetActor=Wood;Worker->ReceiveOrder(O);Stage=3;return false;
  }
  if(Stage==3)
  {
   if(C->GetGuideStep()<4) return false;
   T->TestTrue(TEXT("Actual wood deposited"),Stock->GetAmount(ERTSResource::Wood)>=510);
   // Explicit paid service fixtures for the remaining UI/persistence checks.
   // Natural worker construction is independently covered by CurrentTutorialAndVictory.
   Stock->Add(ERTSResource::Wood,1000);
   for(FName Kind:{FName("ResearchInstitute"),FName("Barracks"),FName("ArcheryRange"),FName("SupportBarracks")})
   {
    FString Error;ARTSBuilding* Built=nullptr;
    for(int32 R=0;R<3 && !Built;++R)for(int32 I=0;I<12 && !Built;++I)
    {const double A=I*UE_PI/6,Radius=1400+R*500;Built=URTSBalanceSubsystem::Get(W)->PlaceBuilding(Kind,0,RTSCampaignTerrain::Ground(C->HomeLocation()+FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0)),Error);}
    if(!Built){T->AddError(TEXT("Paid UI fixture failed: ")+Error);return true;}Built->AddConstructionWork(1000);
   }
   T->AddInfo(TEXT("CAMPAIGN_UI_FIXTURE paidServices=4 instantCompletionFixture=1 naturalBuildClaim=0"));
   C->PrepareSave(); if(Widget.IsValid()) Widget->OpenPage(1); else C->NotifyPageOpened(1);
   C->SelectServiceBuilding(TEXT("ResearchInstitute"));
   if(Widget.IsValid())Widget->RefreshSelectedObject();
   Phase=W->GetTimeSeconds();Stage=30;return false;
  }
  if(Stage==30)
  {
   if(W->GetTimeSeconds()-Phase<2)return false;
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignArtResearch.png"),true,false);
   T->TestTrue(TEXT("Paid upgrade"),C->Upgrade(TEXT("Swordman")));Phase=W->GetTimeSeconds();Stage=31;return false;
  }
  if(Stage==31)
  {
   if(W->GetTimeSeconds()-Phase<1)return false;
   if(Widget.IsValid())Widget->ShowUnitResearch();Phase=W->GetTimeSeconds();Stage=32;return false;
  }
  if(Stage==32)
  {
   if(W->GetTimeSeconds()-Phase<2)return false;
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignResearchAligned.png"),true,false);
   Phase=W->GetTimeSeconds();Stage=33;return false;
  }
  if(Stage==33)
  {
   if(W->GetTimeSeconds()-Phase<1)return false;
   if(Widget.IsValid())Widget->ShowUnitUpgrades();Stage=4;return false;
  }
  if(Stage==4)
  {
   if(C->GetGuideStep()<5) return false;
   if(Widget.IsValid()) Widget->OpenPage(2); else C->NotifyPageOpened(2);
   C->SelectServiceBuilding(TEXT("Barracks"));
   if(Widget.IsValid())Widget->RefreshSelectedObject();
   Phase=W->GetTimeSeconds();Stage=40;return false;
  }
  if(Stage==40)
  {
   if(W->GetTimeSeconds()-Phase<2)return false;
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignArtBarracks.png"),true,false);
   T->TestTrue(TEXT("Train real recruit"),C->Train(TEXT("Swordman")));Phase=W->GetTimeSeconds();Stage=41;return false;
  }
  if(Stage==41)
  {
   if(W->GetTimeSeconds()-Phase<.7)return false;
   if(Widget.IsValid())Widget->RefreshSelectedObject();FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignQueuePortrait.png"),true,false);Stage=5;return false;
  }
  if(Stage==5)
  {
   if(C->GetGuideStep()<6) return false;
   if(Widget.IsValid()) Widget->OpenPage(0);else C->NotifyPageOpened(0);
   C->AdvanceGuide(); T->TestEqual(TEXT("All guide actions completed"),C->GetGuideStep(),7); C->PrepareSave(); Phase=W->GetTimeSeconds();Stage=6;return false;
  }
  if(Stage==66)
  {
   if(FPlatformTime::Seconds()-PauseRealStart<1.2)return false;
   T->TestEqual(TEXT("Pause freezes campaign clock"),C->GetElapsed(),PausedElapsed);
   if(Widget.IsValid()){Widget->RefreshSelectedObject();T->TestTrue(TEXT("Pause overlay visible"),Widget->IsPauseMenuVisible());Widget->TogglePause();T->TestFalse(TEXT("Resume clears overlay"),Widget->IsPauseMenuVisible());}
   else Cast<ARTSHumanController>(W->GetFirstPlayerController())->ToggleGamePause();
   T->TestFalse(TEXT("Resume unpauses world"),UGameplayStatics::IsGamePaused(W));PauseChecked=true;Phase=W->GetTimeSeconds();Stage=6;return false;
  }
  if(Stage==67)
  {
   if(W->GetTimeSeconds()-Phase<1)return false;
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignWarLayoutFixed.png"),true,false);Phase=W->GetTimeSeconds();Stage=68;return false;
  }
  if(Stage==68)
  {
   if(W->GetTimeSeconds()-Phase<.5)return false;
   Widget->ShowSettings();PauseRealStart=FPlatformTime::Seconds();Stage=69;return false;
  }
  if(Stage==69)
  {
   if(FPlatformTime::Seconds()-PauseRealStart<1)return false;
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignGearSettings.png"),true,false);PauseRealStart=FPlatformTime::Seconds();Stage=690;return false;
  }
  if(Stage==690)
  {
   if(FPlatformTime::Seconds()-PauseRealStart<.5)return false;
   auto* Settings=Cast<URTSCampaignSettingsWidget>(Widget->WidgetTree->FindWidget(TEXT("CampaignSettings")));if(Settings)Settings->CloseSettings();
   Widget->RefreshSelectedObject();UIFeedbackChecked=true;Phase=W->GetTimeSeconds();Stage=6;return false;
  }
  if(Stage==6)
  {
   if(W->GetTimeSeconds()-Phase<2) return false;
   if(!PauseChecked){PausedElapsed=C->GetElapsed();PauseRealStart=FPlatformTime::Seconds();if(Widget.IsValid())Widget->TogglePause();else Cast<ARTSHumanController>(W->GetFirstPlayerController())->ToggleGamePause();T->TestTrue(TEXT("Pause freezes world"),UGameplayStatics::IsGamePaused(W));Stage=66;return false;}
   if(!UIFeedbackChecked && Widget.IsValid())
   {
    RTSCampaignUIAssertions::Check(T,Widget.Get());Widget->OpenPage(0);Phase=W->GetTimeSeconds();Stage=67;return false;
   }
   if(!ResearchChecked)
   {
    C->SelectServiceBuilding(TEXT("TownHall"));
    const int32 BeforeWood=Stock->GetAmount(ERTSResource::Wood);
    if(Widget.IsValid())
    {
     auto* Upgrade=Widget->WidgetTree->FindWidget(TEXT("UnitUpgradeList"));auto* Research=Widget->WidgetTree->FindWidget(TEXT("UnitResearchList"));
     TArray<UWidget*> Rows;Widget->WidgetTree->GetAllWidgets(Rows);int32 IconRows=0;bool FillOk=true,AlignOk=true;
     for(auto* Item:Rows)if(auto* Button=Cast<UButton>(Item))if(auto* Row=Cast<UHorizontalBox>(Button->GetContent()))if(Row->GetChildrenCount()==2 && Cast<UTextBlock>(Row->GetChildAt(1)))
     {
      const auto* ButtonSlot=Cast<UButtonSlot>(Row->Slot);FillOk&=ButtonSlot && ButtonSlot->GetHorizontalAlignment()==HAlign_Fill;
      auto* TextSlot=Cast<UHorizontalBoxSlot>(Row->GetChildAt(1)->Slot);auto* IconSlot=Cast<UHorizontalBoxSlot>(Row->GetChildAt(0)->Slot);
      AlignOk&=TextSlot && IconSlot && TextSlot->GetSize().SizeRule==ESlateSizeRule::Fill && TextSlot->GetVerticalAlignment()==VAlign_Center && IconSlot->GetPadding().Right==10 && IconSlot->GetVerticalAlignment()==VAlign_Center;++IconRows;
     }
     T->TestTrue(TEXT("Research and upgrade icon rows present"),IconRows>=20);T->TestTrue(TEXT("Icon rows fill width and align"),FillOk && AlignOk);
     Widget->ShowUnitResearch();T->TestTrue(TEXT("Research tab hides upgrades"),Widget->IsUnitResearchPage() && Upgrade && Research && Upgrade->GetVisibility()==ESlateVisibility::Collapsed && Research->GetVisibility()==ESlateVisibility::Visible);
     Widget->ShowUnitUpgrades();T->TestTrue(TEXT("Upgrade tab hides research"),!Widget->IsUnitResearchPage() && Upgrade && Research && Upgrade->GetVisibility()==ESlateVisibility::Visible && Research->GetVisibility()==ESlateVisibility::Collapsed);
    }
    if(Widget.IsValid())
    {
     TMap<ERTSResource,int32> Original;for(int32 I=0;I<int32(ERTSResource::Count);++I)Original.Add(ERTSResource(I),Stock->GetAmount(ERTSResource(I)));
     auto CheckColors=[&](bool Enough){Widget->RefreshSelectedObject();TArray<UWidget*> All;Widget->WidgetTree->GetAllWidgets(All);int32 Checked=0;for(auto* Item:All)if(auto* Text=Cast<UTextBlock>(Item)){const FString Caption=Text->GetText().ToString();if(Caption==C->GetUpgradeText(TEXT("Swordman")) || Caption==C->GetResearchText(TEXT("Spearman"))){const auto Color=Text->GetColorAndOpacity().GetSpecifiedColor();T->TestTrue(TEXT("Upgrade and research reflect affordability"),Enough?Color.G>Color.R:Color.R>Color.G);++Checked;}}T->TestTrue(TEXT("Upgrade and research labels found"),Checked>=2);};
     TMap<ERTSResource,int32> Empty,Full;for(int32 I=0;I<int32(ERTSResource::Count);++I){Empty.Add(ERTSResource(I),0);Full.Add(ERTSResource(I),5000);}
     Stock->SetAmounts(Empty);CheckColors(false);Stock->SetAmounts(Full);CheckColors(true);
     bool CountersOk=true,IconsOk=true;
     for(int32 I=0;I<8;++I)
     {
      auto* Text=Cast<UTextBlock>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("CampaignStock%d"),I))));
      CountersOk&=Text && Text->GetText().EqualTo(FText::AsNumber(5000));
      auto* Icon=Cast<UImage>(Widget->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("CampaignStockIcon%d"),I))));
      IconsOk&=Icon && Icon->GetBrush().GetResourceObject()!=nullptr;
     }
     T->TestTrue(TEXT("Eight stock counters update"),CountersOk);
     T->TestTrue(TEXT("Eight resource icons have real textures"),IconsOk);
     Stock->SetAmounts(Original);Widget->RefreshSelectedObject();
    }
    T->TestFalse(TEXT("Wrong building cannot upgrade"),C->Upgrade(TEXT("Swordman")));
    T->TestFalse(TEXT("Wrong building cannot research"),C->ResearchUnit(TEXT("Spearman")));
    T->TestFalse(TEXT("Wrong building cannot train sword"),C->Train(TEXT("Swordman")));
    T->TestEqual(TEXT("Wrong service never charges"),Stock->GetAmount(ERTSResource::Wood),BeforeWood);
    Stock->Add(ERTSResource::Wood,1000);Stock->Add(ERTSResource::Food,1000);
    C->SelectServiceBuilding(TEXT("ResearchInstitute"));
    for(FName Kind:{FName("Spearman"),FName("Scout"),FName("Priest")})T->TestTrue(TEXT("Paid unit research"),C->ResearchUnit(Kind));
    const int32 PaidWood=Stock->GetAmount(ERTSResource::Wood);
    C->ResearchUnit(TEXT("Spearman"));T->TestEqual(TEXT("Repeat research does not charge"),Stock->GetAmount(ERTSResource::Wood),PaidWood);
    for(FName Kind:{FName("Spearman"),FName("Scout"),FName("Priest")})
    { C->SelectServiceBuilding(C->TrainingService(Kind));T->TestTrue(TEXT("Unlocked unit enters correct queue"),C->Train(Kind)); }
    ResearchChecked=true;Phase=W->GetTimeSeconds();return false;
   }
  if(W->GetTimeSeconds()-Phase<8)return false;
  // A squad that just left a production queue can still be passing or yielding, and the snapshot guard accepts only a settled record.
  if(!SettleChecked)
  {
   if(SettleStart<=0.0)SettleStart=W->GetTimeSeconds();
   if(W->GetTimeSeconds()-SettleAttempt>=0.5)
   {
    SettleAttempt=W->GetTimeSeconds();
    if(C->SaveCampaign())SettleChecked=true;
   }
   if(!SettleChecked)
   {
    if(W->GetTimeSeconds()-SettleStart<20)return false;
    T->TestTrue(TEXT("Safe save"),false);return true;
   }
  }
   for(FName Kind:{FName("Spearman"),FName("Scout"),FName("Priest")})
   {
    bool Found=false;for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->GetTeam()==0 && It->IsAlive() && It->GetUnitData()->BalanceId==Kind)Found=true;
    T->TestTrue(TEXT("Unlocked recruit actually spawned"),Found);
   }
   if(auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController()))
   {
    ARTSResourceNode* Food=nullptr;for(TActorIterator<ARTSResourceNode> It(W);It;++It)if(It->GetResourceType()==ERTSResource::Food)Food=*It;
    PC->SelectResource(Food);if(Widget.IsValid()){Widget->RefreshSelectedObject();T->TestTrue(TEXT("Resource selection shows image and details"),Widget->IsResourceDetailsVisible());}
    PC->SelectBuilding(nullptr);if(Widget.IsValid()){Widget->RefreshSelectedObject();T->TestFalse(TEXT("Resource details clear with selection"),Widget->IsResourceDetailsVisible());}
   }
   C->MarkGuideEvent(4);
   T->TestTrue(TEXT("Campaign clock now advances"),C->GetElapsed()>0);
   T->TestTrue(TEXT("Safe save"),C->SaveCampaign());
   auto* S=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(TestSlot)); if(!S) {T->AddError(TEXT("Snapshot missing"));return true;}
   if(!VineyardChecked){T->TestEqual(TEXT("First day creates four real sites"),S->DailySites.Num(),4);int32 Enemy=0;for(const auto& Site:S->DailySites)Enemy+=Site.Owner==1;T->TestEqual(TEXT("Three daily sites initially enemy occupied"),Enemy,3);}
   T->TestTrue(TEXT("Daily resources avoid the immediate base and include a reachable outskirts site"),[&]{bool Near=false;for(const auto& Site:S->DailySites){const double D=FVector::Dist2D(Site.Position,ARTSCampaign::HomeLocation());if(D<5000)return false;Near|=D<10000;}return S->DailySites.Num()==4 && Near;}());
   TSet<FIntPoint> DailyQuadrants;
   for(const auto& Site:S->DailySites)
   {
    DailyQuadrants.Add(FIntPoint(Site.Position.X>RTSCampaignMapSpace::CenterX,Site.Position.Y>0));
    bool Open=true;auto* Scenery=C->GetDecorations();TArray<UInstancedStaticMeshComponent*> Parts;if(Scenery)Scenery->GetComponents(Parts);
    for(auto* Part:Parts)for(int32 I=0;I<Part->GetInstanceCount();++I){FTransform P;Part->GetInstanceTransform(I,P,true);Open&=FVector::Dist2D(P.GetLocation(),Site.Position)>=1599;}
    T->TestTrue(TEXT("Daily resource has a visible 16m scenery clearing"),Open);
   }
   if(!VineyardChecked)T->TestTrue(TEXT("Daily resources distribute across map quadrants"),DailyQuadrants.Num()>=2);
   if(!VineyardChecked)
   {
    Baseline.Reset(DuplicateObject<URTSCampaignSave>(S,GetTransientPackage()));
    FString LegacyFixture;
    if(FParse::Value(FCommandLine::Get(),TEXT("RTSCompactLegacyFixture="),LegacyFixture))
    {
     TArray<uint8> Bytes;const bool Read=FFileHelper::LoadFileToArray(Bytes,*LegacyFixture);
     auto* Legacy=Read?Cast<URTSCampaignSave>(RTSCampaignStorage::Decode(Bytes)):nullptr;
     UE_LOG(LogTemp,Display,TEXT("COMPACT_LEGACY_VERSION actual=%d"),Legacy?Legacy->Version:-1);
     T->TestTrue(TEXT("Read-only legacy fixture is v10"),Legacy && Legacy->Version==10);
     if(Legacy)
     {
      WriteTestSnapshot(Legacy);
      T->TestTrue(FString(TEXT("Actual v10 snapshot loads into compact terrain: "))+C->GetMessage(),C->LoadCampaign());
      int32 Living=0,BuildingCount=0;for(TActorIterator<ARTSUnit> It(W);It;++It)Living+=It->IsAlive();for(TActorIterator<ARTSBuilding> It(W);It;++It)BuildingCount+=It->IsAlive();
      T->TestEqual(TEXT("Real old save retains every living unit"),Living,Legacy->Units.Num());
      T->TestEqual(TEXT("Real old save retains every building"),BuildingCount,Legacy->BuildingKinds.Num());
      bool BuildingsAligned=true;for(int32 I=0;I<Legacy->BuildingKinds.Num();++I){bool Found=false;for(TActorIterator<ARTSBuilding> It(W);It;++It)Found|=It->IsAlive() && It->GetBuildingId()==Legacy->BuildingKinds[I] && It->GetActorLocation().Equals(RTSCampaignMapSpace::FromLegacy(Legacy->BuildingPositions[I]),.1);BuildingsAligned&=Found;}
      T->TestTrue(TEXT("Real old save building positions use one shared contraction"),BuildingsAligned);
      // No production write: restore only the synthetic test baseline to the same private slot.
      WriteTestSnapshot(S);T->TestTrue(TEXT("Restore isolated baseline after actual legacy migration"),C->LoadCampaign());
      UE_LOG(LogTemp,Display,TEXT("COMPACT_LEGACY_FIXTURE units=%d buildings=%d aligned=%d read_only=1"),Living,BuildingCount,BuildingsAligned);
     }
    }
    // Current schema day-five fixture. A current economy must not be relabelled v8:
    // old versions bootstrap their own economy, which would duplicate these buildings.
    S->Version=URTSCampaignSave::SnapshotVersion;
    S->Elapsed=1200;S->SpawnDay=4;S->RaidDay=4;S->SpawnMask=3;S->RaidMask=15;
    // Isolate the vineyard regression from the new front's combat; the baseline retains its real enemies.
    S->DailyGeneratedDay=4;
    for(auto& Site:S->DailySites){Site.Owner=0;Site.Capture=5;Site.Pulses=2;Site.NextPush=999999;}
    if(S->DailySites.IsValidIndex(3)){S->DailySites[3].Owner=255;S->DailySites[3].Capture=0;}
    S->Units.RemoveAll([](const FRTSCampaignUnitSave& U){return U.Team==1 && U.Encounter>=10000;});
    WriteTestSnapshot(S);{const bool Loaded=C->LoadCampaign();T->TestTrue(FString(TEXT("Load current day-five vineyard fixture: "))+C->GetMessage(),Loaded);if(!Loaded)return true;}Phase=W->GetTimeSeconds();Stage=60;return false;
   }
   S->Elapsed=250; S->SpawnDay=0; S->RaidDay=0; S->SpawnMask=3;S->RaidMask=0;
   for(auto& Site:S->DailySites){Site.Pulses=2;Site.NextPush=999999;}
   WriteTestSnapshot(S);T->TestTrue(TEXT("Load prior to day1 raid"),C->LoadCampaign()); Phase=W->GetTimeSeconds();Stage=7;return false;
  }
  if(Stage==7)
  {
   if(W->GetTimeSeconds()-Phase<4) return false;
   for(FName Kind:{FName("Spearman"),FName("Scout"),FName("Priest")})T->TestTrue(TEXT("Research survives reload"),C->IsUnitUnlocked(Kind));
   T->TestTrue(FString::Printf(TEXT("Daily sites survive reload count=%d"),C->GetDailySiteCount()),C->GetDailySiteCount()>=4);
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignServicesRaid.png"),true,false);
   T->TestTrue(TEXT("War report describes actual paid faction economies"),C->GetWarPlan().Contains(TEXT("付费")) && C->GetWarPlan().Contains(TEXT("经济运作")));
   if(auto* Camera=Cast<ARTSCameraPawn>(W->GetFirstPlayerController()->GetPawn()))for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->GetTeam()==0 && It->GetUnitData()->bWorker){It->GetHealth()->ApplyDamage(1,nullptr);T->TestTrue(TEXT("Worker damage triggers red postprocess and warning"),Camera->GetAttackWarningStrength()>0 && C->GetAlertText().Contains(TEXT("工人")));break;}
   T->TestFalse(TEXT("Cannot save active war state"),C->SaveCampaign());Phase=W->GetTimeSeconds();Stage=70;return false;
  }
  if(Stage==70)
  {
   if(W->GetTimeSeconds()-Phase<.2)return false;
   if(Widget.IsValid())Widget->RefreshSelectedObject();FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignWorkerAttackWarning.png"),true,false);Phase=W->GetTimeSeconds();Stage=8;return false;
  }
  if(Stage==60)
  {
   if(W->GetTimeSeconds()-Phase<3)return false;
   auto* V=C->GetVineyardNode();T->TestNotNull(TEXT("Day5 vineyard actually spawned"),V);if(!V)return true;
   for(TActorIterator<ARTSResourceNode> It(W);It;++It)if(It->GetResourceType()==ERTSResource::IronOre)T->TestTrue(TEXT("Iron deposits no longer secretly locked"),It->GetOwningTeam()!=254);
   {
    ARTSUnit* Soldier=nullptr;ARTSUnit* Enemy=nullptr;ARTSUnit* Peasant=nullptr;ARTSUnit* Healer=nullptr;
    for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive()){if(It->GetTeam()==1)Enemy=*It;else if(It->GetUnitData()->bWorker)Peasant=*It;else if(It->GetUnitData()->bHealer)Healer=*It;else Soldier=*It;}
    if(Soldier && Enemy && Peasant && Healer)
    {
     const FVector A=Soldier->GetActorLocation(),B=Enemy->GetActorLocation(),D=Peasant->GetActorLocation(),H=Healer->GetActorLocation();
     auto* Point=W->SpawnActor<ARTSResourceNode>(FVector(18000,0,90),FRotator::ZeroRotator);Point->Configure(ERTSResource::Crystal,500);Point->ConfigureResourceTier(2);Point->SetOwningTeam(255);
     const FVector Near(18350,0,82);
     T->TestTrue(TEXT("Neutral campaign point allows harvesting without capture"),Point->CanGather(0));Point->SetOwningTeam(254);T->TestTrue(TEXT("Legacy locked campaign point allows harvesting"),Point->CanGather(0));Point->SetOwningTeam(255);
     Peasant->SetActorLocation(Near);Healer->SetActorLocation(Near+FVector(0,180,0));Point->UpdateCampaignCapture(6);
     T->TestEqual(TEXT("Workers and healers cannot claim"),Point->GetOwningTeam(),uint8(255));Peasant->SetActorLocation(D);Healer->SetActorLocation(H);
     Soldier->SetActorLocation(Near);Point->UpdateCampaignCapture(4);T->TestEqual(TEXT("Four seconds insufficient to change control"),Point->GetOwningTeam(),uint8(255));
     Enemy->SetActorLocation(Near+FVector(0,180,0));Point->UpdateCampaignCapture(6);T->TestEqual(TEXT("Opposing soldiers pause capture"),Point->GetOwningTeam(),uint8(255));
     Enemy->SetActorLocation(B);Point->UpdateCampaignCapture(1);T->TestEqual(TEXT("Player finishes capture after contest clears"),Point->GetOwningTeam(),uint8(0));
     Soldier->SetActorLocation(A);Enemy->SetActorLocation(Near);Point->UpdateCampaignCapture(5);
     T->TestTrue(TEXT("Enemy recaptures but does not block harvesting"),Point->GetOwningTeam()==1 && Point->CanGather(0) && Point->CanGather(1));
     Enemy->SetActorLocation(B);Soldier->SetActorLocation(Near);Point->UpdateCampaignCapture(5);
     T->TestEqual(TEXT("Player can reclaim repeatedly"),Point->GetOwningTeam(),uint8(0));T->TestEqual(TEXT("Capture preserves resources"),Point->GetRemaining(),500);
     Soldier->SetActorLocation(A);Point->Destroy();
    }
    else T->AddError(TEXT("Missing capture regression actors"));
   }
   Stock->Add(ERTSResource::Wood,2000);Stock->Add(ERTSResource::Food,2000);
   C->SelectServiceBuilding(TEXT("ResearchInstitute"));
   T->TestFalse(TEXT("Paladin research blocked without crystal"),C->ResearchUnit(TEXT("Paladin")));
   Stock->Add(ERTSResource::Crystal,200);
   T->TestTrue(TEXT("Crystal unlocks paladin"),C->ResearchUnit(TEXT("Paladin")));
   T->TestEqual(TEXT("Research spends crystal"),Stock->GetAmount(ERTSResource::Crystal),60);
   C->SelectServiceBuilding(TEXT("Barracks"));const int32 CrystalBefore=Stock->GetAmount(ERTSResource::Crystal);
   T->TestTrue(TEXT("Paladin queues with material"),C->Train(TEXT("Paladin")));
   T->TestEqual(TEXT("Training spends crystal"),Stock->GetAmount(ERTSResource::Crystal),CrystalBefore-18);
   if(Widget.IsValid())
   {
    Widget->RefreshSelectedObject();auto* Card=Cast<UBorder>(Widget->WidgetTree->FindWidget(TEXT("ProductionQueueRow0")));T->TestTrue(TEXT("Training queue card visible"),Card && Card->GetVisibility()==ESlateVisibility::Visible);
    TArray<UWidget*> Items;Widget->WidgetTree->GetAllWidgets(Items);bool ImageFound=false,NameFound=false;
    for(auto* Item:Items){if(auto* Image=Cast<UImage>(Item))if(Image->GetBrush().GetResourceObject() && Image->GetBrush().GetResourceObject()->GetName()==TEXT("T_Unit_Paladin"))ImageFound=true;if(auto* Text=Cast<UTextBlock>(Item))NameFound|=Text->GetText().ToString().Contains(TEXT("圣骑士\n训练中"));}
    T->TestTrue(TEXT("Queue shows unit portrait and name"),ImageFound && NameFound);
   }
   T->TestTrue(TEXT("Cancel paladin order"),C->FindServiceBuilding(TEXT("Barracks"))->GetProducer()->CancelLast());
   T->TestEqual(TEXT("Cancel refunds all crystal"),Stock->GetAmount(ERTSResource::Crystal),CrystalBefore);
   if(Widget.IsValid()){Widget->RefreshSelectedObject();auto* Card=Widget->WidgetTree->FindWidget(TEXT("ProductionQueueRow0"));T->TestTrue(TEXT("Cancelled queue card disappears"),Card && Card->GetVisibility()==ESlateVisibility::Collapsed);}
   C->SelectServiceBuilding(TEXT("ResearchInstitute"));T->TestTrue(TEXT("Independent paladin permanent upgrade"),C->Upgrade(TEXT("Paladin")));
   T->TestEqual(TEXT("Paladin level two"),C->GetLevel(TEXT("Paladin")),2);
   ARTSResourceNode* Wheat=nullptr;for(TActorIterator<ARTSResourceNode> It(W);It;++It)if(It->GetResourceType()==ERTSResource::Food && !It->IsVineyard())Wheat=*It;
   T->TestTrue(TEXT("Vineyard better capacity and recovery than wheat"),Wheat && V->GetCapacity()>Wheat->GetCapacity() && V->GetRecoveryPerSecond()>Wheat->GetRecoveryPerSecond());
   T->TestTrue(TEXT("Wild vineyard can be gathered without capture"),V->CanGather(0));T->TestEqual(TEXT("Vineyard tier"),V->GetResourceTier(),3);
   auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());PC->SelectResource(V);if(Widget.IsValid())Widget->RefreshSelectedObject();
   if(PC->GetPawn())PC->GetPawn()->SetActorLocation(V->GetActorLocation());
   for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->GetTeam()==0 && !It->GetUnitData()->bWorker){It->SetActorLocation(V->GetActorLocation()+FVector(350,0,-8));It->SetCombatStance(ERTSCombatStance::Guard);break;}
   Phase=W->GetTimeSeconds();Stage=61;return false;
  }
  if(Stage==61)
  {
   if(W->GetTimeSeconds()-Phase<7)return false;
   auto* V=C->GetVineyardNode();T->TestTrue(TEXT("Vineyard capture changes owner and name"),V && V->CanGather(0) && V->GetResourceDisplayName().Contains(TEXT("种植园")));
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignResourceDetails.png"),true,false);
   V->Extract(400);Phase=W->GetTimeSeconds();Stage=62;return false;
  }
  if(Stage==62)
  {
   if(W->GetTimeSeconds()-Phase<2)return false;
   auto* V=C->GetVineyardNode();T->TestTrue(TEXT("Vineyard actually regenerates"),V && V->GetRemaining()>V->GetCapacity()-400);
   C->PrepareSave();T->TestTrue(FString(TEXT("Vineyard state can save: "))+C->GetMessage(),C->SaveCampaign());
   T->TestTrue(TEXT("Vineyard state can reload"),C->LoadCampaign());V=C->GetVineyardNode();T->TestTrue(TEXT("Vineyard owner and remaining preserved"),V && V->CanGather(0) && V->GetRemaining()<V->GetCapacity());
   T->TestEqual(TEXT("Permanent paladin level survives reload"),C->GetLevel(TEXT("Paladin")),2);
   auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController());PC->SelectBuilding(nullptr);
   for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->GetTeam()==0 && It->GetUnitData()->BalanceId==TEXT("Swordman")){PC->GetSelection()->SelectSingle(*It,false);break;}
   Phase=W->GetTimeSeconds();Stage=63;return false;
  }
  if(Stage==63)
  {
   if(W->GetTimeSeconds()-Phase<2)return false;
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignSelectedUnitDetails.png"),true,false);
   auto* S=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(TestSlot));
   if(S && S->DailySites.IsValidIndex(3))for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->GetTeam()==0 && !It->GetUnitData()->bWorker){FRTSOrder Stop;Stop.Type=ERTSOrderType::Stop;It->ReceiveOrder(Stop);It->SetCombatStance(ERTSCombatStance::Guard);It->SetActorLocation(S->DailySites[3].Position+FVector(350,0,82));break;}
   Phase=W->GetTimeSeconds();Stage=64;return false;
  }
  if(Stage==64)
  {
   if(W->GetTimeSeconds()-Phase<7)return false;
   auto* S=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(TestSlot));bool Claimed=false;
   if(S && S->DailySites.IsValidIndex(3))for(TActorIterator<ARTSResourceNode> It(W);It;++It)if(FVector::Dist2D(It->GetActorLocation(),S->DailySites[3].Position)<50)Claimed=It->GetOwningTeam()==0;
   T->TestTrue(TEXT("Standing army actually claims daily point after five seconds"),Claimed);
   C->PrepareSave();T->TestTrue(TEXT("Captured daily point saves"),C->SaveCampaign());
   auto* Next=Cast<URTSCampaignSave>(RTSCampaignStorage::Read(TestSlot));
   if(!Next)return true;
   T->TestEqual(TEXT("Captured owner persists in snapshot"),Next->DailySites[3].Owner,uint8(0));
   Next->Elapsed=300;Next->SpawnDay=1;Next->RaidDay=1;Next->SpawnMask=3;Next->RaidMask=15;Next->DailyGeneratedDay=0;
   WriteTestSnapshot(Next);T->TestTrue(TEXT("Load at next dawn"),C->LoadCampaign());Phase=W->GetTimeSeconds();Stage=65;return false;
  }
  if(Stage==65)
  {
   if(W->GetTimeSeconds()-Phase<3)return false;
   T->TestEqual(TEXT("Next dawn adds four permanent resource points"),C->GetDailySiteCount(),8);
   WriteTestSnapshot(Baseline.Get());T->TestTrue(TEXT("Return test to day1 baseline"),C->LoadCampaign());Baseline.Reset();VineyardChecked=true;Phase=W->GetTimeSeconds()-8;Stage=6;return false;
  }
  if(Stage==8)
  {
   if(W->GetTimeSeconds()-Phase<23) return false;
   if(auto* Camera=Cast<ARTSCameraPawn>(W->GetFirstPlayerController()->GetPawn())){auto* Hall=C->FindServiceBuilding(TEXT("TownHall"));Hall->GetHealth()->ApplyDamage(1,nullptr);T->TestTrue(TEXT("Building damage triggers postprocess and warning"),Camera->GetAttackWarningStrength()>0 && C->GetAlertText().Contains(TEXT("建筑")));}
   int32 Departed=0; const FVector Origin=ARTSCampaign::RaidPlan(0,1).Origin;
   for(TActorIterator<ARTSUnit> It(W);It;++It) if(It->GetTeam()==1 && It->IsAlive() && FVector::Dist2D(It->GetActorLocation(),Origin)<4000 && It->GetActorLocation().Y<-4500 && It->GetCurrentOrder().Type==ERTSOrderType::Move) ++Departed;
   C->CycleFactionColor(0);C->CycleFactionColor(1);
   T->TestEqual(TEXT("Unused teal palette"),RTSCampaignPalette::Index(0),4);T->TestEqual(TEXT("Valley takes freed purple"),RTSCampaignPalette::Index(1),0);
   for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->GetTeam()==1 && It->IsAlive() && C->GetUnitFaction(*It)==0)
   { if(auto* PC=W->GetFirstPlayerController())if(APawn* CameraPawn=PC->GetPawn())CameraPawn->SetActorLocation(It->GetActorLocation()+FVector(0,0,100));break; }
   T->TestTrue(TEXT("Legacy timer-wave collection remains unused by economic AI"),C->GetActiveRaidCount()==0);
   int32 Killed=0;
   for(TActorIterator<ARTSUnit> It(W);It;++It) if(It->GetTeam()==1 && It->IsAlive() && It->GetActorLocation().Y<-4500 && It->GetCurrentOrder().Type==ERTSOrderType::Move && Killed<2) { It->GetHealth()->ApplyDamage(10000,nullptr); ++Killed; }
   Phase=W->GetTimeSeconds();Stage=9;return false;
  }
  if(Stage==9)
  {
   if(W->GetTimeSeconds()-Phase<2) return false;
   T->TestTrue(TEXT("Economic war report remains available"),!C->GetWarPlan().IsEmpty());
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignServicesFaction.png"),true,false);
   int32 Returning=0;
   for(TActorIterator<ARTSUnit> It(W);It;++It) if(It->GetTeam()==1 && It->IsAlive() && It->GetCurrentOrder().Type==ERTSOrderType::Move && FVector::Dist2D(It->GetCurrentOrder().TargetLocation,ARTSCampaign::RaidPlan(0,1).Origin)<100) ++Returning;
   T->TestTrue(TEXT("Survivors receive actual return-to-camp orders"),Returning>0 || C->GetActiveRaidCount()==0);
   UGameplayStatics::DeleteGameInSlot(TestSlot,0);PortraitFaction=0;
   Stock->Add(ERTSResource::Wood,1000);Stock->Add(ERTSResource::Food,1000);if(auto* PC=Cast<ARTSHumanController>(W->GetFirstPlayerController())){PC->SelectBuilding(nullptr);for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0 && It->GetUnitData()->bWorker){PC->GetSelection()->SelectSingle(*It,false);break;}}T->TestTrue(TEXT("Housing construction starts from a worker"),C->BuyFacility(0));
   ARTSResourceNode* Iron=nullptr;ARTSUnit* Soldier=nullptr;for(TActorIterator<ARTSResourceNode> It(W);It;++It)if(It->GetResourceType()==ERTSResource::IronOre && FVector::Dist2D(It->GetActorLocation(),RTSCampaignMapSpace::FromLegacy(FVector(-4350,800,90)))<100)Iron=*It;
   for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->IsAlive() && It->GetTeam()==0){if(It->GetUnitData()->bWorker)Worker=*It;else if(!It->GetUnitData()->bHealer)Soldier=*It;}
   if(!Iron || !Soldier || !Worker.IsValid()){T->AddError(TEXT("Iron gather regression actors missing"));return true;}
   FRTSOrder Stop;Stop.Type=ERTSOrderType::Stop;Iron->SetOwningTeam(1);T->TestTrue(TEXT("Enemy iron harvestable without capture or research"),Iron->CanGather(0));
   Worker->ReceiveOrder(Stop);Worker->SetActorLocation(Iron->GetActorLocation()+FVector(180,0,-8));FRTSOrder Gather;Gather.Type=ERTSOrderType::Gather;Gather.TargetActor=Iron;Worker->ReceiveOrder(Gather);
   C->FocusHome();if(Widget.IsValid())Widget->RefreshSelectedObject();Phase=W->GetTimeSeconds();Stage=12;return false;
  }
  if(Stage==12)
  {
   C->SelectServiceBuilding(TEXT("TownHall"));
   if(W->GetTimeSeconds()-Phase<.5)return false;
   if(Widget.IsValid()){Widget->RefreshSelectedObject();TArray<UWidget*> Items;Widget->WidgetTree->GetAllWidgets(Items);bool Found=false;for(auto* Item:Items)if(auto* Text=Cast<UTextBlock>(Item))Found|=Text->GetText().ToString().Contains(TEXT("住宅扩建\n建设中"));T->TestTrue(TEXT("Facility queue shows name and countdown"),Found);}
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("Automation/CampaignFacilityQueue.png"),true,false);Stage=13;return false;
  }
  if(Stage==13)
  {
   if(W->GetTimeSeconds()-Phase<9)return false;
   T->TestTrue(TEXT("Worker really harvests iron"),Worker.IsValid() && ((Worker->GetGatherer()->GetCarriedType()==ERTSResource::IronOre && Worker->GetGatherer()->GetCarriedAmount()>0) || Stock->GetAmount(ERTSResource::IronOre)>0));Stage=10;return false;
  }
  if(Stage==10)
  {
   if(PortraitFaction>=4){PortraitFaction=0;Stage=90;return false;}
   ARTSUnit* Archer=nullptr;for(TActorIterator<ARTSUnit> It(W);It;++It)if(It->GetTeam()==1 && It->IsAlive() && C->GetUnitFaction(*It)==PortraitFaction && It->GetUnitData()->BalanceId==TEXT("Archer")){Archer=*It;break;}
   if(!Archer){T->AddError(TEXT("Missing faction archer"));return true;}PortraitUnit=Archer;
   Archer->SetCombatStance(ERTSCombatStance::Guard);Archer->SetActorRotation(FRotator(0,180,0));
   if(!PortraitCamera.IsValid())PortraitCamera=W->SpawnActor<ACameraActor>();
   const FVector Head=Archer->GetActorLocation()+FVector(0,0,55);
   PortraitCamera->SetActorLocation(Head+FVector(-200,0,25));PortraitCamera->SetActorRotation((Head-PortraitCamera->GetActorLocation()).Rotation());PortraitCamera->GetCameraComponent()->FieldOfView=35;
   W->GetFirstPlayerController()->SetViewTarget(PortraitCamera.Get());Archer->GetHealth()->ApplyDamage(1,nullptr);Phase=W->GetTimeSeconds();Stage=11;return false;
  }
  if(Stage==11)
  {
   if(W->GetTimeSeconds()-Phase<2)return false;
   auto* Archer=PortraitUnit.Get();const int32 Slot=Archer->GetMesh()->GetMaterialIndex(TEXT("TT_RTS_Units"));
   const FString Variant=RTSCampaignPalette::Index(PortraitFaction)==4?TEXT("GreenB"):RTSCampaignPalette::Name(PortraitFaction);
   T->TestTrue(TEXT("Archer atlas restores after hit"),Slot!=INDEX_NONE && Archer->GetMesh()->GetMaterial(Slot)->GetName()==TEXT("MI_Units_")+Variant);
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/ArcherHead_Faction%d.png"),PortraitFaction),false,false);
   ++PortraitFaction;Stage=10;return false;
  }
  if(Stage==90)
  {
   if(PortraitFaction>=4)return true;
   if(!PortraitCamera.IsValid())PortraitCamera=W->SpawnActor<ACameraActor>();
   const FVector Targets[]={FVector(3500,0,0),FVector(-23500,-19500,1400),FVector(18000,6500,750),FVector(-3600,0,0)};
   auto* Camera=PortraitCamera.Get();const FVector Target=RTSCampaignMapSpace::FromLegacy(Targets[PortraitFaction]);
   Camera->GetCameraComponent()->SetProjectionMode(PortraitFaction==0?ECameraProjectionMode::Orthographic:ECameraProjectionMode::Perspective);
   Camera->GetCameraComponent()->OrthoWidth=100000;Camera->GetCameraComponent()->FieldOfView=55;
   const FVector Offset=PortraitFaction==0?FVector(0,0,50000):FVector(-6500,0,7500);
   Camera->SetActorLocation(Target+Offset);Camera->SetActorRotation(PortraitFaction==0?FRotator(-90,0,0):(Target-Camera->GetActorLocation()).Rotation());
   if(Widget.IsValid())Widget->CallFunctionByNameWithArguments(TEXT("CloseTools"),*GLog,nullptr,true);W->GetFirstPlayerController()->SetViewTarget(Camera);Phase=W->GetTimeSeconds();Stage=91;return false;
  }
  if(Stage==91)
  {
   if(W->GetTimeSeconds()-Phase<3)return false;
   TArray<UUserWidget*> Maps;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(W,Maps,URTSCampaignMinimap::StaticClass(),false);
   if(!FParse::Param(FCommandLine::Get(),TEXT("NullRHI")))T->TestTrue(TEXT("Minimap has generated terrain and woodland texture"),Maps.Num()>0 && Cast<URTSCampaignMinimap>(Maps[0])->HasTerrainTexture());
   else T->AddInfo(TEXT("MINIMAP_GPU_TEXTURE_NOT_TESTED NullRHI=1"));
   FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Automation/TerrainReview%d.png"),PortraitFaction),true,false);
   ++PortraitFaction;Stage=90;return false;
  }
  return false;
 }
};
}
bool FRTSCampaignIntegrationTest::RunTest(const FString&)
{
 // Tests use their own slot: never require or overwrite the player's manual campaign save.
 const auto Scope=MakeShared<FRTSAutomationSaveScope>(TEXT("Guidance"));TestSlotStorage=Scope->Slot;TestSlot=*TestSlotStorage;
 FAutomationEditorCommonUtils::LoadMap(TEXT("/Game/RTS/Maps/L_BorderIncremental"));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FCampaignCheck(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());ADD_LATENT_AUTOMATION_COMMAND(FRTSAutomationSaveCleanup(Scope));return true;
}
#endif
