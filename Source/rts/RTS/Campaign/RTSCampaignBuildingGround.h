#pragma once
#include "CoreMinimal.h"

class UWorld;

/** Fixed economic sites only. Terrain sampling never treats a roof or an unrelated obstacle as ground. */
namespace RTSCampaignBuildingGround
{
 bool FormulaSite(int32 Faction,FName Kind,FVector& OutPosition);
 bool IsFormulaSite(uint8 Team,int32 Faction,FName Kind,const FVector& Position,float Facing);
 // Preserve XY/yaw, never lower. Upward repair <=25.5 cm on relief <=20 cm; clear sites stay put.
 bool Sample(UWorld* World,FName Kind,const FVector& FormulaPosition,float Facing,FVector& OutPosition);
}
