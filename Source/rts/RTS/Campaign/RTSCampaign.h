#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/SaveGame.h"
#include "GameFramework/GameModeBase.h"
#include "Core/RTSTypes.h"
#include "Campaign/RTSCampaignDailyTypes.h"
#include "Campaign/RTSCampaignMapSpace.h"
#include "Economy/RTSEconomySnapshot.h"
#include "RTSCampaign.generated.h"
class ARTSUnit;
class ARTSBuilding;
class ARTSResourceNode;
class URTSUnitData;
class UInstancedStaticMeshComponent;

USTRUCT()
struct FRTSCampaignUnitSave
{
 GENERATED_BODY()
 UPROPERTY() FName Kind;
 UPROPERTY() uint8 Team = 0;
 UPROPERTY() FVector Feet = FVector::ZeroVector;
 UPROPERTY() float HealthFraction = 1;
 UPROPERTY() float MaxHealth = 0;
 UPROPERTY() float Damage = 0;
 UPROPERTY() int32 Encounter = -1;
 UPROPERTY() int32 Faction = 0;
 // Version 12 uses snapshot-local references, never serialized actor pointers.
 UPROPERTY() ERTSOrderType OrderType = ERTSOrderType::None;
 UPROPERTY() uint8 TargetClass = 0; // 0 none, 1 fixed node, 2 daily-site id, 3 building, 4 unit, 5 farm source
 UPROPERTY() int32 TargetIndex = -1;
 UPROPERTY() FVector OrderLocation = FVector::ZeroVector;
 UPROPERTY() int32 MoveGroup = -1;
 UPROPERTY() FVector MoveGoal = FVector::ZeroVector;
 UPROPERTY() FRTSGathererSnapshot Economy;
 UPROPERTY() ERTSCombatStance Stance = ERTSCombatStance::Patrol;
 UPROPERTY() float CollisionRadius = 40;
 UPROPERTY() float Facing = 0;
};
USTRUCT()
struct FRTSCampaignCampSave
{
 GENERATED_BODY()
 UPROPERTY() int32 Id = 0;
 UPROPERTY() int32 Tier = 0;
 UPROPERTY() FVector Center = FVector::ZeroVector;
 UPROPERTY() double Expires = 0;
 UPROPERTY() bool Won = false;
 UPROPERTY() float Capture = 0;
 UPROPERTY() TMap<ERTSResource,int32> Remaining;
};
USTRUCT()
struct FRTSCampaignBuildingWorkSave
{
 GENERATED_BODY()
 UPROPERTY() float Progress = 1;
 UPROPERTY() float Facing = 0;
 UPROPERTY() FRTSProductionSnapshot Production;
 UPROPERTY() bool bHasFoodSource = false;
 UPROPERTY() FVector FoodFeet = FVector::ZeroVector;
 UPROPERTY() int32 FoodRemaining = 0;
 UPROPERTY() float FoodRemainder = 0;
};
USTRUCT()
struct FRTSFactionEconomySave
{
 GENERATED_BODY()
 UPROPERTY() TMap<ERTSResource,int32> Stock;
 // Advanced entries also record completed paid research (level >= 1).
 UPROPERTY() TMap<FName,int32> Levels;
 UPROPERTY() double NextRaid = 0;
 UPROPERTY() double RetreatAt = 0;
 UPROPERTY() bool Active = true;
 UPROPERTY() bool Deployed = false;
};
USTRUCT()
struct FRTSEnemyNodeSave
{
 GENERATED_BODY()
 UPROPERTY() int32 Faction = 0;
 UPROPERTY() ERTSResource Type = ERTSResource::Wood;
 UPROPERTY() FVector Feet = FVector::ZeroVector;
 UPROPERTY() int32 Remaining = 6000;
 UPROPERTY() float Remainder = 0;
 UPROPERTY() uint8 Owner = 1;
 UPROPERTY() FRTSResourceCaptureSnapshot Capture;
};
UCLASS()
class URTSCampaignSave : public USaveGame
{
 GENERATED_BODY()
public:
 // Keep the historical default: UE may omit default-valued fields from older snapshots.
 // New snapshots explicitly write SnapshotVersion; map layout stays independently at 11.
 UPROPERTY() int32 Version = 10;
 static constexpr int32 SnapshotVersion = 15;
 UPROPERTY() FString CampaignId;
 UPROPERTY() double Elapsed = 0;
 UPROPERTY() int32 SwordLevel = 1;
 UPROPERTY() int32 ArcherLevel = 1;
 UPROPERTY() int32 RegionMask = 0;
 UPROPERTY() TMap<FName,int32> UnitLevels;
 UPROPERTY() TArray<FRTSCampaignDailySiteSave> DailySites;
 UPROPERTY() int32 DailyGeneratedDay = -1;
 UPROPERTY() int32 FacilityMask = 0;
 UPROPERTY() int32 PopulationPurchases = 0;
 UPROPERTY() int32 LegacyPopulationBonus = 0;
 UPROPERTY() int32 ResearchMask = 0;
 UPROPERTY() int32 SeenGuideEvents = 0;
 UPROPERTY() int32 SpawnMask = 0;
 UPROPERTY() int32 SpawnDay = 0;
 UPROPERTY() bool Completed = false;
 UPROPERTY() int32 GuideStep = 0;
 UPROPERTY() int32 RaidMask = 0;
 UPROPERTY() int32 RaidDay = 0;
 UPROPERTY() TArray<bool> RegionSpawnValid;
 UPROPERTY() TMap<ERTSResource,int32> Stock;
 UPROPERTY() TArray<FRTSCampaignUnitSave> Units;
 UPROPERTY() TArray<FRTSCampaignCampSave> Camps;
 UPROPERTY() TArray<int32> NodeRemaining;
 UPROPERTY() TArray<float> NodeRemainders;
 UPROPERTY() TArray<int32> NodeTiers;
 UPROPERTY() TArray<uint8> NodeOwners;
 UPROPERTY() TArray<FName> BuildingKinds;
 UPROPERTY() TArray<FVector> BuildingPositions;
 UPROPERTY() TArray<float> BuildingHealth;
 UPROPERTY() TArray<FRTSCampaignBuildingWorkSave> BuildingWork;
 UPROPERTY() int32 PendingFacility = -1;
 UPROPERTY() float FacilitySeconds = 0;
 UPROPERTY() float SimulationRemainder = 0;
 UPROPERTY() float RefugeeSeconds = 0;
 UPROPERTY() TArray<int32> RaidWaves;
 UPROPERTY() TArray<float> RegionCapture;
 UPROPERTY() TArray<FRTSResourceCaptureSnapshot> NodeCapture;
 UPROPERTY() TArray<FRTSResourceCaptureSnapshot> DailyCapture;
 UPROPERTY() TArray<FRTSFactionEconomySave> FactionEconomies;
 UPROPERTY() TArray<int32> BuildingFactions; // -1 player, otherwise campaign faction
 UPROPERTY() TArray<uint8> BuildingTeams;
 UPROPERTY() TArray<FRTSEnemyNodeSave> EnemyNodes;
};

struct FRTSCampaignCamp
{
 FRTSCampaignCampSave State;
 TArray<TWeakObjectPtr<ARTSUnit>> Guards;
 TWeakObjectPtr<AActor> Marker;
};

struct FRTSCampaignRaidPlan
{
 int32 Faction=0, Swords=0, Archers=0;
 float Launch=0;
 FVector Origin=FVector::ZeroVector, Target=FVector::ZeroVector;
 FString Intent;
};
struct FRTSCampaignRaid
{
 FRTSCampaignRaidPlan Plan;
 TArray<TWeakObjectPtr<ARTSUnit>> Units;
 double Began=0;
 bool Returning=false;
};
UCLASS()
class ARTSCampaign : public AActor
{
 GENERATED_BODY()
public:
 ARTSCampaign();
 static ARTSCampaign* Get(const UObject* Context);
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 FString GetOverview() const;
 FString GetUpgradeText(FName Kind) const;
 FString GetObjectives() const;
 FString GetMessage() const { return Message; }
 void NotifyCivilianAttacked(AActor* Victim);
 void NotifyFirstIncoming(ARTSUnit* Enemy);
 FString GetAlertText() const;
 int32 GetLevel(FName Kind) const;
 static int32 AppearanceForLevel(int32 Level);
 static constexpr float DaySeconds=300.f;
 int32 GetDay() const { return FMath::FloorToInt(Elapsed/DaySeconds); }
 TMap<ERTSResource,int32> GetTrainingCost(FName Kind,const AActor* Producer=nullptr) const;
 UFUNCTION(BlueprintCallable, Category="RTS|Editor")
 static bool BuildNavigation(class ANavMeshBoundsVolume* Volume);
 UFUNCTION(BlueprintCallable, Category="RTS|Editor")
 static bool PrepareNavigationForInspection(UObject* Context);
 bool Upgrade(FName Kind);
 void AssignWorkers(int32 ResourceIndex);
 void AttackObjective(int32 Index);
 void FocusHome();
 bool SaveCampaign();
 bool LoadCampaign();
 // Startup restore may bypass idle-state checks only in a pristine ?CampaignContinue world.
 bool LoadCampaignOnEntry();
 static bool HasCampaignSave();
 bool LoadPreviousCampaign();
 static bool HasPreviousCampaign();
 // Automation runs a private slot so a test save can never touch a player's campaign.
 static FString SaveSlotName();
 static void SetSaveSlotOverride(const TCHAR* Slot);
	// P0-05: automatic safe-point saves use a separate slot so manual saves are never overwritten by them.
	static constexpr float AutosaveInterval = 600.f;
	// P0-05: slot 0 is the main manual slot; 1-3 are additional manual slots.
	static FString SlotNameFor(int32 Index);
	bool SaveToSlot(int32 Index);
	bool LoadFromSlot(int32 Index);
	static bool HasSlot(int32 Index);
	/** Start-menu cleanup list: size and timestamp of one manual slot, without loading it. */
	/** Base name (production or automation override) that slot suffixes hang off. */
	static FString BaseSlotName();
	/** Manual slot currently being played; Continue and autosaves follow it. */
	static int32 ActiveSlot();
	static void SetActiveSlot(int32 Index);
	/** Day-start rollback record: written automatically when the in-game day rolls over. */
	static FString DaySnapshotSlotName() { return SaveSlotName()+TEXT("_day"); }
	bool SaveDaySnapshot();
	/** True only when the stored record really belongs to the day now being played. */
	bool HasDaySnapshot() const;
	bool LoadDaySnapshot();
	struct FRTSSlotInfo { bool bExists = false; int64 Size = 0; FDateTime Modified; };
	static bool DescribeSlot(int32 Index, FRTSSlotInfo& OutInfo);
	/**
	 * Player-invoked cleanup from the start menu: removes this slot together with its own previous
	 * backup (slot 0 also removes its autosave chain). Returns false when nothing existed.
	 */
	static bool DeleteSlotRecords(int32 Index, FString& OutSummary);
	static FString AutosaveSlotName();
	bool SaveAutosave();
	static bool HasAutosave();
	bool LoadAutosave();
 void PrepareSave();
 // Pure, idempotent legacy XY migration; snapshot schema 12 already uses layout 11.
 static bool MigrateCompactLayout(URTSCampaignSave* Save);
 bool BuyFacility(int32 Index);
 static TMap<ERTSResource,int32> FacilityCost(int32 Index);
 static float FacilityBuildSeconds(int32 Index) { return Index==0?10.f:20.f; }
 bool IsFacilityComplete(int32 Index) const { return Index>=0 && Index<5 && (FacilityMask&(1<<Index))!=0; }
 int32 GetPendingFacility() const { return PendingFacility; }
 float GetFacilitySecondsRemaining() const { return FacilitySeconds; }
 int32 GetPopulationLimit() const;
 int32 GetPopulationPurchases() const { return PopulationPurchases; }
 TMap<ERTSResource,int32> PopulationExpansionCost() const;
 bool CanBuyPopulation(FString& Error) const;
 bool BuyPopulation();
 bool Train(FName Kind);
 bool CanTrain(FName Kind, FString& Error) const;
 ARTSBuilding* FindServiceBuilding(FName Kind) const;
 bool SelectServiceBuilding(FName Kind);
 FName GetSelectedService() const;
 bool CanUseService(FName Kind) const;
 bool ResearchUnit(FName Kind);
 bool IsUnitUnlocked(FName Kind) const;
 FString GetResearchText(FName Kind) const;
 static FName TrainingService(FName Kind);
 ARTSUnit* Spawn(FName Kind,uint8 Team,const FVector& Feet,float HP=0,float Damage=0);
 void ApplyProgression(ARTSUnit* Unit);
	// Explicit end states for the settlement panel.
	bool IsCompleted() const { return bCompleted; }
	bool IsDefeated() const { return bDefeated; }
 bool IsReady() const { return bReady; }
 double GetElapsed() const { return Elapsed; }
	/** P1-04: idle workers and producers waiting for a free exit, for player-facing feedback. */
	int32 GetIdleWorkerCount() const;
	int32 GetBlockedProductionCount() const;
 int32 GetRegionMask() const { return RegionMask; }
 FString GetGuidance() const;
 void AdvanceGuide();
 int32 GetGuideStep() const { return GuideStep; }
 void NotifyPageOpened(int32 Index);
 FString GetWarPlan() const;
 static FRTSCampaignRaidPlan RaidPlan(int32 Day,int32 Faction);
 // Enemy troops climb the very same 1.10^(level-1) table the player upgrades on, but by day instead of by purchase.
 static int32 EnemyLevelForDay(int32 Day) { return FMath::Clamp(1 + Day, 1, 30); }
 static int32 EnemyLevelForDayAndTier(int32 Day,int32 TierBoost) { return FMath::Clamp(EnemyLevelForDay(Day) + FMath::Max(0, TierBoost - 1), 1, 30); }
 /** Enemy rosters unlock the same kinds by day: light infantry, spears, heavy troops, paladins and finally kings. */
 static FName EnemyKindForDay(int32 Day,int32 Slot,bool bRanged);
 /** Daily wave budget: one wave early, up to three once the campaign reaches the late game. */
 static int32 EnemyWaveLimit(int32 Day) { return 1 + FMath::Min(Day/6, 2); }
 void ApplyEnemyProgression(ARTSUnit* Unit,int32 Day,int32 TierBoost = 0);
 int32 GetActiveRaidCount() const { return Raids.Num(); }
 int32 GetSeenGuideEvents() const { return SeenGuideEvents; }
 void MarkGuideEvent(int32 Bit) { SeenGuideEvents |= Bit; }
 int32 GetUnitFaction(ARTSUnit* Unit) const;
 void CycleFactionColor(int32 Faction);
 bool IsVineyardSpawned() const { return bVineyardSpawned; }
 ARTSResourceNode* GetVineyardNode() const;
 // The single day-5 wild vineyard. Campaign owns the numbers; save validation reads them too.
 static constexpr float VineyardSpawnElapsed = 4*DaySeconds;
 static constexpr int32 VineyardTier = 3;
 static constexpr int32 VineyardCapacity = 18000;
 static constexpr float VineyardRecoveryPerSecond = 8.f;
 static constexpr int32 VineyardGatherSlots = 8;
 static constexpr float VineyardCaptureSeconds = 5.f;
 static constexpr float VineyardCaptureRadius = 700.f;
 static FVector VineyardLocation();
 float GetVineyardCaptureProgress() const;
 FString GetDailyOverview() const;
 int32 GetDailySiteCount() const { return DailySites.Num(); }
 // Pure data: the daily slot layout is inspectable without spawning a world.
 static ERTSResource DailySiteTypeForDay(int32 Day,int32 Slot);
 // Map dressing: scenery instanced in one actor, all of it non-colliding and navigation-inert.
 static FVector HomeLocation() { return RTSCampaignMapSpace::FromLegacy(FVector(-3600, 0, 0)); }
 AActor* GetDecorations() const { return Decor; }
 int32 GetDecorationCount() const;
 // Clear only visual ISM dressing; real resource trees, terrain and gameplay actors are untouched.
 void ClearSceneryNear(const FVector& Position,double Radius=1400.0);
 // Combat Team 1 remains allied; economic identity is campaign-local, never a combat team.
 int32 GetActorFaction(const AActor* Actor) const;
 class URTSResourceComponent* GetActorAccount(const AActor* Actor) const;
 void RegisterProducedUnit(ARTSUnit* Unit,const ARTSBuilding* Producer);
 void StepFactionEconomy(int32 Faction);
 bool CanFactionTrain(const AActor* Producer,FName Kind) const;
 bool IsFactionEconomyActive(int32 Faction) const { return EconomyStates.IsValidIndex(Faction) && EconomyStates[Faction].Active; }
 bool IsManagedEconomyUnit(const ARTSUnit* Unit) const;
private:
 bool BootstrapEnemyEconomy();
 void StartEconomyTrees();
 bool RestoreEnemyNode(const FRTSEnemyNodeSave& State);
 UPROPERTY() TArray<TObjectPtr<class URTSResourceComponent>> FactionAccounts;
 UPROPERTY() TArray<TObjectPtr<class ARTSCampaignEconomyController>> EconomyControllers;
 TArray<FRTSFactionEconomySave> EconomyStates;
 TMap<TWeakObjectPtr<AActor>,int32> EconomicFactions;
 UPROPERTY() TArray<TObjectPtr<ARTSResourceNode>> EnemyNodes;
 TArray<int32> EnemyNodeFactions;
private:
 void Start();
 void ClearOwned();
 void CreateBuildings();
 void CreateNodes();
 void CreateRegions();
 void CreateDecorations();
 void CreateRegion(int32 Region);
 void SpawnCamp(int32 Slot);
 void UpdateRegions(float Seconds);
 void UpdateCamps(float Seconds);
 void ConfigureNodes();
 void EnsureVineyardNode(bool bForce = false);
 void UpdateVineyard(float Seconds);
 void Focus(const FVector& Position);
 AActor* Marker(const FVector& Position,const FString& Label,FLinearColor Color);
 double LastDailyDiagnostic=-1000;
 double LastDailyGenerationAttempt=-1000;
 bool ValidateSave(const URTSCampaignSave* Save) const;
 bool LoadSnapshotFromSlot(const FString& RequestedSlot,bool bAllowFallback);
 bool IsSafe(FString& Reason) const;
 bool IsSafeToLoad(FString& Reason) const;
 bool LoadCampaignInternal(bool bFromEntry,bool bPrevious=false,bool bAutosave=false);
 URTSCampaignSave* CaptureSnapshot(FString& Error) const;
 bool RestoreSnapshot(const URTSCampaignSave* Save,FString& Error);
 bool PreflightSnapshot(const URTSCampaignSave* Save,FString& Error) const;
 bool bStagingRestore=false;
 friend struct FRTSCampaignPersistenceTestAccess;
 friend class URTSCampaignRestoreProbe;
 friend struct FRTSCampaignEnemyTestAccess;
 bool bEntryLoadConsumed=false;
 TMap<ERTSResource,int32> CampReward(int32 Tier) const;
 void Award(FRTSCampaignCamp& Camp);
 void UpdateGuide();
 void UpdateWar(float Seconds);
 void UpdateDaily(float Seconds);
 bool SpawnDailySite(const FRTSCampaignDailySiteSave& State,bool bRestore=false);
 int32 RestoreDailySites(const TArray<FRTSCampaignDailySiteSave>& States);
 void SnapshotDailySites(TArray<FRTSCampaignDailySiteSave>& States) const;
 void RegisterDailyUnit(ARTSUnit* Unit,int32 Encounter);
 TArray<FRTSCampaignDailySite> DailySites;
 int32 DailyGeneratedDay=-1;
 TMap<FName,int32> UnitLevels;
 void ApplyFaction(ARTSUnit* Unit,int32 Faction);
 TArray<FRTSCampaignRaid> Raids;
 TMap<TWeakObjectPtr<ARTSUnit>,int32> Factions;
 TWeakObjectPtr<ARTSUnit> GuideWorker;
 FVector GuideWorkerStart=FVector::ZeroVector;
 int32 GuideStep=0, LastGuidePage=-1, RaidDay=0, RaidMask=0;
 /** Waves already launched today per faction, and the enemy level each unit was scaled to. */
 TArray<int32> RaidWaves;
 TMap<TWeakObjectPtr<ARTSUnit>,int32> AppliedEnemyLevels;
 int32 GuideWoodBaseline=500;
 float WarDecisionTimer=0;
 UPROPERTY() TArray<TObjectPtr<AActor>> Owned;
 /** One actor holds every instanced scenery mesh: pines, boulders, rock shelves and ground patches. */
 UPROPERTY() TObjectPtr<AActor> Decor;
 UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> DecorComponents;
 UPROPERTY() TArray<TObjectPtr<ARTSResourceNode>> Nodes;
 TWeakObjectPtr<ARTSResourceNode> Vineyard;
 bool bVineyardSpawned = false;
 float VineyardCapture = 0;
 UPROPERTY() TArray<TObjectPtr<ARTSBuilding>> Buildings;
 TArray<TArray<TWeakObjectPtr<ARTSUnit>>> RegionGuards;
 TArray<FRTSCampaignCamp> Camps;
 TMap<TWeakObjectPtr<ARTSUnit>,int32> AppliedLevels;
 TMap<TWeakObjectPtr<ARTSUnit>,int32> EncounterIds;
 TArray<float> RegionCapture;
 TArray<bool> RegionSpawnValid;
 FString CampaignId;
 FString Message;
 FString AlertText;
 double AlertUntil=-1000;
 double LastCivilianWarning=-1000;
 double Elapsed = 0;
 float Accumulator = 0;
 float StartupWait = 0;
 int32 SwordLevel = 1, ArcherLevel = 1;
 int32 RegionMask = 0, FacilityMask = 0;
 int32 PopulationPurchases = 0;
 int32 LegacyPopulationBonus = 0;
 int32 ResearchMask = 0;
 int32 SeenGuideEvents = 0;
 int32 SpawnDay = 0, SpawnMask = 0;
 bool bReady = false, bCompleted = false, bTransaction = false;
	/** Last in-game day whose start snapshot was written, so a rollover never repeats. */
	int32 LastDaySnapshotDay = -1;
 int32 PendingFacility = -1;
 float FacilitySeconds = 0;
 float RefugeeTimer = 0;
 float LastAutosaveElapsed=-1000;
 bool bDefeated=false;
};
UCLASS()
class ARTSCampaignGameMode : public AGameModeBase
{
 GENERATED_BODY()
public:
 ARTSCampaignGameMode();
 virtual void BeginPlay() override;
};
