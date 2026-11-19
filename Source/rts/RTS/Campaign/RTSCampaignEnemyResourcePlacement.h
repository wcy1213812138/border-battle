#pragma once
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignTerrain.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "Units/RTSUnit.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

// Bootstrap only. Saved EnemyNodes must always bypass this policy and keep their coordinates.
namespace RTSCampaignEnemyResourcePlacement
{
 inline constexpr int32 AttemptsPerType=80;
 inline constexpr int32 PathQueriesPerType=24;
 inline constexpr double NodeSpacing=600.;
 inline constexpr double BuildingApron=420.;
 inline constexpr double TerritoryRadius=3800.;
 inline uint32 Seed(const FString& CampaignId,int32 Faction)
 {
  // Explicit character hash: no actor IDs, global RNG or process-dependent name hashes.
  uint32 H=2166136261u;
  for(int32 I=0;I<CampaignId.Len();++I){H^=uint32(CampaignId[I]);H*=16777619u;}
  return (H^((uint32(Faction)+1u)*0x9e3779b9u))*16777619u;
 }
 inline FVector Candidate(FRandomStream& Random,const FVector& Base,const FVector& Patch)
 {
  // A broad asymmetric territory patch, NOT polar offsets or a ring around the Hall.
  const float X=Random.FRandRange(-2400.f,2400.f),Y=Random.FRandRange(-2100.f,2100.f);
  return Base+Patch+FVector(X,Y,0);
 }
 struct FBuildingObstacle { FVector Center,Extent;FQuat Rotation; };
 struct FContext
 {
  UWorld* World=nullptr;
  UNavigationSystemV1* Nav=nullptr;
  UStaticMeshComponent* Terrain=nullptr;
  FCollisionQueryParams Query;
  TArray<FBuildingObstacle> Buildings;
  TArray<TArray<FVector>> HaulRoutes;
  TArray<FVector> Resources;

  explicit FContext(UWorld* InWorld):World(InWorld),Query(SCENE_QUERY_STAT(CampaignEnemyResource),true)
  {
   if(!World)return;
   Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
   for(TActorIterator<AStaticMeshActor> It(World);It;++It)
   {
   auto* C=It->GetStaticMeshComponent();UStaticMesh* Mesh=C?C->GetStaticMesh():nullptr;
    if(Mesh && Mesh->GetPathName()==TEXT("/Game/RTS/Environment/Border/SM_CampaignTerrain7x5.SM_CampaignTerrain7x5"))
    {if(Terrain){Terrain=nullptr;return;}Terrain=C;}
   }
   // Moving bodies cannot decide a campaign's resource layout. Static/dynamic scenery still blocks.
   for(TActorIterator<ARTSUnit> It(World);It;++It)Query.AddIgnoredActor(*It);
  }
  bool Ready() const {return World && Nav && Terrain && Terrain->GetCollisionResponseToChannel(ECC_Pawn)==ECR_Block;}
  void AddBuilding(const ARTSBuilding* B)
  {
   FBuildingObstacle O;O.Center=B->GetActorLocation();O.Extent=ARTSBuilding::CollisionExtent(B->GetBuildingId(),B->GetFootprint())*B->GetActorScale3D().GetAbs();O.Rotation=B->GetActorQuat();Buildings.Add(O);
  }
  void AddResource(const ARTSResourceNode* N){Resources.Add(N->GetActorLocation()-FVector(0,0,90));}
  bool InsideMap(const FVector& P,double Margin) const
  {
   return !P.ContainsNaN() && P.X>=RTSCampaignMapSpace::MinX+Margin && P.X<=RTSCampaignMapSpace::MaxX-Margin && P.Y>=RTSCampaignMapSpace::MinY+Margin && P.Y<=RTSCampaignMapSpace::MaxY-Margin;
  }
  bool ClearOfSites(const FVector& P,double Margin) const
  {
   for(const auto& B:Buildings)
   {
    const FVector Local=B.Rotation.UnrotateVector(P-B.Center);
    if(FMath::Abs(Local.X)<B.Extent.X+Margin && FMath::Abs(Local.Y)<B.Extent.Y+Margin)return false;
   }
   for(const FVector& R:Resources)if(FVector::DistSquared2D(P,R)<FMath::Square(Margin+45.))return false;
   return true;
  }
  bool GroundAt(const FVector& XY,FVector& Ground) const
  {
   // Trace the authored terrain component, never a roof, wall top or analytic Z as ground.
   const FVector Guess=RTSCampaignTerrain::Ground(XY);FHitResult Hit;
   if(!InsideMap(XY,300) || !Terrain->LineTraceComponent(Hit,Guess+FVector(0,0,350),Guess-FVector(0,0,350),Query) || Hit.ImpactNormal.Z<.90f)return false;
   Ground=Hit.ImpactPoint;return true;
  }
  bool WorkPoint(const FVector& At,double ExpectedZ,FVector& Out) const
  {
   FVector Ground;if(!GroundAt(At,Ground) || FMath::Abs(Ground.Z-ExpectedZ)>45)return false;
   FNavLocation P;
   if(!Nav->ProjectPointToNavigation(Ground,P,FVector(20,20,120)) || FVector::Dist2D(P.Location,Ground)>15 || FMath::Abs(P.Location.Z-Ground.Z)>25 || !ClearOfSites(P.Location,90))return false;
   // Same conservative body envelope used by gatherer candidate occupancy checks.
   if(World->OverlapBlockingTestByChannel(P.Location+FVector(0,0,82),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(42,78),Query))return false;
   Out=P.Location;return true;
  }
  bool SegmentClear(const FVector& A,const FVector& B,double Radius) const
  {
   // Explicit sites also cover collision-disabled staged buildings/resources and stale nav tiles.
   for(const auto& O:Buildings)
   {
    const FVector From=O.Rotation.UnrotateVector(A-O.Center),To=O.Rotation.UnrotateVector(B-O.Center);
    double Enter=0,Exit=1;
    for(int32 Axis=0;Axis<2 && Enter<=Exit;++Axis)
    {
     const double Delta=To[Axis]-From[Axis],Extent=O.Extent[Axis]+Radius;
     if(FMath::Abs(Delta)<.001){if(FMath::Abs(From[Axis])<=Extent)continue;Enter=2;break;}
     const double T0=(-Extent-From[Axis])/Delta,T1=(Extent-From[Axis])/Delta;
     Enter=FMath::Max(Enter,FMath::Min(T0,T1));Exit=FMath::Min(Exit,FMath::Max(T0,T1));
    }
    if(Enter<=Exit)return false;
   }
   const FVector AB=FVector(B.X-A.X,B.Y-A.Y,0);const double Length=AB.SizeSquared();
   for(const FVector& R:Resources)
   {
    const double T=Length>.01?FMath::Clamp(FVector::DotProduct(FVector(R.X-A.X,R.Y-A.Y,0),AB)/Length,0.,1.):0.;
    if(FVector::DistSquared2D(R,A+AB*T)<FMath::Square(45.+Radius))return false;
   }
   FHitResult Hit;
   return !World->SweepSingleByChannel(Hit,A+FVector(0,0,82),B+FVector(0,0,82),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(float(Radius),78),Query);
  }
  bool Reachable(const FVector& From,const FVector& To,const FVector& Base,const FVector& Node,TArray<FVector>& OutRoute) const
  {
   // At most four short synchronous path probes per candidate, never a whole-map scan.
   const auto* Path=Nav->FindPathToLocationSynchronously(World,From,To);
   if(!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num()<2 || Path->PathPoints.Num()>48 || Path->GetPathLength()>9000 || FVector::Dist2D(Path->PathPoints.Last(),To)>30)return false;
   for(int32 I=0;I<Path->PathPoints.Num();++I)
   {
    const FVector& P=Path->PathPoints[I];
    if(!InsideMap(P,100) || FVector::Dist2D(P,Base)>6000)return false;
    if(I==0)continue;
    const FVector& A=Path->PathPoints[I-1];const FVector D=FVector(P.X-A.X,P.Y-A.Y,0);const double Len=D.SizeSquared();
    const double T=Len>.01?FMath::Clamp(FVector::DotProduct(FVector(Node.X-A.X,Node.Y-A.Y,0),D)/Len,0.,1.):0.;
    if(FVector::DistSquared2D(Node,A+D*T)<FMath::Square(100.) || !SegmentClear(A,P,42))return false;
   }
   OutRoute=Path->PathPoints;return true;
  }
  bool HallAccess(const ARTSBuilding* Hall,TArray<FVector>& Out) const
  {
   Out.Reset();const FVector Extent=ARTSBuilding::CollisionExtent(Hall->GetBuildingId(),Hall->GetFootprint());
   // Match the existing gatherer's actual Hall work envelope (ExtraReach=60).
   for(int32 I=0;I<32;++I)
   {
    const int32 Index=(I%4)*8+I/4; // Try all four sides before filling gaps.
    const double A=2*UE_PI*Index/32;const FVector Direction(FMath::Cos(A),FMath::Sin(A),0);
    const double Edge=1./FMath::Max(FMath::Abs(Direction.X)/(Extent.X+130),FMath::Abs(Direction.Y)/(Extent.Y+130));
    const FVector At=Hall->GetActorLocation()+Hall->GetActorQuat().RotateVector(Direction*Edge);FVector P;
    if(Edge>Hall->GetFootprint()*.71+140 || !WorkPoint(At,Hall->GetActorLocation().Z,P))continue;
    FCollisionQueryParams SightQuery=Query;SightQuery.AddIgnoredActor(Hall);FHitResult SightHit;
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    if(World->LineTraceSingleByObjectType(SightHit,P+FVector(0,0,80),Hall->GetActorLocation()+FVector(0,0,80),Objects,SightQuery))continue;
    bool Spaced=true;for(const FVector& Other:Out)if(FVector::Dist2D(P,Other)<180)Spaced=false;
    if(Spaced)Out.Add(P);
    if(Out.Num()==4)break;
   }
   return Out.Num()>=2;
  }
  bool Select(FRandomStream& Random,int32 Faction,ERTSResource Type,const FVector& Patch,const TArray<FVector>& HallPoints,FVector& OutFeet)
  {
   int32 PathQueries=0;
   const FVector Base=RTSCampaignFactions::Base[Faction];
   for(int32 Attempt=0;Attempt<AttemptsPerType;++Attempt)
   {
    const FVector XY=Candidate(Random,Base,Patch);
    if(FVector::Dist2D(XY,Base)>TerritoryRadius || !InsideMap(XY,350) || !ClearOfSites(XY,BuildingApron))continue;
    bool Spaced=true;for(const FVector& R:Resources)if(FVector::Dist2D(XY,R)<NodeSpacing){Spaced=false;break;}if(!Spaced)continue;
    for(int32 I=0;I<2;++I)if(FVector::Dist2D(XY,Base+FVector(-700-I*170,1100,0))<220)Spaced=false;
    if(!Spaced)continue;
    // A later node must not close an earlier node's validated hauling corridor.
    for(const auto& Route:HaulRoutes)for(int32 I=1;I<Route.Num() && Spaced;++I)
    {
     const FVector A=Route[I-1],D=FVector(Route[I].X-A.X,Route[I].Y-A.Y,0);const double Len=D.SizeSquared();
     const double T=Len>.01?FMath::Clamp(FVector::DotProduct(FVector(XY.X-A.X,XY.Y-A.Y,0),D)/Len,0.,1.):0.;
     if(FVector::DistSquared2D(XY,A+D*T)<FMath::Square(150.))Spaced=false;
    }
    if(!Spaced)continue;
    FVector Feet;if(!GroundAt(XY,Feet))continue;
    // Support the complete physical capsule, not just its centre. Permit only 12 cm relief.
    double Low=Feet.Z,High=Feet.Z;
    for(int32 I=0;I<4;++I)
    {
     const double A=I*UE_PI/2;FVector G;
     if(!GroundAt(XY+FVector(FMath::Cos(A)*45,FMath::Sin(A)*45,0),G)){Spaced=false;break;}
     Low=FMath::Min(Low,G.Z);High=FMath::Max(High,G.Z);
    }
    if(!Spaced || High-Low>12)continue;Feet.Z=High+.5;
    if(World->OverlapBlockingTestByChannel(Feet+FVector(0,0,90),FQuat::Identity,ECC_Pawn,FCollisionShape::MakeCapsule(45,89),Query))continue;
    // Also keep the 120 cm save-preflight work apron available after nav updates.
    int32 SavePositions=0;for(int32 I=0;I<12;++I){const double A=I*UE_PI/6;FVector P;if(WorkPoint(Feet+FVector(FMath::Cos(A)*120,FMath::Sin(A)*120,0),Feet.Z,P))++SavePositions;}
    if(SavePositions<2)continue;
    const double WorkRadius=45.+(Type==ERTSResource::Food?120.:60.)+40.+32.;
    const int32 Positions=FMath::Max(6,FMath::FloorToInt(2*UE_PI*WorkRadius/105));TArray<FVector> Work;
    for(int32 I=0;I<Positions;++I)
    {
     const double A=2*UE_PI*I/Positions;FVector P;
     if(!WorkPoint(Feet+FVector(FMath::Cos(A)*WorkRadius,FMath::Sin(A)*WorkRadius,0),Feet.Z,P) || !SegmentClear(P,Feet,1))continue;
     FHitResult Sight;FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
     if(World->LineTraceSingleByObjectType(Sight,P+FVector(0,0,80),Feet+FVector(0,0,170),Objects,Query))continue;
     Work.Add(P);
    }
    if(Work.Num()<4)continue;
    TArray<FVector> Access=HallPoints;
    Access.Sort([&](const FVector& A,const FVector& B){return FVector::DistSquared2D(A,Feet)<FVector::DistSquared2D(B,Feet);});
    for(const FVector& H:Access)
    {
     int32 Nearest=0;for(int32 I=1;I<Work.Num();++I)if(FVector::DistSquared2D(H,Work[I])<FVector::DistSquared2D(H,Work[Nearest]))Nearest=I;
     // Four adjacent, exclusive slots, with a nav- and collision-safe link between each.
     TArray<FVector> Slots;for(int32 I=0;I<4;++I)Slots.Add(Work[(Nearest+Work.Num()-1+I)%Work.Num()]);
     bool Connected=true;
     for(int32 I=1;I<Slots.Num();++I)
     {
      const FVector A=Slots[I-1],D=FVector(Slots[I].X-A.X,Slots[I].Y-A.Y,0);const double Len=D.SizeSquared();
      const double T=Len>.01?FMath::Clamp(FVector::DotProduct(FVector(Feet.X-A.X,Feet.Y-A.Y,0),D)/Len,0.,1.):0.;FVector Hit;
      if(FVector::DistSquared2D(Feet,A+D*T)<FMath::Square(100.) || UNavigationSystemV1::NavigationRaycast(World,A,Slots[I],Hit) || !SegmentClear(A,Slots[I],42))Connected=false;
      for(int32 J=0;J<I;++J)if(FVector::Dist2D(Slots[I],Slots[J])<100)Connected=false;
     }
     TArray<FVector> Route;
     if(!Connected)continue;
     if(PathQueries++>=PathQueriesPerType)return false;
     if(Reachable(H,Slots[0],Base,Feet,Route))
     {Route.Append(Slots);HaulRoutes.Add(MoveTemp(Route));OutFeet=Feet;return true;}
    }
   }
   return false; // Required node failure is transactional, never skipped or replaced by stock.
  }
 };
}
