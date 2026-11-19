// MiniRTS - single entry point for issuing orders. The player controller and the
// computer AI both call these functions, so both sides play by the same rules.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "RTSOrderLibrary.generated.h"

class ARTSUnit;

UCLASS()
class URTSOrderLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Moves a group to Goal, spreading them in a grid so they don't fight over one point. */
	UFUNCTION(BlueprintCallable, Category = "RTS|Orders", meta = (WorldContext = "WorldContext"))
	static void IssueMoveOrder(const UObject* WorldContext, const TArray<ARTSUnit*>& Units, const FVector& Goal, bool bQueued = false);

	/** Orders every unit to attack Target. */
	UFUNCTION(BlueprintCallable, Category = "RTS|Orders")
	static void IssueAttackOrder(const TArray<ARTSUnit*>& Units, AActor* Target, bool bQueued = false);

	UFUNCTION(BlueprintCallable, Category = "RTS|Orders")
	static void IssueStopOrder(const TArray<ARTSUnit*>& Units);

	/** Worker-only contextual order: Gather, Build or ReturnResources. */
	static void IssueWorkOrder(const TArray<ARTSUnit*>& Units, AActor* Target, bool bQueued = false);

	/** P1-03: march to Goal, engage hostiles met on the way, then resume the march. */
	static void IssueAttackMoveOrder(const TArray<ARTSUnit*>& Units, const FVector& Goal);

	/** Grid slots centered on Goal, rows perpendicular to Facing. Exposed for tests. */
	static TArray<FVector> ComputeFormationSlots(const FVector& Goal, const FVector& Facing, int32 Count, float Spacing);
};
