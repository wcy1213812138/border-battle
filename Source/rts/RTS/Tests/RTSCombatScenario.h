#pragma once
#include "CoreMinimal.h"
class UWorld;
class ARTSUnit;
namespace RTSCombatScenario
{
	bool Create(UWorld* World, int32 SwordsPerTeam, int32 ArchersPerTeam, TArray<ARTSUnit*>& OutUnits, FString& OutError);
}
