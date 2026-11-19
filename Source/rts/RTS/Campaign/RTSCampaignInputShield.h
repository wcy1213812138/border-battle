#pragma once
#include "CoreMinimal.h"
#include "Components/Border.h"
#include "RTSCampaignInputShield.generated.h"

/** Modal dialogue shield consumes both left and right clicks before gameplay. */
UCLASS()
class URTSCampaignInputShield : public UBorder
{
 GENERATED_BODY()
public:
 virtual void SynchronizeProperties() override;
 UFUNCTION() FEventReply Consume(FGeometry Geometry,const FPointerEvent& Event);
};
