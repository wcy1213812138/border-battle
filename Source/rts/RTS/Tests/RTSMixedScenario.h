// Development-only, runtime mixed-unit movement fixture; never saves map/assets.
#pragma once
#include "CoreMinimal.h"

class ARTSUnit;
class UWorld;

namespace RTSMixedScenario
{
	// Counts are swordsmen, archers, peasants. Replaces friendly units in the PIE/game
	// copy of L_M0Test only; the editor world is never changed.
	bool Create(UWorld* World, const TArray<int32>& Counts, TArray<ARTSUnit*>& OutUnits, FString& OutError);
}
