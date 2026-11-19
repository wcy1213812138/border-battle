#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "RTSCampaignCookAssets.generated.h"

/** Native hard references for assets otherwise selected by runtime strings. */
UCLASS()
class URTSCampaignCookAssets : public UObject
{
 GENERATED_BODY()
public:
 URTSCampaignCookAssets();
private:
 UPROPERTY() TArray<TObjectPtr<UObject>> Assets;
};
