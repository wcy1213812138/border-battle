#include "Tests/RTSMixedScenario.h"
#include "Match/RTSMatchSubsystem.h"
#include "Core/RTSDisplayNames.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "UObject/UnrealType.h"
#include "HAL/IConsoleManager.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"

bool RTSMixedScenario::Create(UWorld* World, const TArray<int32>& Counts, TArray<ARTSUnit*>& OutUnits, FString& OutError)
{
	OutUnits.Reset();
#if !UE_BUILD_SHIPPING
	if (!World || !World->IsGameWorld() || !World->GetMapName().EndsWith(TEXT("L_M0Test")))
	{
		OutError = TEXT("Run Play in L_M0Test first.");
		return false;
	}
	int32 Total = 0;
	for (int32 Count : Counts)
	{
		if (Count < 0 || Count > 48) { OutError = TEXT("Counts must be between 0 and 48."); return false; }
		Total += Count;
	}
	if (Counts.Num() != 3 || Total < 1 || Total > 48)
	{
		OutError = TEXT("Specify three counts, total 1..48: swordsmen archers peasants.");
		return false;
	}
	URTSUnitData* Source = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/RTS/Data/Units/DA_Swordman.DA_Swordman"));
	FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(ARTSUnit::StaticClass(), TEXT("UnitData"));
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Source || !Property || !Nav) { OutError = TEXT("Unit data or navigation unavailable."); return false; }

	const TCHAR* MeshNames[] = {TEXT("Swordman"), TEXT("Archer"), TEXT("Peasant")};
	const TCHAR* AnimFolders[] = {TEXT("Infantry"), TEXT("Archer"), TEXT("Infantry")};
	const TCHAR* AnimPrefixes[] = {TEXT("infantry"), TEXT("archer"), TEXT("infantry")};
	TArray<URTSUnitData*> Types;
	for (int32 i = 0; i < 3; ++i)
	{
		URTSUnitData* Data = DuplicateObject<URTSUnitData>(Source, World, MakeUniqueObjectName(World, URTSUnitData::StaticClass(), FName(MeshNames[i])));
		Data->SetFlags(RF_Transient);
		const FString MeshPath = FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Meshes/Characters_Prebuilt/Units/SK_%s.SK_%s"), MeshNames[i], MeshNames[i]);
		Data->Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
		auto LoadAnim = [&](const TCHAR* Suffix)
		{
			const FString Name = FString::Printf(TEXT("Anim_%s_%s"), AnimPrefixes[i], Suffix);
			return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("/Game/Toony_Tiny_RTS_Set/Animations/Units/%s/%s.%s"), AnimFolders[i], *Name, *Name));
		};
		Data->IdleAnim = LoadAnim(TEXT("01_idle"));
		Data->MoveAnim = LoadAnim(TEXT("03_run"));
		Data->AttackAnim = LoadAnim(TEXT("04_attack_A"));
		Data->DeathAnim = LoadAnim(TEXT("06_death_A"));
		Data->DisplayName = RTSDisplayNames::Entity(FName(MeshNames[i]));
		if (!Data->Mesh || !Data->IdleAnim || !Data->MoveAnim || !Data->AttackAnim || !Data->DeathAnim)
		{
			OutError = FString::Printf(TEXT("Missing local art for %s."), MeshNames[i]);
			return false;
		}
		Types.Add(Data);
	}

	// Validate spawn ground before replacing any runtime actors.
	TArray<FVector> Locations;
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Total)));
	for (int32 i = 0; i < Total; ++i)
	{
		FNavLocation Projected;
		const FVector Feet(-1000.0 + (i % Columns - (Columns - 1) * 0.5) * 150.0, -1000.0 + (i / Columns) * 150.0, 0.0);
		if (!Nav->ProjectPointToNavigation(Feet, Projected, FVector(60, 60, 150)))
		{
			OutError = TEXT("Test spawn area has no usable NavMesh yet. Wait and retry.");
			return false;
		}
		Locations.Add(Projected.Location + FVector(0, 0, Source->CapsuleHalfHeight + 2.0));
	}
	if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
	TArray<ARTSUnit*> OldUnits;
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		if (It->GetTeam() == RTSTeam::Player) { OldUnits.Add(*It); }
	}
	for (ARTSUnit* Unit : OldUnits) { Unit->Destroy(); }

	TArray<int32> Remaining = Counts;
	for (int32 i = 0, Type = 0; i < Total; ++i)
	{
		while (Remaining[Type] == 0) { Type = (Type + 1) % 3; }
		ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), FTransform(Locations[i]), nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Unit) { OutError = TEXT("Could not spawn a test unit."); return false; }
		Property->SetObjectPropertyValue_InContainer(Unit, Types[Type]);
		Unit->FinishSpawning(FTransform(Locations[i]));
		OutUnits.Add(Unit);
		--Remaining[Type];
		Type = (Type + 1) % 3;
	}
	return true;
#else
	OutError = TEXT("Development scenario unavailable in shipping.");
	return false;
#endif
}

#if WITH_EDITOR || UE_BUILD_DEBUG
static FAutoConsoleCommandWithWorldAndArgs GRTSMixedScenarioCommand(
	TEXT("rts.TestMixed"), TEXT("In L_M0Test Play: rts.TestMixed [swordsmen archers peasants]. Default: 8 5 3. Runtime only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		TArray<int32> Counts = {8, 5, 3};
		if (Args.Num() != 0 && Args.Num() != 3) { UE_LOG(LogRTS, Warning, TEXT("Usage: rts.TestMixed 8 5 3")); return; }
		for (int32 i = 0; i < Args.Num(); ++i)
		{
			if (!LexTryParseString(Counts[i], *Args[i])) { UE_LOG(LogRTS, Warning, TEXT("Counts must be integers.")); return; }
		}
		TArray<ARTSUnit*> Units;
		FString Error;
		if (!RTSMixedScenario::Create(World, Counts, Units, Error)) { UE_LOG(LogRTS, Warning, TEXT("%s"), *Error); return; }
		if (ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController()))
		{
			PC->GetSelection()->ClearSelection();
			for (ARTSUnit* Unit : Units) { PC->GetSelection()->SelectSingle(Unit, true); }
			if (APawn* Camera = PC->GetPawn()) { Camera->SetActorLocation(FVector(-1000, -800, 90)); }
		}
		UE_LOG(LogRTS, Display, TEXT("Mixed movement scenario: %d swordsmen + %d archers + %d peasants. All selected; right-click clear ground to move. Stop Play restores the saved map."), Counts[0], Counts[1], Counts[2]);
	}));
#endif
