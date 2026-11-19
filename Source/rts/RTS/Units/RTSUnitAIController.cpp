#include "Units/RTSUnitAIController.h"
#include "Units/RTSCrowdFollowingComponent.h"
#include "GenericTeamAgentInterface.h"

ARTSUnitAIController::ARTSUnitAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<URTSCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
}

void ARTSUnitAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Mirror the pawn's team so engine AI systems (perception, crowd) agree with it.
	if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(InPawn))
	{
		SetGenericTeamId(TeamAgent->GetGenericTeamId());
	}

	if (UCrowdFollowingComponent* Crowd = Cast<UCrowdFollowingComponent>(GetPathFollowingComponent()))
	{
		Crowd->SetCrowdSeparation(true);
		Crowd->SetCrowdSeparationWeight(1.5f);
		Crowd->SetCrowdAvoidanceQuality(ECrowdAvoidanceQuality::Good);
		// Re-plan around crowds that block the corridor.
		Crowd->SetCrowdOptimizeTopology(true);
	}
}
