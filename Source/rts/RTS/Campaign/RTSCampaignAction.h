#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RTSCampaignAction.generated.h"
class ARTSCampaign;
UCLASS()
class URTSCampaignAction : public UObject
{
 GENERATED_BODY()
public:
 TWeakObjectPtr<ARTSCampaign> Campaign;
 FName Kind;
 int32 Mode=0;
 UFUNCTION() void Execute();
};
