#pragma once
#include "CoreMinimal.h"
#include "Campaign/RTSCampaignMapSpace.h"

// Shared campaign layout; map generation may read these literal constants.
namespace RTSCampaignFactions
{
 inline constexpr int32 FactionCount = 4;
 inline constexpr int32 RegionCount = 7;
 inline constexpr int32 RegionMask = 127;
 inline constexpr int32 RaidMask = 15;
 inline const FVector WorldCenter(3500,0,0);
 inline const FVector WorldSize(RTSCampaignMapSpace::SizeX,RTSCampaignMapSpace::SizeY,0);
 inline const FVector LegacyBase[] = {FVector(30500,19500,0),FVector(30500,-19500,0),FVector(-23500,-19500,1400),FVector(-23500,19500,0)};
 inline const FVector Base[] = {RTSCampaignMapSpace::FromLegacy(LegacyBase[0]),RTSCampaignMapSpace::FromLegacy(LegacyBase[1]),RTSCampaignMapSpace::FromLegacy(LegacyBase[2]),RTSCampaignMapSpace::FromLegacy(LegacyBase[3])};
 inline constexpr int32 RegionIndex[] = {3,4,5,6};
 inline const TCHAR* const Name[] = {TEXT("暮棘盟"),TEXT("赤金沙团"),TEXT("赤焰卫"),TEXT("蔷薇庭")};
 inline const FVector LegacyRegionCenters[] = {FVector(27000,-19500,0),FVector(30500,-16000,0),FVector(27000,19500,0),LegacyBase[0],LegacyBase[1],LegacyBase[2],LegacyBase[3]};
 inline const FVector RegionCenters[] = {RTSCampaignMapSpace::FromLegacy(LegacyRegionCenters[0]),RTSCampaignMapSpace::FromLegacy(LegacyRegionCenters[1]),RTSCampaignMapSpace::FromLegacy(LegacyRegionCenters[2]),Base[0],Base[1],Base[2],Base[3]};
 inline constexpr int32 RegionFaction[] = {1,1,0,0,1,2,3};
 inline constexpr int32 Swords[] = {4,8,8,10,12,8,6};
 inline constexpr int32 Archers[] = {2,4,6,8,6,4,6};
 inline bool Contains(const FVector& P)
 {
  return RTSCampaignMapSpace::ContainsXY(P);
 }
}
