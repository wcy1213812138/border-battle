#pragma once
#include "CoreMinimal.h"
class UWorld;
class ARTSUnit;
class ARTSBuilding;
class ARTSResourceNode;
struct FRTSEconomyScenario
{
	TArray<ARTSUnit*> Workers;
	ARTSBuilding* Hall = nullptr;
	ARTSResourceNode* Wood = nullptr;
	ARTSResourceNode* Food = nullptr;
	FVector Center = FVector::ZeroVector;
};
namespace RTSEconomyScenario
{
	bool Create(UWorld* World, FRTSEconomyScenario& Out, FString& Error);
}
