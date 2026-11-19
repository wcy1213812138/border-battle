// Keep DetourCrowd travel, but require actual proximity when settling a formation slot.
#pragma once

#include "CoreMinimal.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "RTSCrowdFollowingComponent.generated.h"

UCLASS()
class URTSCrowdFollowingComponent : public UCrowdFollowingComponent
{
	GENERATED_BODY()

public:
	virtual void Initialize() override;
	virtual void SetCrowdSimulationState(ECrowdSimulationState NewState) override;
	// Called only after the old generation has been retired at restore commit.
	bool ActivateRestoredAgent();
	void SetFormationSettling(bool bEnabled);
	bool IsFormationSettling() const { return bFormationSettling; }
	// Also require distance-based arrival while Crowd routes an unclaimed unit.
	void SetPreciseArrival(bool bEnabled) { bPreciseArrival = bEnabled; }
	// Temporarily use normal nav-path following, retaining the latest requested Crowd mode.
	void SetDeadlockTravel(bool bEnabled);
protected:
	virtual void UpdatePathSegment() override;

private:
	bool bFormationSettling = false;
	bool bPreciseArrival = false;
	bool bDeadlockTravel = false;
	ECrowdSimulationState ResumeSimulationState = ECrowdSimulationState::Enabled;
};
