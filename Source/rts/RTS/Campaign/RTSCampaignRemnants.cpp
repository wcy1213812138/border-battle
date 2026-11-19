#include "Campaign/RTSCampaignRemnants.h"
#include "Campaign/RTSCampaignFactions.h"
#include "Campaign/RTSWorldLabel.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	/** Leaf asset name of the actor's silhouette mesh, e.g. "SM_TownHall_Body"; empty when it has none. */
	FString DressingMeshName(AStaticMeshActor& Actor)
	{
		const UStaticMeshComponent* Component = Actor.GetStaticMeshComponent();
		const UStaticMesh* Mesh = Component ? Component->GetStaticMesh() : nullptr;
		return Mesh ? Mesh->GetName() : FString();
	}

	/**
	 * Fort kind behind a silhouette mesh name, e.g. "TownHall"; empty for anything that is not one of the
	 * four authored fort parts. Scenery (pines, rocks, boulders) can never produce a kind.
	 */
	FString FortKindName(const FString& MeshName)
	{
		if (!MeshName.StartsWith(TEXT("SM_"), ESearchCase::CaseSensitive)) { return FString(); }
		FString Rest = MeshName.RightChop(3);
		if (Rest.EndsWith(TEXT("_Body"), ESearchCase::CaseSensitive)) { Rest = Rest.LeftChop(5); }
		else if (Rest.EndsWith(TEXT("_Team"), ESearchCase::CaseSensitive)) { Rest = Rest.LeftChop(5); }
		else { return FString(); }
		static const TCHAR* const Kinds[] = { TEXT("TownHall"), TEXT("Barracks"), TEXT("ArcheryRange"),
			TEXT("SupportBarracks"), TEXT("ResearchInstitute"), TEXT("Farm"), TEXT("Tower") };
		for (const TCHAR* Kind : Kinds) { if (Rest == Kind) { return Rest; } }
		return FString();
	}

	bool HideDressing(AStaticMeshActor& Actor)
	{
		if (Actor.IsHidden()) { return false; }
		Actor.SetActorHiddenInGame(true);
		// Remnant watchtowers keep collision off so decorative ruins stay inert.
		Actor.SetActorEnableCollision(false);
		return true;
	}

	bool ShowDressing(AStaticMeshActor& Actor)
	{
		if (!Actor.IsHidden()) { return false; }
		Actor.SetActorHiddenInGame(false);
		Actor.SetActorEnableCollision(true);
		return true;
	}

	/** Removes the base's own HUD flag pole. Hiding is useless here: the HUD draws every live ARTSWorldLabel. */
	int32 RemoveFlags(UWorld* World, const FVector& Centre, int32 Faction, bool bDefeatedOnly)
	{
		int32 Removed = 0;
		for (TActorIterator<ARTSWorldLabel> It(World); It; ++It)
		{
			const FString Label = It->GetLabel();
			const bool bMatch = bDefeatedOnly ? RTSCampaignRemnants::IsDefeatedStatusFlag(Label)
				: RTSCampaignRemnants::IsFactionRegionFlag(Label, Faction);
			if (!bMatch || !RTSCampaignRemnants::WithinBaseRadius(Centre, It->GetActorLocation())) { continue; }
			It->Destroy();
			UE_LOG(LogTemp, Display, TEXT("CAMPAIGN_REMNANTS_FLAG faction=%d label=%s"), Faction, *Label);
			++Removed;
		}
		return Removed;
	}
}

bool RTSCampaignRemnants::IsFactionBaseDressing(AStaticMeshActor& Actor)
{
	const FString MeshKind = FortKindName(DressingMeshName(Actor));
	if (!MeshKind.IsEmpty()) { return true; }
	// Editor-only fallback for a map whose silhouette assets were replaced: the generated label identifies them.
	return Actor.GetActorNameOrLabel().StartsWith(DressingLabelPrefix);
}

bool RTSCampaignRemnants::IsFactionBaseDressingOfKind(AStaticMeshActor& Actor, FName Kind)
{
	if (Kind.IsNone() || !IsFactionBaseDressing(Actor)) { return false; }
	const FString MeshKind = FortKindName(DressingMeshName(Actor));
	if (!MeshKind.IsEmpty()) { return MeshKind == Kind.ToString(); }
	return Actor.GetActorNameOrLabel().Contains(FString::Printf(TEXT(" %s "), *Kind.ToString()));
}

bool RTSCampaignRemnants::IsFactionRegionFlag(const FString& Label, int32 Faction)
{
	if (Label.IsEmpty() || Faction < 0 || Faction >= RTSCampaignFactions::FactionCount) { return false; }
	// Daily-camp markers follow the camp lifecycle in ARTSCampaign::UpdateCamps, never a faction base.
	if (Label.Contains(TEXT("每日小据点"))) { return false; }
	// CreateRegion names the flag after its faction; a restored world appends the objective status instead.
	return Label.Contains(RTSCampaignFactions::Name[Faction]) || IsDefeatedStatusFlag(Label)
		|| Label.Contains(TEXT("摧毁敌方大本营"));
}

bool RTSCampaignRemnants::IsDefeatedStatusFlag(const FString& Label)
{
	return Label.Contains(DefeatedFlagText);
}

bool RTSCampaignRemnants::WithinBaseRadius(const FVector& BaseCentre, const FVector& Position)
{
	return !Position.ContainsNaN() && FVector::Dist2D(BaseCentre, Position) <= BaseRadius;
}

int32 RTSCampaignRemnants::ClearDefeatedFactionBuilding(UWorld* World, int32 Faction, FName BuildingId)
{
	if (!World || Faction < 0 || Faction >= RTSCampaignFactions::FactionCount) { return 0; }
	const FVector Centre = RTSCampaignFactions::Base[Faction];
	// The headquarters defines the base; its loss retires every silhouette and the flag of that faction.
	const bool bWholeBase = BuildingId == TEXT("TownHall");
	int32 Silhouettes = 0;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		// Proximity first: one base's cleanup must never reach a neighbour base or open scenery.
		if (!WithinBaseRadius(Centre, It->GetActorLocation()) || !IsFactionBaseDressing(**It)) { continue; }
		if (!bWholeBase && !IsFactionBaseDressingOfKind(**It, BuildingId)) { continue; }
		if (HideDressing(**It)) { ++Silhouettes; }
	}
	const int32 Flags = bWholeBase ? RemoveFlags(World, Centre, Faction, false) : 0;
	if (Silhouettes + Flags)
	{
		UE_LOG(LogTemp, Display, TEXT("CAMPAIGN_REMNANTS_CLEARED faction=%d kind=%s silhouettes=%d flags=%d"),
			Faction, *BuildingId.ToString(), Silhouettes, Flags);
	}
	return Silhouettes + Flags;
}

int32 RTSCampaignRemnants::SyncFactionBases(UWorld* World, int32 RegionMask)
{
	if (!World) { return 0; }
	int32 Changed = 0;
	for (int32 Faction = 0; Faction < RTSCampaignFactions::FactionCount; ++Faction)
	{
		// RegionMask bit set == this faction's headquarters fell; the base must stay invisible after a load.
		const bool bDefeated = (RegionMask & (1 << RTSCampaignFactions::RegionIndex[Faction])) != 0;
		const FVector Centre = RTSCampaignFactions::Base[Faction];
		int32 Silhouettes = 0;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (!WithinBaseRadius(Centre, It->GetActorLocation()) || !IsFactionBaseDressing(**It)) { continue; }
			// Reversible on purpose: loading a save from before the victory brings the base back.
			if (bDefeated ? HideDressing(**It) : ShowDressing(**It)) { ++Silhouettes; }
		}
		const int32 Flags = bDefeated ? RemoveFlags(World, Centre, Faction, true) : 0;
		if (Silhouettes + Flags)
		{
			Changed += Silhouettes + Flags;
			UE_LOG(LogTemp, Display, TEXT("CAMPAIGN_REMNANTS_SYNC faction=%d defeated=%d silhouettes=%d flags=%d"),
				Faction, bDefeated ? 1 : 0, Silhouettes, Flags);
		}
	}
	return Changed;
}
