#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSInfantryCatalog.h"
#include "Core/RTSDisplayNames.h"
#include "Core/RTSPlayerState.h"
#include "Economy/RTSResourceComponent.h"
#include "Units/RTSUnit.h"
#include "Buildings/RTSBuilding.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "HAL/IConsoleManager.h"
#include "Match/RTSMatchSubsystem.h"

URTSBalanceSubsystem* URTSBalanceSubsystem::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<URTSBalanceSubsystem>() : nullptr;
}

void URTSBalanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FString Error;
	if (!Reload(Error)) { UE_LOG(LogRTS, Error, TEXT("Balance CSV: %s"), *Error); }
}

bool URTSBalanceSubsystem::Reload(FString& Error)
{
	FString CSV;
	if (!FFileHelper::LoadFileToString(CSV, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RTS/Data/Balance/RTSBalance.csv"))))
	{ Error = TEXT("Cannot read Content/RTS/Data/Balance/RTSBalance.csv"); return false; }
	return ReloadFromCSV(CSV, Error);
}

bool URTSBalanceSubsystem::ReloadFromCSV(const FString& CSV, FString& Error)
{
	Error.Reset();
	UDataTable* Candidate = NewObject<UDataTable>(this);
	Candidate->RowStruct = FRTSBalanceRow::StaticStruct();
	const TArray<FString> Problems = Candidate->CreateTableFromCSVString(CSV);
	if (!Problems.IsEmpty()) { Error = FString::Join(Problems, TEXT("; ")); return false; }
	TArray<FName> Required = {TEXT("TownHall"), TEXT("Barracks"), TEXT("Farm"), TEXT("Tower")};
	for (const auto& Entry : RTSInfantryCatalog::Entries()) { Required.Add(Entry.Id); }
	for (const FName Id : Required)
	{
		const FRTSBalanceRow* Row = Candidate->FindRow<FRTSBalanceRow>(Id, TEXT("Validate"), false);
		const bool bUnit = RTSInfantryCatalog::Find(Id) != nullptr;
		if (!Row || Row->Kind != (bUnit ? TEXT("Unit") : TEXT("Building")) || Row->WoodCost < 0 || Row->FoodCost < 0
			|| !FMath::IsFinite(Row->MaxHealth) || Row->MaxHealth <= 0 || !FMath::IsFinite(Row->Footprint) || Row->Footprint < 80
			|| !FMath::IsFinite(Row->TrainSeconds) || (Row->TrainSeconds <= 0 && Row->Kind == TEXT("Unit"))
			|| !FMath::IsFinite(Row->BuildSeconds) || (Row->BuildSeconds <= 0 && Row->Kind == TEXT("Building"))
			|| !FMath::IsFinite(Row->GatherInterval) || Row->GatherInterval <= 0 || Row->CarryCapacity < 0
			|| (Row->bWorker && Row->CarryCapacity < 1))
		{ Error = FString::Printf(TEXT("Invalid or missing row %s"), *Id.ToString()); return false; }
	}
	Table = Candidate;
	UE_LOG(LogRTS, Display, TEXT("Balance CSV loaded: %d rows."), Table->GetRowNames().Num());
	return true;
}

const FRTSBalanceRow* URTSBalanceSubsystem::Find(FName Id) const
{
	return Table ? Table->FindRow<FRTSBalanceRow>(Id, TEXT("RTSBalance"), false) : nullptr;
}

URTSResourceComponent* URTSBalanceSubsystem::GetResources(uint8 Team) const
{
	if (Team == RTSTeam::Neutral) { return nullptr; }
	if (Team == RTSTeam::Player)
	{
		const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const ARTSPlayerState* PS = PC ? PC->GetPlayerState<ARTSPlayerState>() : nullptr;
		return PS ? PS->GetResources() : nullptr;
	}
	if (const TObjectPtr<URTSResourceComponent>* Existing = OtherStockpiles.Find(Team)) { return Existing->Get(); }
	URTSResourceComponent* Resources = NewObject<URTSResourceComponent>(const_cast<URTSBalanceSubsystem*>(this));
	OtherStockpiles.Add(Team, Resources);
	return Resources;
}

URTSUnitData* URTSBalanceSubsystem::CreateUnitData(FName Id, UObject* Outer)
{
	const FRTSBalanceRow* Row = Find(Id);
	if (!Row || Row->Kind != TEXT("Unit")) { return nullptr; }
	URTSUnitData* Source = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/RTS/Data/Units/DA_Swordman.DA_Swordman"));
	if (!Source) { return nullptr; }
	URTSUnitData* Data = DuplicateObject<URTSUnitData>(Source, Outer, MakeUniqueObjectName(Outer, URTSUnitData::StaticClass(), Id));
	Data->SetFlags(RF_Transient);
	Data->BalanceId = Id;
	Data->DisplayName = RTSDisplayNames::Entity(Id);
	Data->MaxHealth = Row->MaxHealth; Data->MoveSpeed = Row->MoveSpeed;
	Data->AttackDamage = Row->AttackDamage; Data->AttackRange = Row->AttackRange;
	Data->AttackInterval = Row->AttackInterval; Data->AttackHitDelay = Row->AttackHitDelay;
	Data->bRangedAttack = Row->bRangedAttack; Data->ProjectileSpeed = Row->ProjectileSpeed;
	Data->bWorker = Row->bWorker; Data->CarryCapacity = Row->CarryCapacity; Data->GatherInterval = Row->GatherInterval;
	Data->bAutoAcquireTargets = !Row->bWorker;
	const RTSInfantryCatalog::FEntry* Entry = RTSInfantryCatalog::Find(Id);
	if (!Entry) { return nullptr; }
	Data->Mesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Meshes/Characters_Prebuilt/Units/%s.%s"), Entry->Mesh, Entry->Mesh));
	const FString Prefix(Entry->AnimPrefix);
	const FString Folder(Entry->AnimFolder);
	auto Anim = [&](const TCHAR* Suffix)
	{
		const FString Name = FString::Printf(TEXT("Anim_%s_%s"), *Prefix, Suffix);
		return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Animations/Units/%s/%s.%s"), *Folder, *Name, *Name));
	};
	Data->IdleAnim = Anim(TEXT("01_idle")); Data->MoveAnim = Anim(TEXT("03_run"));
	Data->AttackAnim = Anim(TEXT("04_attack_A")); Data->DeathAnim = Anim(TEXT("06_death_A"));
	Data->bHealer = Id == TEXT("Priest") || Id == TEXT("HighPriest");
	if (Data->bHealer)
	{
		Data->bAutoAcquireTargets = false; Data->AttackDamage = 0; Data->bRangedAttack = false;
		Data->HealAmount = Id == TEXT("HighPriest") ? 25.f : 15.f;
		Data->HealRange = Id == TEXT("HighPriest") ? 500.f : 450.f;
		Data->HealInterval = Id == TEXT("HighPriest") ? 1.6f : 1.8f;
		Data->HealCastDelay = 0.5f;
		Data->AttackAnim = Anim(TEXT("07_cast_A"));
	}
	return Data->Mesh && Data->IdleAnim && Data->MoveAnim && Data->AttackAnim && Data->DeathAnim ? Data : nullptr;
}

ARTSUnit* URTSBalanceSubsystem::SpawnUnit(FName Id, uint8 Team, const FVector& FeetLocation)
{
	const FTransform Transform(FeetLocation + FVector(0, 0, 82));
	ARTSUnit* Unit = GetWorld()->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::DontSpawnIfColliding);
	if (!Unit) { return nullptr; }
	URTSUnitData* Data = CreateUnitData(Id, Unit);
	if (!Data) { Unit->Destroy(); return nullptr; }
	Unit->Configure(Data, Team);
	Unit->FinishSpawning(Transform);
	return Unit;
}

bool URTSBalanceSubsystem::FindSpawnPoint(const FVector& Center, float Radius, FVector& OutFeet) const
{
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!World || !Nav) { return false; }
	for (int32 Ring = 0; Ring < 4; ++Ring)
	{
		for (int32 i = 0; i < 24; ++i)
		{
			const double Angle = i * PI / 12;
			const FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Radius + Ring * 100);
			FNavLocation Projected;
			if (!Nav->ProjectPointToNavigation(Point, Projected, FVector(30, 30, 100)) || FMath::Abs(Projected.Location.Z - Center.Z) > 60) { continue; }
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSProductionSpace), false);
			if (World->OverlapBlockingTestByChannel(Projected.Location + FVector(0, 0, 82), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(42, 78), Params)) { continue; }
			// A free capsule on an isolated nav island is not a usable production exit.
			bool bHasExit = false;
			for (int32 Turn : {0, -1, 1, -2, 2})
			{
				const FVector Direction = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0).RotateAngleAxis(Turn * 45, FVector::UpVector);
				FNavLocation Exit;
				if (!Nav->ProjectPointToNavigation(Projected.Location + Direction * 180, Exit, FVector(25, 25, 60))
					|| FMath::Abs(Exit.Location.Z - Projected.Location.Z) > 40) { continue; }
				FHitResult Hit;
				if (World->SweepSingleByChannel(Hit, Projected.Location + FVector(0, 0, 82), Exit.Location + FVector(0, 0, 82),
					FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(42, 78), Params)) { continue; }
				UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, Projected.Location, Exit.Location);
				if (Path && Path->IsValid() && !Path->IsPartial()) { bHasExit = true; break; }
			}
			if (bHasExit) { OutFeet = Projected.Location; return true; }
		}
	}
	return false;
}

bool URTSBalanceSubsystem::FindProductionExit(const ARTSBuilding* Building, float UnitRadius, float UnitHalfHeight,
	FVector& OutFeet, FVector& OutExit, int32 StartCandidate, int32* FoundCandidate) const
{
	UWorld* World = GetWorld();
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!IsValid(Building) || !World || !Nav || UnitRadius <= 0 || UnitHalfHeight < UnitRadius) { return false; }
	const FVector Center = Building->GetActorLocation();
	const FVector Extent = ARTSBuilding::CollisionExtent(Building->GetBuildingId(), Building->GetFootprint()) * Building->GetActorScale3D().GetAbs();
	const FQuat Rotation = Building->GetActorQuat();
	// Both runtime spawn paths currently place the unit origin82cm above nav feet.
	const FVector Lift(0, 0, 82);
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(UnitRadius + 2, UnitHalfHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSProductionExit), false);
	FCollisionObjectQueryParams GroundObjects; GroundObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	for (int32 Index = FMath::Max(0, StartCandidate); Index < 96; ++Index)
	{
		const int32 Ring = Index / 24, Side = Index % 24;
		const FVector LocalDirection(FMath::Cos(Side * UE_PI / 12), FMath::Sin(Side * UE_PI / 12), 0);
		const FVector Direction = Rotation.RotateVector(LocalDirection);
		const double Edge = FMath::Min(FMath::Abs(LocalDirection.X) > .001 ? (Extent.X + UnitRadius + 4) / FMath::Abs(LocalDirection.X) : DBL_MAX,
			FMath::Abs(LocalDirection.Y) > .001 ? (Extent.Y + UnitRadius + 4) / FMath::Abs(LocalDirection.Y) : DBL_MAX);
		const FVector Candidate = Center + Direction * (Edge + 80 + Ring * 90);
		FNavLocation P;
		if (!Nav->ProjectPointToNavigation(Candidate, P, FVector(30, 30, 100))
			|| FVector::DistSquared2D(P.Location, Candidate) > FMath::Square(30.) || FMath::Abs(P.Location.Z - Center.Z) > 60) { continue; }
		if (World->OverlapBlockingTestByChannel(P.Location + Lift, FQuat::Identity, ECC_Pawn, Capsule, Params)) { continue; }
		FHitResult Ground;
		if (!World->LineTraceSingleByObjectType(Ground, P.Location + FVector(0,0,40), P.Location - FVector(0,0,60), GroundObjects, Params)
			|| Ground.ImpactNormal.Z < .7 || FMath::Abs(Ground.ImpactPoint.Z - P.Location.Z) > 25) { continue; }
		// Do not skip a wall or a blocking row of units to reach an outer ring.
		const FVector NearEdge = Center + Direction * (Edge + 8);
		FHitResult Approach;
		if (World->SweepSingleByChannel(Approach, FVector(NearEdge.X,NearEdge.Y,P.Location.Z) + Lift, P.Location + Lift,
			FQuat::Identity, ECC_Pawn, Capsule, Params)) { continue; }
		for (int32 Turn : {0,-1,1,-2,2})
		{
			FNavLocation Exit;
			const FVector ExitCandidate = P.Location + Direction.RotateAngleAxis(Turn * 45, FVector::UpVector) * 220;
			if (!Nav->ProjectPointToNavigation(ExitCandidate, Exit, FVector(25,25,60))
				|| FVector::DistSquared2D(Exit.Location, ExitCandidate) > FMath::Square(25.)
				|| FMath::Abs(Exit.Location.Z - P.Location.Z) > 40) { continue; }
			FHitResult Hit;
			if (World->SweepSingleByChannel(Hit, P.Location + Lift, Exit.Location + Lift, FQuat::Identity, ECC_Pawn, Capsule, Params)) { continue; }
			auto* Path = Nav->FindPathToLocationSynchronously(World, P.Location, Exit.Location);
			if (!Path || !Path->IsValid() || Path->IsPartial() || Path->PathPoints.Num() < 2) { continue; }
			OutFeet = P.Location; OutExit = Exit.Location;
			if (FoundCandidate) { *FoundCandidate = Index; }
			return true;
		}
	}
	return false;
}

bool URTSBalanceSubsystem::CanPlace(FName Id, const FVector& Ground, FString& Error) const
{
	const FRTSBalanceRow* Row = Find(Id);
	UWorld* World = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Row || Row->Kind != TEXT("Building") || !Nav) { Error = TEXT("建筑数值或导航尚未就绪。"); return false; }
	const double Half = Row->Footprint * 0.5;
	// Keep construction footprints disjoint even though the solid bodies are smaller.
	for (TActorIterator<ARTSBuilding> It(World); It; ++It)
	{
		if (!It->IsAlive()) { continue; }
		const FVector Delta = Ground - It->GetActorLocation();
		const float Clearance = Row->Footprint * 0.5f + It->GetFootprint() * 0.5f + 10;
		if (FMath::Abs(Delta.X) < Clearance && FMath::Abs(Delta.Y) < Clearance && FMath::Abs(Delta.Z) < 180)
		{ Error = TEXT("建筑之间需要保留占地间距。"); return false; }
	}
	for (int32 X = -2; X <= 2; ++X)
	{
		for (int32 Y = -2; Y <= 2; ++Y)
		{
			const FVector Point = Ground + FVector(X * Half * 0.5, Y * Half * 0.5, 0);
			FNavLocation P;
			if (!Nav->ProjectPointToNavigation(Point, P, FVector(15, 15, 60)) || FVector::Dist2D(Point, P.Location) > 10 || FMath::Abs(P.Location.Z - Ground.Z) > 25)
			{ Error = TEXT("请选择平坦且可行走的地面。"); return false; }
		}
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSBuildingSpace), false);
	if (World->OverlapBlockingTestByChannel(Ground + FVector(0, 0, 105), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeBox(FVector(Half + 10, Half + 10, 98)), Params))
	{ Error = TEXT("放置位置被单位、资源或建筑占用。"); return false; }
	return true;
}

ARTSBuilding* URTSBalanceSubsystem::PlaceBuilding(FName Id, uint8 Team, const FVector& Ground, FString& Error)
{
	if (const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this); Match && !Match->AllowsGameplay())
	{ Error = TEXT("当前对局状态不能建造建筑。"); return nullptr; }
	if (!CanPlace(Id, Ground, Error)) { return nullptr; }
	const FRTSBalanceRow* Row = Find(Id);
	const TMap<ERTSResource, int32> Cost = {{ERTSResource::Wood, Row->WoodCost}, {ERTSResource::Food, Row->FoodCost}};
	URTSResourceComponent* Resources = GetResources(Team);
	if (!Resources || !Resources->CanAfford(Cost)) { Error = TEXT("资源不足。"); return nullptr; }
	const FTransform Transform(Ground);
	ARTSBuilding* Building = GetWorld()->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Building) { Error = TEXT("无法创建建筑。"); return nullptr; }
	Building->Configure(Id, Team, false);
	if (!Resources->TrySpend(Cost)) { Building->Destroy(); Error = TEXT("资源不足。"); return nullptr; }
	Building->FinishSpawning(Transform);
	return Building;
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GReloadRTSBalance(TEXT("rts.ReloadBalance"), TEXT("Reload validated CSV. New units/buildings/orders use new values; existing stats stay unchanged."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		FString Error;
		if (URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World))
		{ if (!Balance->Reload(Error)) { UE_LOG(LogRTS, Warning, TEXT("%s"), *Error); } }
	}));
#endif
