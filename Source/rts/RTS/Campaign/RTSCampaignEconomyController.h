#pragma once
#include "CoreMinimal.h"
#include "AIController.h"
#include "BehaviorTree/BTTaskNode.h"
#include "RTSCampaignEconomyController.generated.h"
class ARTSCampaign;
UCLASS()
class URTSCampaignEconomyTask : public UBTTaskNode
{
 GENERATED_BODY()
public:
 URTSCampaignEconomyTask();
 virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,uint8* NodeMemory) override;
};
// A faction manager, not a unit movement controller. No pawn possession required.
UCLASS()
class ARTSCampaignEconomyController : public AAIController
{
 GENERATED_BODY()
public:
 void Start(ARTSCampaign* Campaign,int32 InFaction);
 UPROPERTY() TObjectPtr<ARTSCampaign> Campaign;
 int32 Faction = -1;
private:
 UPROPERTY() TObjectPtr<class UBehaviorTree> Tree;
};
