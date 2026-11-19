#pragma once
#include "CoreMinimal.h"
#include "Core/RTSTypes.h"
#include "RTSCampaignDailyTypes.generated.h"
class ARTSUnit;class ARTSResourceNode;
USTRUCT()
struct FRTSCampaignDailySiteSave
{
 GENERATED_BODY()
 UPROPERTY() int32 Id=0;
 UPROPERTY() int32 Day=0;
 UPROPERTY() int32 Tier=1;
 UPROPERTY() ERTSResource Type=ERTSResource::Wood;
 UPROPERTY() FVector Position=FVector::ZeroVector;
 UPROPERTY() uint8 Owner=1;
 UPROPERTY() int32 Remaining=900;
 UPROPERTY() float RecoveryRemainder=0;
 UPROPERTY() float Capture=0;
 UPROPERTY() int32 Pulses=0;
 UPROPERTY() double NextPush=90;
 UPROPERTY() bool GuardValid=true;
};
struct FRTSCampaignDailySite
{
 FRTSCampaignDailySiteSave State;
 TWeakObjectPtr<ARTSResourceNode> Node;
 TArray<TWeakObjectPtr<ARTSUnit>> Guards;
};
