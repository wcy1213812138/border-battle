#pragma once
#include "CoreMinimal.h"

/** Progress toward one path destination, not distance travelled (circling is not progress). */
struct FRTSMovementProgress
{
	FVector Goal = FVector::ZeroVector;
	double BestRemaining = 0;
	double LastProgressAt = -1;

	void Reset() { LastProgressAt = -1; }
	/** Remaining distances from different routes are not comparable, even for the same goal. */
	void BeginRoute(const FVector& InGoal, double Remaining, double Now)
	{
		Goal = InGoal; BestRemaining = Remaining; LastProgressAt = Now;
	}

	bool Observe(const FVector& InGoal, double Remaining, double Now, double Delay = 1.6)
	{
		if (LastProgressAt < 0 || !Goal.Equals(InGoal, 25))
		{
			Goal = InGoal; BestRemaining = Remaining; LastProgressAt = Now;
		}
		else if (Remaining < BestRemaining - 10)
		{
			BestRemaining = Remaining; LastProgressAt = Now;
		}
		return Now - LastProgressAt >= Delay;
	}
};
