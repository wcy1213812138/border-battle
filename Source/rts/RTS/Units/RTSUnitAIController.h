// MiniRTS - controller for every unit (player and computer). Uses DetourCrowd path
// following so groups flow around each other on the way; the final formation comes from
// FRTSMoveGroup slots claimed near the goal.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "RTSUnitAIController.generated.h"

UCLASS()
class ARTSUnitAIController : public AAIController
{
	GENERATED_BODY()

public:
	ARTSUnitAIController(const FObjectInitializer& ObjectInitializer);

protected:
	virtual void OnPossess(APawn* InPawn) override;
};
