#pragma once
#include "CoreMinimal.h"
class ARTSUnit;
namespace RTSDuelScenario
{
	bool Create(UWorld* World, FName Left, FName Right, int32 Tier, TArray<ARTSUnit*>& OutUnits, FString& Error);
}
