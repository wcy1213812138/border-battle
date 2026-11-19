#include "Campaign/RTSCampaignAction.h"
#include "Campaign/RTSCampaign.h"
void URTSCampaignAction::Execute()
{
 if(auto* C=Campaign.Get()) { if(Mode==0)C->ResearchUnit(Kind);else if(Mode==1)C->Upgrade(Kind);else C->Train(Kind); }
}
