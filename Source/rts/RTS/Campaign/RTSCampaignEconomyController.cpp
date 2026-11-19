#include "Campaign/RTSCampaignEconomyController.h"
#include "Campaign/RTSCampaign.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
URTSCampaignEconomyTask::URTSCampaignEconomyTask(){NodeName=TEXT("Gather, purchase, dispatch faction economy");}
EBTNodeResult::Type URTSCampaignEconomyTask::ExecuteTask(UBehaviorTreeComponent& OwnerComp,uint8*)
{
 auto* AI=Cast<ARTSCampaignEconomyController>(OwnerComp.GetAIOwner());
 if(!AI || !IsValid(AI->Campaign))return EBTNodeResult::Failed;
 AI->Campaign->StepFactionEconomy(AI->Faction);return EBTNodeResult::Succeeded;
}
void ARTSCampaignEconomyController::Start(ARTSCampaign* InCampaign,int32 InFaction)
{
 Campaign=InCampaign;Faction=InFaction;
 Tree=NewObject<UBehaviorTree>(this);
 auto* Root=NewObject<UBTComposite_Sequence>(Tree);Tree->RootNode=Root;
 auto* Economy=NewObject<URTSCampaignEconomyTask>(Tree);Economy->InitializeFromAsset(*Tree);
 auto* Wait=NewObject<UBTTask_Wait>(Tree);Wait->WaitTime=2.f;Wait->RandomDeviation=0.f;Wait->InitializeFromAsset(*Tree);
 FBTCompositeChild Work;Work.ChildTask=Economy;Root->Children.Add(Work);
 FBTCompositeChild Pause;Pause.ChildTask=Wait;Root->Children.Add(Pause);
 RunBehaviorTree(Tree); // UBehaviorTreeComponent executes this runtime tree in looping mode.
}
