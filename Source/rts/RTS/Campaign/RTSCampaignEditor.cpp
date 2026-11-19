#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignMapSpace.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "EngineUtils.h"
#include "UObject/UObjectGlobals.h"
#if WITH_EDITOR
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "PhysicsEngine/BodySetup.h"
#endif
bool ARTSCampaign::BuildNavigation(ANavMeshBoundsVolume* Volume)
{
#if WITH_EDITOR
 if(!Volume || !Volume->GetWorld() || Volume->GetWorld()->IsGameWorld()) return false;
 UCubeBuilder* Builder=NewObject<UCubeBuilder>(); Builder->X=static_cast<float>(RTSCampaignMapSpace::SizeX); Builder->Y=static_cast<float>(RTSCampaignMapSpace::SizeY); Builder->Z=2400;
 if(!Builder->Build(Volume->GetWorld(),Volume)) return false;
 // CubeBuilder creates editor polygons only. Cooked bounds use the aggregate geometry.
 UBrushComponent* Brush=Volume->GetBrushComponent();if(!Brush)return false;
 // Script-built UModels need not contain BSP nodes; construct the known box explicitly.
 if(!Brush->BrushBodySetup)Brush->BrushBodySetup=NewObject<UBodySetup>(Brush);
 UBodySetup* Body=Brush->BrushBodySetup;Body->Modify();Body->InvalidatePhysicsData();
 Body->AggGeom.EmptyElements();Body->AggGeom.BoxElems.Add(FKBoxElem(static_cast<float>(RTSCampaignMapSpace::SizeX),static_cast<float>(RTSCampaignMapSpace::SizeY),2400));
 Body->CollisionTraceFlag=CTF_UseSimpleAsComplex;Body->CreatePhysicsMeshes();
 Brush->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 const FVector CookedSize=Brush->BrushBodySetup->AggGeom.CalcAABB(Brush->GetComponentTransform()).GetSize();
 if(!CookedSize.Equals(FVector(RTSCampaignMapSpace::SizeX,RTSCampaignMapSpace::SizeY,2400),1))return false;
 Brush->UpdateBounds();Volume->MarkPackageDirty();
 UE_LOG(LogTemp,Display,TEXT("CAMPAIGN_COOK_BOUNDS size=%s shapes=%d"),*CookedSize.ToString(),Brush->BrushBodySetup->AggGeom.GetElementCount());
 UWorld* World=Volume->GetWorld();
 UNavigationSystemV1* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
 if(!Nav) return false;
 FlushAsyncLoading();
 Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock);
 Nav->OnNavigationBoundsUpdated(Volume);
 bool HasData=false; for(TActorIterator<ARecastNavMesh> It(World);It;++It) HasData=true;
 if(!HasData) World->SpawnActor<ARecastNavMesh>();
 Nav->Build();
 for(TActorIterator<ARecastNavMesh> It(World);It;++It) It->EnsureBuildCompletion();
 // Fail before the Python caller can save if any added side lacks ground nav.
 const FVector Probes[]={FVector(-3600,0,0),FVector(2500,2200,900),FVector(7900,4800,900),FVector(0,2500,450),
  FVector(-27500,-23000,0),FVector(-27500,23000,0),FVector(34500,-23000,0),FVector(34500,23000,0),
  FVector(30500,19500,0),FVector(30500,-19500,0),FVector(-23500,-19500,0),FVector(-23500,19500,0),
  FVector(-27500,0,0),FVector(34500,0,0),FVector(3500,-43800,0),FVector(3500,43800,0),FVector(-27500,-43800,0),FVector(34500,43800,0)};
 for(const FVector& Probe:Probes)
 {
  FNavLocation Point;
  const FVector Compact=RTSCampaignMapSpace::FromLegacy(Probe);
  const FVector Expected=Probe.Z==0?RTSCampaignTerrain::Ground(Compact):Compact;
  if(!Nav->ProjectPointToNavigation(Expected,Point,FVector(200,200,200)) || FVector::Dist2D(Expected,Point.Location)>200 || FMath::Abs(Expected.Z-Point.Location.Z)>100) return false;
 }
 return true;
#else
 return false;
#endif
}
bool ARTSCampaign::PrepareNavigationForInspection(UObject* Context)
{
#if WITH_EDITOR
 UWorld* World=Context?Context->GetWorld():nullptr;
 if(!World || World->IsGameWorld())return false;
 auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);if(!Nav)return false;
 FlushAsyncLoading();Nav->RemoveNavigationBuildLock(ENavigationBuildLock::AsyncLoadLock);
 for(TActorIterator<ANavMeshBoundsVolume> It(World);It;++It)Nav->OnNavigationBoundsUpdated(*It);
 Nav->Build();bool HasData=false;
 for(TActorIterator<ARecastNavMesh> It(World);It;++It){HasData=true;It->EnsureBuildCompletion();}
 return HasData;
#else
 return false;
#endif
}
