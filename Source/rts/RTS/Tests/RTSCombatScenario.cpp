#include "Tests/RTSCombatScenario.h"
#include "Match/RTSMatchSubsystem.h"
#include "Units/RTSUnit.h"
#include "Units/RTSTrainingDummy.h"
#include "Combat/RTSProjectile.h"
#include "Data/RTSUnitData.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "HAL/IConsoleManager.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"

bool RTSCombatScenario::Create(UWorld* World, int32 Swords, int32 Archers, TArray<ARTSUnit*>& OutUnits, FString& OutError)
{
	OutUnits.Reset();
#if !UE_BUILD_SHIPPING
	if (!World || !World->IsGameWorld() || !World->GetMapName().EndsWith(TEXT("L_M0Test")))
	{
		OutError = TEXT("Run Play in L_M0Test first."); return false;
	}
	if (Swords < 1 || Archers < 0 || Swords > 12 || Archers > 12 || Swords + Archers > 12)
	{
		OutError = TEXT("Each team needs 1..12 units, with at least one swordsman."); return false;
	}
	URTSUnitData* Source = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/RTS/Data/Units/DA_Swordman.DA_Swordman"));
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Source || !Nav) { OutError = TEXT("Unit data or navigation unavailable."); return false; }
	URTSUnitData* Sword = DuplicateObject<URTSUnitData>(Source, World, MakeUniqueObjectName(World, URTSUnitData::StaticClass(), TEXT("CombatSword")));
	Sword->SetFlags(RF_Transient);
	Sword->MaxHealth = 120; Sword->AttackDamage = 15; Sword->AttackRange = 60;
	Sword->AttackInterval = 1.2f; Sword->AttackHitDelay = 0.4f;
	Sword->bAutoAcquireTargets = true; Sword->AcquisitionRadius = 1000; Sword->AutoChaseLeash = 1400;
	Sword->bRangedAttack = false;
	Sword->DisplayName = NSLOCTEXT("MiniRTS", "Swordman", "剑士");
	URTSUnitData* Archer = DuplicateObject<URTSUnitData>(Sword, World, MakeUniqueObjectName(World, URTSUnitData::StaticClass(), TEXT("CombatArcher")));
	Archer->SetFlags(RF_Transient);
	Archer->Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Toony_Tiny_RTS_Set/Meshes/Characters_Prebuilt/Units/SK_Archer.SK_Archer"));
	auto LoadAnim = [](const TCHAR* Suffix)
	{
		const FString Name = FString::Printf(TEXT("Anim_archer_%s"), Suffix);
		return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Animations/Units/Archer/%s.%s"), *Name, *Name));
	};
	Archer->IdleAnim = LoadAnim(TEXT("01_idle")); Archer->MoveAnim = LoadAnim(TEXT("03_run"));
	Archer->AttackAnim = LoadAnim(TEXT("04_attack_A")); Archer->DeathAnim = LoadAnim(TEXT("06_death_A"));
	Archer->DisplayName = NSLOCTEXT("MiniRTS", "Archer", "弓箭手");
	Archer->MaxHealth = 70; Archer->AttackDamage = 12; Archer->AttackRange = 600;
	Archer->AttackInterval = 1.4f; Archer->AttackHitDelay = 0.35f;
	Archer->bRangedAttack = true; Archer->ProjectileSpeed = 1200;
	if (!Sword->Mesh || !Sword->AttackAnim || !Sword->DeathAnim || !Archer->Mesh || !Archer->IdleAnim
		|| !Archer->MoveAnim || !Archer->AttackAnim || !Archer->DeathAnim)
	{
		OutError = TEXT("Missing local unit art."); return false;
	}
	// Validate the actual footprint and combat corridor before replacing runtime actors.
	TArray<FVector> Locations;
	FVector Center;
	FVector BattleForward = FVector::RightVector;
	const double HalfWidth = (FMath::Max(Swords, Archers) - 1) * 90.0 + 45.0;
	TArray<FVector> Candidates = {FVector(-1300, 0, 10)};
	for (int32 X = -1800; X <= 1800; X += 150)
	{
		for (int32 Y = -1800; Y <= 1800; Y += 150) { Candidates.Add(FVector(X, Y, 10)); }
	}
	for (const FVector& Candidate : Candidates)
	{
		if (!Locations.IsEmpty()) { break; }
		for (const FVector Forward : {FVector::RightVector, FVector::ForwardVector})
		{
			if (!Locations.IsEmpty()) { break; }
			const FVector Across(-Forward.Y, Forward.X, 0);
			bool bClear = true;
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(ECC_WorldStatic);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSCombatGround), false);
			const int32 WidthSteps = FMath::CeilToInt(HalfWidth * 2 / 80);
			for (int32 X = 0; X <= WidthSteps && bClear; ++X)
			{
				for (int32 Y = 0; Y <= 15 && bClear; ++Y)
				{
					const FVector Point = Candidate + Across * FMath::Lerp(-HalfWidth, HalfWidth, double(X) / WidthSteps)
						+ Forward * FMath::Lerp(-595.0, 595.0, double(Y) / 15);
					FNavLocation P;
					bClear = Nav->ProjectPointToNavigation(Point, P, FVector(20, 20, 40))
						&& FVector::Dist2D(Point, P.Location) < 10 && FMath::Abs(P.Location.Z - 10) < 25
						&& !World->OverlapAnyTestByObjectType(P.Location + FVector(0, 0, 82), FQuat::Identity, Objects,
							FCollisionShape::MakeCapsule(40, 78), Params);
				}
			}
			if (!bClear) { continue; }
			TArray<FVector> Proposed;
			for (int32 Team = 0; Team < 2 && bClear; ++Team)
			{
				const double Side = Team == 0 ? -1.0 : 1.0;
				for (int32 Type = 0; Type < 2 && bClear; ++Type)
				{
					const int32 Count = Type == 0 ? Swords : Archers;
					for (int32 i = 0; i < Count; ++i)
					{
						const FVector Feet = Candidate + Across * ((i - (Count - 1) * 0.5) * 180) + Forward * Side * (Type == 0 ? 350 : 550);
						FNavLocation P;
						if (!Nav->ProjectPointToNavigation(Feet, P, FVector(20, 20, 40))) { bClear = false; break; }
						Proposed.Add(P.Location + FVector(0, 0, Source->CapsuleHalfHeight + 2));
					}
				}
			}
			if (bClear) { Locations = MoveTemp(Proposed); Center = Candidate; BattleForward = Forward; }
		}
	}
	if (Locations.IsEmpty()) { OutError = TEXT("No flat clear combat area found. Wait for navigation or try fewer units."); return false; }
	if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
	TArray<AActor*> Old;
	for (TActorIterator<ARTSUnit> It(World); It; ++It) { Old.Add(*It); }
	for (TActorIterator<ARTSTrainingDummy> It(World); It; ++It) { Old.Add(*It); }
	for (TActorIterator<ARTSProjectile> It(World); It; ++It) { Old.Add(*It); }
	for (AActor* Actor : Old) { Actor->Destroy(); }
	int32 Index = 0;
	for (uint8 Team = 0; Team < 2; ++Team)
	{
		for (int32 Type = 0; Type < 2; ++Type)
		{
			const int32 Count = Type == 0 ? Swords : Archers;
			for (int32 i = 0; i < Count; ++i)
			{
				const FTransform Transform((Team == 0 ? BattleForward : -BattleForward).Rotation(), Locations[Index++]);
				ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (!Unit) { OutError = TEXT("Could not spawn combat unit."); return false; }
				Unit->Configure(Type == 0 ? Sword : Archer, Team);
				Unit->FinishSpawning(Transform);
				OutUnits.Add(Unit);
			}
		}
	}
	if (ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController()))
	{
		PC->GetSelection()->ClearSelection();
		for (ARTSUnit* Unit : OutUnits) { if (Unit->GetTeam() == RTSTeam::Player) { PC->GetSelection()->SelectSingle(Unit, true); } }
		if (APawn* Camera = PC->GetPawn()) { Camera->SetActorLocation(Center + FVector(0, 0, 80)); }
	}
	return true;
#else
	OutError = TEXT("Combat scenario unavailable in shipping."); return false;
#endif
}

#if WITH_EDITOR || UE_BUILD_DEBUG
static FAutoConsoleCommandWithWorldAndArgs GRTSCombatScenarioCommand(
	TEXT("rts.TestCombat"), TEXT("In L_M0Test Play: rts.TestCombat [swordsPerTeam archersPerTeam]. Default 3 2. Runtime only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		int32 Swords = 3, Archers = 2;
		if ((Args.Num() != 0 && Args.Num() != 2) || (Args.Num() == 2 && (!LexTryParseString(Swords, *Args[0]) || !LexTryParseString(Archers, *Args[1]))))
		{
			UE_LOG(LogRTS, Warning, TEXT("Usage: rts.TestCombat 3 2")); return;
		}
		TArray<ARTSUnit*> Units;
		FString Error;
		if (!RTSCombatScenario::Create(World, Swords, Archers, Units, Error)) { UE_LOG(LogRTS, Warning, TEXT("%s"), *Error); return; }
		UE_LOG(LogRTS, Display, TEXT("Combat: each team has %d swordsmen + %d archers. Green selected allies, red enemies. Auto combat active; right-click to command. Stop Play restores the map."), Swords, Archers);
	}));
#endif
