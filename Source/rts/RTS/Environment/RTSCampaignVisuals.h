#pragma once
#include "CoreMinimal.h"
#include "Core/RTSTypes.h"
class UStaticMeshComponent;
namespace RTSCampaignVisuals
{
    // Presentation only. False means caller must retain its legacy visual path.
    bool Building(UStaticMeshComponent* Body, UStaticMeshComponent* Roof, FName Id, uint8 Team,
        float Footprint, float Progress, int32& AppliedTeam);
    bool Resource(UStaticMeshComponent* Visual, ERTSResource Type);
}
