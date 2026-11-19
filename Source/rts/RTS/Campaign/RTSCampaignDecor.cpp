// MiniRTS - campaign map dressing: pines, boulders, rock shelves, shrub clumps and ground patches.
// Everything is instanced into one actor and is strictly visual: no collision, no navigation contribution,
// so however dense the scenery gets it can never block a unit, a path or a nav mesh query.

#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSCampaignMapSpace.h"
#include "Campaign/RTSCampaignRemnants.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "NavigationSystem.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"

namespace
{
	constexpr int32 ScenerySeed = 20260614;

	UInstancedStaticMeshComponent* MakeSceneryComponent(AActor* Owner, USceneComponent* Root, FName Name,
		UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Owner, Name);
		Owner->AddInstanceComponent(Component);
		Component->SetupAttachment(Root);
		if (Mesh) { Component->SetStaticMesh(Mesh); }
		if (Material) { Component->SetMaterial(0, Material); }
		// Presentation only: no collision, no navigation contribution, no overlap events.
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(false);
		Component->RegisterComponent();
		return Component;
	}

	double DistanceToSegment2D(const FVector& Point, const FVector& A, const FVector& B)
	{
		const FVector2D Start(A.X, A.Y), End(B.X, B.Y), P(Point.X, Point.Y);
		const FVector2D Span = End - Start;
		const double LengthSquared = Span.SizeSquared();
		if (LengthSquared <= KINDA_SMALL_NUMBER) { return FVector2D::Distance(P, Start); }
		const double T = FMath::Clamp(FVector2D::DotProduct(P - Start, Span) / LengthSquared, 0.0, 1.0);
		return FVector2D::Distance(P, Start + Span * T);
	}

	/** Cheap spacing test so groves look planted instead of stacked on one point. */
	struct FSceneryHash
	{
		TMap<FIntPoint, TArray<FVector2D>> Cells;

		static FIntPoint Key(const FVector& P)
		{
			return FIntPoint(FMath::FloorToInt(P.X / 1200.0), FMath::FloorToInt(P.Y / 1200.0));
		}

		bool IsFree(const FVector& P, double Radius) const
		{
			const FIntPoint Center = Key(P);
			const int32 Reach = FMath::CeilToInt(Radius / 1200.0);
			for (int32 DX = -Reach; DX <= Reach; ++DX)
			{
				for (int32 DY = -Reach; DY <= Reach; ++DY)
				{
					const TArray<FVector2D>* Found = Cells.Find(FIntPoint(Center.X + DX, Center.Y + DY));
					if (!Found) { continue; }
					for (const FVector2D& Other : *Found)
					{
						if (FVector2D::Distance(FVector2D(P.X, P.Y), Other) < Radius) { return false; }
					}
				}
			}
			return true;
		}

		void Add(const FVector& P) { Cells.FindOrAdd(Key(P)).Add(FVector2D(P.X, P.Y)); }
	};
}

int32 ARTSCampaign::GetDecorationCount() const
{
	int32 Total = 0;
	for (const TObjectPtr<UInstancedStaticMeshComponent>& Component : DecorComponents)
	{
		if (Component) { Total += Component->GetInstanceCount(); }
	}
	return Total;
}

void ARTSCampaign::ClearSceneryNear(const FVector& Position,double Radius)
{
 if(bStagingRestore)return; // A failed load must not change live map dressing.
 // Descending indices keep paired trunk/crown transforms intact when instances are removed.
 for(auto& Component:DecorComponents)if(Component)
  for(int32 I=Component->GetInstanceCount()-1;I>=0;--I)
  {
   FTransform T;if(Component->GetInstanceTransform(I,T,true) && FVector::Dist2D(T.GetLocation(),Position)<Radius)
    Component->RemoveInstance(I);
  }
 // Authored trees are also decorative. Hide only explicitly tagged/recognized trees in this world.
 for(TActorIterator<AStaticMeshActor> It(GetWorld());It;++It)
 {
  const bool Tree=It->ActorHasTag(TEXT("RTS_CompactSceneryTree"));
  if(Tree && FVector::Dist2D(It->GetActorLocation(),Position)<Radius)It->SetActorHiddenInGame(true);
 }
}

void ARTSCampaign::CreateDecorations()
{
	// Enemy bases are dressed by authored fort silhouettes plus a HUD flag pole. RegionMask says which
	// headquarters already fell, so this must run even when the scenery actor is still alive: the load
	// path calls CreateDecorations again after the world generation was rebuilt.
	RTSCampaignRemnants::SyncFactionBases(GetWorld(), RegionMask);
	if (Decor) { return; }
	UWorld* World = GetWorld();
	if (!World) { return; }
	for(TActorIterator<AStaticMeshActor> It(World);It;++It)if(It->ActorHasTag(TEXT("RTS_CompactSceneryTree")))It->SetActorHiddenInGame(false);

	UStaticMesh* TrunkMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/RTS/Environment/SM_PineTrunk.SM_PineTrunk"));
	UStaticMesh* CrownMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/RTS/Environment/SM_PineCrown.SM_PineCrown"));
	UStaticMesh* RockMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/RTS/Environment/SM_FacetedRock.SM_FacetedRock"));
	if (!TrunkMesh || !CrownMesh || !RockMesh) { return; }

	UMaterialInterface* TrunkMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RTS/Environment/Border/M_Border_Trunk.M_Border_Trunk"));
	UMaterialInterface* PineMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RTS/Environment/Border/M_Border_Pine.M_Border_Pine"));
	UMaterialInterface* RockMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RTS/Environment/Border/M_Border_Rock.M_Border_Rock"));

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	AActor* SceneryActor = World->SpawnActor<AActor>(AActor::StaticClass());
	if (!SceneryActor) { return; }
	USceneComponent* Root = NewObject<USceneComponent>(SceneryActor, TEXT("SceneryRoot"));
	SceneryActor->SetRootComponent(Root);
	Root->RegisterComponent();

	UInstancedStaticMeshComponent* Trunk = MakeSceneryComponent(SceneryActor, Root, TEXT("SceneryPineTrunk"), TrunkMesh, TrunkMaterial);
	UInstancedStaticMeshComponent* Crown = MakeSceneryComponent(SceneryActor, Root, TEXT("SceneryPineCrown"), CrownMesh, PineMaterial);
	UInstancedStaticMeshComponent* Rock = MakeSceneryComponent(SceneryActor, Root, TEXT("SceneryRock"), RockMesh, RockMaterial);
	UInstancedStaticMeshComponent* Shelf = MakeSceneryComponent(SceneryActor, Root, TEXT("SceneryRockShelf"), RockMesh, RockMaterial);
	UInstancedStaticMeshComponent* Shrub = MakeSceneryComponent(SceneryActor, Root, TEXT("SceneryShrub"), CrownMesh, PineMaterial);
	if (!Trunk || !Crown || !Rock || !Shelf || !Shrub)
	{
		SceneryActor->Destroy();
		return;
	}

	Decor = SceneryActor;
	DecorComponents = { Trunk, Crown, Rock, Shelf, Shrub };

	FRandomStream Stream(ScenerySeed);
	FSceneryHash Spacing;

	// Snap a candidate onto walkable ground: scenery must sit on the terrain, never float in the air.
	auto GroundPoint = [Nav, World](const FVector& XY, FVector& Out) -> bool
	{
		// Static terrain tracing also dresses slopes outside the walkable nav mesh.
		FHitResult Surface;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		if (World->LineTraceSingleByObjectType(Surface, XY + FVector(0,0,6000), XY - FVector(0,0,3000), Objects)
			&& Surface.ImpactNormal.Z > 0.55 && RTSCampaignMapSpace::ContainsXY(Surface.ImpactPoint))
		{
			Out = Surface.ImpactPoint; return true;
		}
		FNavLocation Hit;
		if (!Nav || !Nav->ProjectPointToNavigation(XY, Hit, FVector(200.f,200.f,4000.f))
			|| FVector::Dist2D(Hit.Location,XY)>200.0) { return false; }
		Out=Hit.Location; return true;
	};

	// Keep authored gameplay readable: no scenery on top of buildings, nodes, camps, fronts or the lanes between them.
	auto IsClear = [this](const FVector& P, double Radius) -> bool
	{
		if (!RTSCampaignFactions::Contains(P)) { return false; }
		if (FVector::Dist2D(P, HomeLocation()) < 1200.0 + Radius) { return false; }
		for (const TObjectPtr<ARTSBuilding>& Building : Buildings)
		{
			if (Building && FVector::Dist2D(P, Building->GetActorLocation()) < 700.0 + Radius) { return false; }
		}
		for (const TObjectPtr<ARTSResourceNode>& Node : Nodes)
		{
			if (Node && FVector::Dist2D(P, Node->GetActorLocation()) < 900.0 + Radius) { return false; }
		}
		for (const FRTSCampaignDailySite& Site : DailySites)
		{
			if (FVector::Dist2D(P, Site.State.Position) < 900.0 + Radius) { return false; }
		}
		for (const FRTSCampaignCamp& Camp : Camps)
		{
			if (FVector::Dist2D(P, Camp.State.Center) < 900.0 + Radius) { return false; }
		}
		const FVector Home = HomeLocation();
		for (int32 R = 0; R < RTSCampaignFactions::RegionCount; ++R)
		{
			const FVector Center = RTSCampaignFactions::RegionCenters[R];
			if (FVector::Dist2D(P, Center) < 1200.0 + Radius) { return false; }
			if (DistanceToSegment2D(P, Home, Center) < 600.0 + Radius) { return false; }
		}
		return true;
	};

	auto AddTree = [&](const FVector& Ground, double Scale)
	{
		const double Yaw = Stream.FRandRange(0.f, 360.f);
		Trunk->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Ground, FVector(Scale)));
		Crown->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Ground + FVector(0.0, 0.0, 95.0 * Scale),
			FVector(Scale * 1.25, Scale * 1.25, Scale * 1.45)));
	};

	auto AddShrub = [&](const FVector& Ground, double Scale)
	{
		const double Yaw = Stream.FRandRange(0.f, 360.f);
		Shrub->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Ground, FVector(Scale, Scale, Scale * 0.6)));
	};

	auto AddRock = [&](const FVector& Ground, double ScaleX, double ScaleY, double ScaleZ)
	{
		const double Yaw = Stream.FRandRange(0.f, 360.f);
		Rock->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Ground, FVector(ScaleX, ScaleY, ScaleZ)));
	};

	auto AddShelf = [&](const FVector& Ground, double ScaleX, double ScaleY, double ScaleZ)
	{
		const double Yaw = Stream.FRandRange(0.f, 360.f);
		// Wide, flattened boulders read as rock shelves and low ridges: extra terrain relief with no gameplay volume.
		Shelf->AddInstance(FTransform(FRotator(0.f, Yaw, 0.f), Ground + FVector(0.0, 0.0, 30.0 * ScaleZ),
			FVector(ScaleX, ScaleY, ScaleZ)));
	};


	// 1. Forest belts around the map rim: denser woodland the further the player pushes out.
	for (int32 Belt = 0; Belt < 18; ++Belt)
	{
		const double Angle = 2.0 * PI * Belt / 18.0 + Stream.FRandRange(-0.09f, 0.09f);
		const FVector BeltCenter(RTSCampaignMapSpace::CenterX + RTSCampaignMapSpace::SizeX * 0.37 * FMath::Cos(Angle),
			RTSCampaignMapSpace::CenterY + RTSCampaignMapSpace::SizeY * 0.37 * FMath::Sin(Angle), 0.0);
		FVector BeltGround;
		if (!GroundPoint(BeltCenter, BeltGround) || !IsClear(BeltGround, 400.0)) { continue; }
		const int32 Trees = Stream.RandRange(4, 8);
		for (int32 I = 0; I < Trees; ++I)
		{
			const FVector Candidate = BeltCenter + FVector(Stream.FRandRange(-2300.f, 2300.f), Stream.FRandRange(-2300.f, 2300.f), 0.0);
			FVector Ground;
			if (!GroundPoint(Candidate, Ground)) { continue; }
			if (!IsClear(Ground, 320.0)) { continue; }
			if (!Spacing.IsFree(Ground, 520.0)) { continue; }
			Spacing.Add(Ground);
			AddTree(Ground, Stream.FRandRange(0.8f, 1.5f));
		}
	}

	// 2. Lone pines and shrubs between the fronts, so travel is not an empty plain.
	for (int32 I = 0; I < 120; ++I)
	{
		const FVector Candidate(RTSCampaignMapSpace::MinX + Stream.FRandRange(0.f, (float)RTSCampaignMapSpace::SizeX),
			RTSCampaignMapSpace::MinY + Stream.FRandRange(0.f, (float)RTSCampaignMapSpace::SizeY), 0.0);
		FVector Ground;
		if (!GroundPoint(Candidate, Ground) || !IsClear(Ground, 500.0) || !Spacing.IsFree(Ground, 850.0)) { continue; }
		Spacing.Add(Ground);
		if (Stream.RandRange(0, 2) == 0) { AddShrub(Ground, Stream.FRandRange(0.5f, 1.1f)); }
		else { AddTree(Ground, Stream.FRandRange(0.85f, 1.45f)); }
	}

	// 2b. Woodland ring around the player base: keeps the opening view from looking like bare ground.
	for (int32 Ring = 0; Ring < 10; ++Ring)
	{
		const double Angle = 2.0 * PI * Ring / 10.0 + Stream.FRandRange(-0.12f, 0.12f);
		const FVector RingCenter = HomeLocation() + FVector(2600.0 * FMath::Cos(Angle), 2600.0 * FMath::Sin(Angle), 0.0);
		for (int32 I = 0; I < 4; ++I)
		{
			const FVector Candidate = RingCenter + FVector(Stream.FRandRange(-1400.f, 1400.f), Stream.FRandRange(-1400.f, 1400.f), 0.0);
			FVector Ground;
			if (!GroundPoint(Candidate, Ground) || !IsClear(Ground, 320.0) || !Spacing.IsFree(Ground, 420.0)) { continue; }
			Spacing.Add(Ground);
			AddTree(Ground, Stream.FRandRange(0.85f, 1.45f));
			if (Stream.RandRange(0, 3) == 0) { AddShrub(Ground + FVector(Stream.FRandRange(-450.f, 450.f), Stream.FRandRange(-450.f, 450.f), 0.0), Stream.FRandRange(0.5f, 1.0f)); }
		}
	}
	// 3. Boulder fields and rock shelves: the readable "terrain" layer.
	for (int32 I = 0; I < 320; ++I)
	{
		const FVector Candidate(RTSCampaignMapSpace::MinX + Stream.FRandRange(0.f, (float)RTSCampaignMapSpace::SizeX),
			RTSCampaignMapSpace::MinY + Stream.FRandRange(0.f, (float)RTSCampaignMapSpace::SizeY), 0.0);
		FVector Ground;
		if (!GroundPoint(Candidate, Ground) || !IsClear(Ground, 450.0) || !Spacing.IsFree(Ground, 700.0)) { continue; }
		Spacing.Add(Ground);
		const double Scale = Stream.FRandRange(0.7f, 2.4f);
		AddRock(Ground, Scale * 1.15, Scale, Scale * 1.05);
		if (Stream.RandRange(0, 4) == 0) { AddShrub(Ground, Stream.FRandRange(0.6f, 1.2f)); }
	}

	// 4. Ridge shelves along the outer bands: wide, low outcrops that give the map broken relief.
	for (int32 I = 0; I < 80; ++I)
	{
		const FVector Candidate(RTSCampaignMapSpace::MinX + Stream.FRandRange(0.f, (float)RTSCampaignMapSpace::SizeX),
			RTSCampaignMapSpace::MinY + Stream.FRandRange(0.f, (float)RTSCampaignMapSpace::SizeY), 0.0);
		FVector Ground;
		if (!GroundPoint(Candidate, Ground) || !IsClear(Ground, 900.0) || !Spacing.IsFree(Ground, 1500.0)) { continue; }
		Spacing.Add(Ground);
		AddShelf(Ground, Stream.FRandRange(6.f, 15.f), Stream.FRandRange(4.f, 11.f), Stream.FRandRange(1.2f, 3.4f));
	}

	// Open meadows dominate; small coherent groves occupy one sector in four.
	// Sector extents derive from the compact map grid.
	for (int32 X=0; X<12; ++X)
	{
		for (int32 Y=0; Y<17; ++Y)
		{
			const FVector Center(RTSCampaignMapSpace::MinX+(X+0.5)*(RTSCampaignMapSpace::SizeX/12.0),
				RTSCampaignMapSpace::MinY+(Y+0.5)*(RTSCampaignMapSpace::SizeY/17.0),0);
			const bool Woodland=((X+Y*3)%4)==0;
			const bool Stony=((X*3+Y)%7)<2;
			// Reserve rocky relief before the dense understory consumes its spacing.
			if (Stony)
			{
				const double Heading=Stream.FRandRange(0.f,PI*2);
				for (int32 I=-2; I<=2; ++I)
				{
					const FVector Candidate=Center+FVector(FMath::Cos(Heading)*I*650,FMath::Sin(Heading)*I*650,0);
					FVector Ground;
					if (!GroundPoint(Candidate,Ground) || !IsClear(Ground,850) || !Spacing.IsFree(Ground,550)) continue;
					Spacing.Add(Ground);
					AddShelf(Ground,Stream.FRandRange(4.f,8.f),Stream.FRandRange(3.f,6.f),Stream.FRandRange(1.f,2.2f));
				}
			}
			const FVector Grove=Center+FVector(Stream.FRandRange(-700.f,700.f),Stream.FRandRange(-700.f,700.f),0);
			for (int32 I=0; I<(Woodland?14:1); ++I)
			{
				const FVector Candidate=Grove+FVector(Stream.FRandRange(-1700.f,1700.f),Stream.FRandRange(-1700.f,1700.f),0);
				FVector Ground;
				if (!GroundPoint(Candidate,Ground) || !IsClear(Ground,400) || !Spacing.IsFree(Ground,Woodland?520:900)) continue;
				Spacing.Add(Ground);
				AddTree(Ground,Stream.FRandRange(.85f,1.5f));
			}
			// Understory and small stone groups fill gaps without hiding marching units.
			for (int32 I=0; I<(Woodland?8:3); ++I)
			{
				const FVector Candidate=Center+FVector(Stream.FRandRange(-1900.f,1900.f),Stream.FRandRange(-1900.f,1900.f),0);
				FVector Ground;
				if (!GroundPoint(Candidate,Ground) || !IsClear(Ground,300) || !Spacing.IsFree(Ground,400)) continue;
				Spacing.Add(Ground);
				if (Stony && I%3==0) AddRock(Ground,Stream.FRandRange(1.2f,3.2f),Stream.FRandRange(1.f,2.5f),Stream.FRandRange(.6f,1.8f));
				else AddShrub(Ground,Stream.FRandRange(.4f,.8f));
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("CAMPAIGN_SCENERY trees=%d rocks=%d shelves=%d shrubs=%d total=%d"),
		Trunk->GetInstanceCount(), Rock->GetInstanceCount(), Shelf->GetInstanceCount(),
		Shrub->GetInstanceCount(), GetDecorationCount());
	for(const auto& B:Buildings)if(IsValid(B))ClearSceneryNear(B->GetActorLocation(),1100);
	for(const auto& N:Nodes)if(IsValid(N))ClearSceneryNear(N->GetActorLocation(),1600);
	for(const auto& Site:DailySites)ClearSceneryNear(Site.State.Position,1600);
}

