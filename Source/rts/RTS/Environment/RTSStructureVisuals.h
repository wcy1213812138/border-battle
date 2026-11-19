#pragma once
#include "CoreMinimal.h"
class UStaticMeshComponent;

// Presentation only. Gameplay collision/footprint remains owned by the actor.
namespace RTSStructureVisuals
{
	bool Building(UStaticMeshComponent* Body, UStaticMeshComponent* Roof, FName Id, uint8 Team, float Footprint, float Progress, int32& AppliedTeam);
	bool Resource(UStaticMeshComponent* Visual, bool bWood);
}
