// Short detours around stationary capsules during the final formation approach.
#pragma once
#include "CoreMinimal.h"

class ARTSUnit;
struct FRTSMoveGroup;
namespace RTSLocalFormationPath
{
	/** Deferred admission preserves the caller's route and reservation; retry next frame. */
	bool TryBeginPlanning(const ARTSUnit* Unit);
	bool Find(const ARTSUnit* Unit, const FVector& Goal, TArray<FVector>& OutWaypoints, const FRTSMoveGroup* EntryGroup = nullptr);
}
