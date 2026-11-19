// MiniRTS - campaign world dressing that mimics a faction base must not outlive that base.
// Two pieces of dressing are authored outside the gameplay actors:
//  * the fake fort silhouettes baked into L_BorderIncremental by Tools/ExpandBorderCampaign.py
//    ("Campaign expansion / HQ <Color> <Kind> Body|Team", CampaignArt SM_<Kind>_Body/_Team meshes),
//  * the HUD flag pole that ARTSCampaign::CreateRegion spawns for every faction base (ARTSWorldLabel).
// ARTSBuilding::HandleDied only destroys the real building, so both stay behind after a fight and are
// re-created on load. This helper owns the whole lifecycle: hide on death, hide on load, restore when a
// save from before the victory is loaded again. Nothing here touches gameplay actors, terrain or scenery.
#pragma once

#include "CoreMinimal.h"

class AStaticMeshActor;
class UWorld;

namespace RTSCampaignRemnants
{
	/** Every authored silhouette and the base flag sits inside this radius (cm) of RTSCampaignFactions::Base. */
	inline constexpr double BaseRadius = 2400.0;
	/** Actor-label prefix written by Tools/ExpandBorderCampaign.py for the faction bases. */
	inline const TCHAR* const DressingLabelPrefix = TEXT("Campaign expansion / HQ ");
	/** Status text an old world generation can leave on a defeated base flag. */
	inline const TCHAR* const DefeatedFlagText = TEXT("已击败");

	/** True only for the authored faction-fort silhouettes; scenery, rocks and trees never match. */
	bool IsFactionBaseDressing(AStaticMeshActor& Actor);
	/** True when that silhouette depicts Kind (TownHall/Barracks/ArcheryRange/Tower). */
	bool IsFactionBaseDressingOfKind(AStaticMeshActor& Actor, FName Kind);
	/** True for the region flag that carries this faction's name; daily-camp markers never match. */
	bool IsFactionRegionFlag(const FString& Label, int32 Faction);
	/** True for the stale "已击败" status flag of a defeated base. */
	bool IsDefeatedStatusFlag(const FString& Label);
	/** Proximity test that keeps one base's cleanup away from its neighbours and from open scenery. */
	bool WithinBaseRadius(const FVector& BaseCentre, const FVector& Position);

	/**
	 * One faction building died: hide the silhouette that mirrored it. A lost TownHall retires the whole
	 * base, including its flag. Reversible on purpose so loading an older save brings the base back.
	 */
	int32 ClearDefeatedFactionBuilding(UWorld* World, int32 Faction, FName BuildingId);
	/** Post-start/post-load pass: defeat state in RegionMask decides whether each base is hidden or shown. */
	int32 SyncFactionBases(UWorld* World, int32 RegionMask);
}
