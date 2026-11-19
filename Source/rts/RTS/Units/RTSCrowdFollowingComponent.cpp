#include "Units/RTSCrowdFollowingComponent.h"
#include "AIController.h"
#include "Navigation/CrowdManager.h"
#include "Units/RTSUnit.h"

namespace
{
	bool IsRestoreStaged(const URTSCrowdFollowingComponent* Component)
	{
		const auto* AI = Cast<AAIController>(Component->GetOwner());
		return AI && AI->GetPawn() && AI->GetPawn()->ActorHasTag(TEXT("RTS_CampaignRestoreStaged"));
	}
}

void URTSCrowdFollowingComponent::Initialize()
{
	// Possession initializes the follower before the snapshot's generic Stage().
	// The tag is installed before FinishSpawning, so no temporary Detour slot is used.
	if (IsRestoreStaged(this)) { SimulationState = ECrowdSimulationState::Disabled; }
	Super::Initialize();
}

void URTSCrowdFollowingComponent::SetCrowdSimulationState(ECrowdSimulationState NewState)
{
	// Restoring orders calls LeaveMoveGroup/SetFormationSettling too. These must
	// not re-register the staged generation while the old battlefield is live.
	if (bDeadlockTravel) { ResumeSimulationState = NewState; return; }
	Super::SetCrowdSimulationState(IsRestoreStaged(this) ? ECrowdSimulationState::Disabled : NewState);
}

void URTSCrowdFollowingComponent::SetDeadlockTravel(bool bEnabled)
{
	if (bDeadlockTravel == bEnabled) { return; }
	auto* AI = Cast<AAIController>(GetOwner());
	const bool bResumeMove = AI && AI->GetMoveStatus() == EPathFollowingStatus::Moving;
	const FVector Goal = GetPathDestination();
	const float Acceptance = GetAcceptanceRadius();
	if (AI) { AI->StopMovement(); }
	if (bEnabled) { ResumeSimulationState = GetCrowdSimulationState(); }
	bDeadlockTravel = bEnabled;
	Super::SetCrowdSimulationState(bEnabled ? ECrowdSimulationState::ObstacleOnly : ResumeSimulationState);
	if (bResumeMove)
	{
		FAIMoveRequest Request(Goal);
		Request.SetAcceptanceRadius(Acceptance);
		Request.SetReachTestIncludesAgentRadius(false);
		Request.SetUsePathfinding(true);
		Request.SetAllowPartialPath(false);
		AI->MoveTo(Request);
	}
}

bool URTSCrowdFollowingComponent::ActivateRestoredAgent()
{
	if (IsRestoreStaged(this)) { return false; }
	auto* AI = Cast<AAIController>(GetOwner());
	auto* Unit = AI ? Cast<ARTSUnit>(AI->GetPawn()) : nullptr;
	const bool bResumeMove = Unit && !Unit->GetMoveGroup() && Unit->GetCurrentOrder().Type == ERTSOrderType::Move;
	const FVector MoveGoal = bResumeMove ? Unit->GetCurrentOrder().TargetLocation : FVector::ZeroVector;
	const float MoveAcceptance = GetAcceptanceRadius();
	if (AI) { AI->StopMovement(); }
	SetCrowdSimulationState(bFormationSettling ? ECrowdSimulationState::ObstacleOnly : ECrowdSimulationState::Enabled);
	const auto* Manager = UCrowdManager::GetCurrent(GetWorld());
	const bool bValid = Manager && Manager->IsAgentValid(this);
	// Group and work orders drive themselves on Tick; a standalone Move needs
	// its original request restarted after the mandatory idle registration.
	if (bValid && bResumeMove)
	{
		FAIMoveRequest Request(MoveGoal);
		Request.SetAcceptanceRadius(MoveAcceptance);
		Request.SetReachTestIncludesAgentRadius(false);
		Request.SetUsePathfinding(true);
		Request.SetAllowPartialPath(true);
		Request.SetProjectGoalLocation(true);
		Request.SetCanStrafe(false);
		return AI->MoveTo(Request).Code != EPathFollowingRequestResult::Failed;
	}
	return bValid;
}

void URTSCrowdFollowingComponent::SetFormationSettling(bool bEnabled)
{
	bFormationSettling = bEnabled;
	// The caller stops the old request first. During local settling, use normal
	// path following while remaining a crowd obstacle for the arriving neighbours.
	// Capsule collision stays enabled; no teleport or pass-through is used.
	SetCrowdSimulationState(bEnabled ? ECrowdSimulationState::ObstacleOnly : ECrowdSimulationState::Enabled);
	SetCrowdSeparation(!bEnabled);
}

void URTSCrowdFollowingComponent::UpdatePathSegment()
{
	if (bFormationSettling || bPreciseArrival)
	{
		// Avoidance can reverse velocity before a detour waypoint or final slot.
		// Require the distance test; reversal alone is not arrival.
		bCanCheckMovingTooFar = false;
		bCheckMovementAngle = false;
	}
	Super::UpdatePathSegment();
}
