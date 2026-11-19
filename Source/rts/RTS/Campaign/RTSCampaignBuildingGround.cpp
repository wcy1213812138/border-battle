#include "Campaign/RTSCampaignBuildingGround.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Buildings/RTSBuilding.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool RTSCampaignBuildingGround::FormulaSite(int32 Faction,FName Kind,FVector& OutPosition)
{
 if(Faction<0 || Faction>=RTSCampaignFactions::FactionCount)return false;
 FVector Offset;
 if(Kind==TEXT("TownHall"))Offset=FVector(0,1100,0);
 else if(Kind==TEXT("Barracks"))Offset=FVector(0,2100,0);
 else if(Kind==TEXT("ArcheryRange"))Offset=Faction==2?FVector(1000,1100,0):FVector(0,3100,0);
 else return false;
 OutPosition=RTSCampaignTerrain::Ground(RTSCampaignFactions::Base[Faction]+Offset);return true;
}
bool RTSCampaignBuildingGround::IsFormulaSite(uint8 Team,int32 Faction,FName Kind,const FVector& Position,float Facing)
{
 FVector Expected;
 return Team==1 && FMath::IsFinite(Facing) && FMath::Abs(FMath::UnwindDegrees(Facing))<.01f &&
  FormulaSite(Faction,Kind,Expected) && Position.Equals(Expected,1.f);
}
bool RTSCampaignBuildingGround::Sample(UWorld* World,FName Kind,const FVector& FormulaPosition,float Facing,FVector& OutPosition)
{
 if(!World || FormulaPosition.ContainsNaN() || !FMath::IsFinite(Facing))return false;
 auto* Balance=URTSBalanceSubsystem::Get(World);const auto* Row=Balance?Balance->Find(Kind):nullptr;
 if(!Row)return false;
 UStaticMeshComponent* Terrain=nullptr;
 for(TActorIterator<AStaticMeshActor> It(World);It;++It)
 {
  auto* Component=It->GetStaticMeshComponent();UStaticMesh* Mesh=Component?Component->GetStaticMesh():nullptr;
  if(!Mesh || Mesh->GetPathName()!=TEXT("/Game/RTS/Environment/Border/SM_CampaignTerrain7x5.SM_CampaignTerrain7x5"))continue;
  if(Terrain)return false;
  Terrain=Component;
 }
 if(!Terrain || Terrain->GetCollisionResponseToChannel(ECC_Pawn)!=ECR_Block)
 {UE_LOG(LogTemp,Warning,TEXT("CAMPAIGN_GROUND_SAMPLE terrain=%s kind=%s"),*GetNameSafe(Terrain),*Kind.ToString());return false;}
 const FVector Extent=ARTSBuilding::CollisionExtent(Kind,Row->Footprint);
 const FQuat Rotation=FRotator(0,Facing,0).Quaternion();
 double Minimum=TNumericLimits<double>::Max(),Maximum=TNumericLimits<double>::Lowest();
 FCollisionQueryParams Query(SCENE_QUERY_STAT(CampaignBuildingGround),true);
 // Sample the complete real footprint, not just the centre of the formula plateau.
 for(int32 X=-2;X<=2;++X)for(int32 Y=-2;Y<=2;++Y)
 {
  const FVector Point=FormulaPosition+Rotation.RotateVector(FVector(Extent.X*X*.5,Extent.Y*Y*.5,0));
  FHitResult Hit;
  const bool bHit=Terrain->LineTraceComponent(Hit,Point+FVector(0,0,100),Point-FVector(0,0,100),Query);
  if(!bHit || Hit.ImpactNormal.Z<.95 || Hit.ImpactPoint.Z-FormulaPosition.Z>25.)
  {UE_LOG(LogTemp,Warning,TEXT("CAMPAIGN_GROUND_SAMPLE kind=%s point=%s hit=%d blocking=%d surface=%s normal=%s"),*Kind.ToString(),*Point.ToString(),bHit,Hit.bBlockingHit,*Hit.ImpactPoint.ToString(),*Hit.ImpactNormal.ToString());return false;}
  Minimum=FMath::Min(Minimum,Hit.ImpactPoint.Z);Maximum=FMath::Max(Maximum,Hit.ImpactPoint.Z);
 }
 OutPosition=FormulaPosition;
 if(Maximum<=FormulaPosition.Z+1.)return true; // Already clear: never lower a building onto a slope.
 if(Maximum-Minimum>20.)return false;
 OutPosition.Z=Maximum+.5;return true;
}
