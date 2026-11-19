// MiniRTS - base unit. A Character driven by an AIController; receives FRTSOrder from
// URTSOrderLibrary (player and AI share the same path). Stats and art come from URTSUnitData.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GenericTeamAgentInterface.h"
#include "Core/RTSTypes.h"
#include "Orders/RTSMoveGroup.h"
#include "Orders/RTSMovementProgress.h"
#include "RTSUnit.generated.h"

class URTSUnitData;
class URTSHealthComponent;
class URTSAttackComponent;
class URTSAcquisitionComponent;
class URTSGathererComponent;
class UStaticMeshComponent;
class UAnimSequence;
class ARTSUnit;

DECLARE_MULTICAST_DELEGATE_OneParam(FRTSUnitDiedNative, ARTSUnit*);

UENUM(BlueprintType)
enum class ERTSUnitAnimState : uint8
{
	None,
	Idle,
	Move,
	Attack,
	Death
};

UCLASS()
class ARTSUnit : public ACharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ARTSUnit();
	/** Configure deferred spawns before FinishSpawning; runtime stats are applied during initialization. */
	void Configure(URTSUnitData* Data, uint8 InTeam);
	URTSAttackComponent* GetAttack() const { return Attack; }
	ERTSUnitAnimState GetAnimState() const { return AnimState; }
	URTSGathererComponent* GetGatherer() const { return Gatherer; }

	// ---- Orders ----

	/** Replaces the current order. Called by URTSOrderLibrary only. */
	void ReceiveOrder(const FRTSOrder& Order);
	/**
	 * P1-03: append an order behind the current one instead of replacing it.
	 * Used by Shift+right-click; a bounded queue keeps repeated clicks harmless.
	 */
	void QueueOrder(const FRTSOrder& Order);
	int32 GetQueuedOrderCount() const { return OrderQueue.Num(); }
	void ClearQueuedOrders() { OrderQueue.Reset(); }
	/** Attack-move destination: remembered so combat on the way never cancels the march. */
	void SetAttackMoveGoal(const FVector& Goal) { AttackMoveGoal = Goal; bHasAttackMoveGoal = true; }
	bool HasAttackMoveGoal() const { return bHasAttackMoveGoal; }
	void ClearAttackMoveGoal() { bHasAttackMoveGoal = false; }

	/** Group move: head for the group's goal, then claim a formation slot near it. */
	void ReceiveGroupMove(const TSharedRef<FRTSMoveGroup>& Group, const FVector& InWaitingPoint = FVector::ZeroVector);

	UFUNCTION(BlueprintPure, Category = "RTS|Unit")
	const FRTSOrder& GetCurrentOrder() const { return CurrentOrder; }

	/** Read-only movement state, also used by integration tests. */
	int32 GetFormationSlotIndex() const { return GroupSlot; }
	/** Lane (column) this unit holds in a narrow-pocket group move; INDEX_NONE otherwise. */
	int32 GetFormationLane() const { return MoveGroup.IsValid() && MoveGroup->MemberLane.IsValidIndex(GroupMember) ? MoveGroup->MemberLane[GroupMember] : INDEX_NONE; }
	/** Index of this unit inside the shared formation, used to publish its progress. */
	int32 GetFormationMember() const { return GroupMember; }
	const FRTSMoveGroup* GetMoveGroup() const { return MoveGroup.Get(); }
	FVector GetSlotClaimLocation() const { return SlotClaimLocation; }
	/** Same data asset means the same formation unit type. */
	FName GetFormationUnitType() const;
	float GetStandingCollisionRadius() const { return StandingCapsuleRadius; }
	float GetFormationCollisionRadius() const { return FMath::Min(StandingCapsuleRadius, 28.f); }

	// ---- Selection ----

	void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintPure, Category = "RTS|Unit")
	bool IsSelected() const { return bSelected; }

	// ---- State ----

	UFUNCTION(BlueprintPure, Category = "RTS|Unit")
	bool IsAlive() const;

	UFUNCTION(BlueprintPure, Category = "RTS|Unit")
	URTSHealthComponent* GetHealth() const { return Health; }

	UFUNCTION(BlueprintPure, Category = "RTS|Unit")
	URTSUnitData* GetUnitData() const { return UnitData; }

	uint8 GetTeam() const { return TeamId; }

	ERTSCombatStance GetCombatStance() const;
	void SetCombatStance(ERTSCombatStance Stance);
	bool IsHoldingAutomaticPosition() const;
	bool IsRecoveringFromCongestion() const { return YieldPhase != 0 || TrafficPassPartner.IsValid() || GetWorldTimeForTraffic() < TrafficWaitUntil; }
	bool IsYieldingToTraffic() const { return YieldPhase != 0; }
	uint32 GetTrafficYieldCount() const { return TrafficYieldCount; }
	/** Idle friendly units may accept a real collision-checked sidestep, never an order replacement. */
	bool TryYieldTo(ARTSUnit* Requester, const FVector& TravelDirection);
	/** A bounded, symmetric exception for one friendly blocker, not a Pawn-channel change. */
	bool IsTrafficPassing(const ARTSUnit* Other) const { return Other && TrafficPassPartner.Get() == Other; }
	uint32 GetTrafficPassCount() const { return TrafficPassCount; }
	bool IsInTrafficPass() const { return TrafficPassPartner.IsValid(); }
	/** Requested anti-deadlock fallback: a travelling non-combat unit that cannot move for two
	 *  seconds ignores pawn collision once, for a short bounded window. */
	bool IsDeadlockUnsticking() const { return bUnstickActive; }
	uint32 GetDeadlockUnstickCount() const { return UnstickCount; }

	// ---- IGenericTeamAgentInterface ----
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(TeamId); }

	/** Fired once when this unit dies (native, for selection cleanup etc.). */
	FRTSUnitDiedNative OnUnitDied;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Pushes UnitData into mesh/capsule/movement/health/attack. Safe to call in editor. */
	void ApplyUnitData();
	void ApplyTeamAppearance();

	void TickAttackOrder(float DeltaSeconds);
	bool RequestBuildingAttackApproach(const class ARTSBuilding* Building);
	void TickHealing(float DeltaSeconds);
	void TickGroupMove();
	/** Narrow pocket: queue down one lane, claim when the deeper neighbour has passed. */
	void TickLaneEntry();
	/** Sends the AI to Location. Returns false if no path could be requested. */
	bool RequestMoveTo(const FVector& Location, float AcceptanceRadius);
	void LeaveMoveGroup();
	void UpdateAnimation();
	void PlayAnimState(ERTSUnitAnimState NewState);
	void FaceTowards(const FVector& WorldLocation, float DeltaSeconds);
	void StopAIMovement();

	UFUNCTION()
	void HandleDied(URTSHealthComponent* HealthComp, AActor* Killer);

	UFUNCTION()
	void HandleAttackStarted(AActor* Target);

	// ---- Config ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Unit")
	TObjectPtr<URTSUnitData> UnitData;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Unit")
	uint8 TeamId = RTSTeam::Player;

	/** Corpse stays this long after the death animation starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Unit", meta = (Units = "s"))
	float CorpseLifetime = 8.0f;

	/** Re-issue MoveToActor at most this often while chasing an attack target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Unit", meta = (Units = "s"))
	float ChaseRepathInterval = 0.5f;

	// ---- Components ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS|Unit")
	TObjectPtr<URTSHealthComponent> Health;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS|Unit")
	TObjectPtr<URTSAttackComponent> Attack;

	UPROPERTY(VisibleAnywhere, Category = "RTS|Unit")
	TObjectPtr<URTSAcquisitionComponent> Acquisition;
	UPROPERTY(VisibleAnywhere, Category = "RTS|Unit")
	TObjectPtr<URTSGathererComponent> Gatherer;

	UPROPERTY(VisibleAnywhere, Category = "RTS|Feedback")
	TObjectPtr<class URTSHitFeedbackComponent> HitFeedback;

	/** Flat ring under the unit, visible while selected. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS|Unit")
	TObjectPtr<UStaticMeshComponent> SelectionRing;

	/** Worker team marker follows the upper-arm bone, without gameplay collision. */
	UPROPERTY(VisibleAnywhere, Category = "RTS|Unit")
	TObjectPtr<UStaticMeshComponent> TeamArmband;

private:
	friend struct FRTSTrafficPassTestAccess;
	friend struct FRTSDeadlockTestAccess;
	friend class URTSGathererComponent;
	FRTSOrder CurrentOrder;
	bool bSelected = false;

	ERTSUnitAnimState AnimState = ERTSUnitAnimState::None;
	double AttackAnimEndTime = 0.0;
	double NextRepathTime = 0.0;
	bool TryRefreshMissingNavigation();
	double MissingNavRetryUntil = 0;
	double NextMissingNavRetry = 0;
	double NextMissingNavRefresh = 0;
	TWeakObjectPtr<ARTSBuilding> BuildingApproachTarget;
	FVector BuildingApproachProgress = FVector::ZeroVector;
	double BuildingApproachProgressTime = 0;
	TWeakObjectPtr<ARTSUnit> HealingTarget;
	double NextHealTime = 0;
	double HealResolveTime = -1;
	bool bHealingStopped = false;

	/** Shared formation of the current group move (null for other orders). */
	TSharedPtr<FRTSMoveGroup> MoveGroup;
	int32 GroupSlot = INDEX_NONE;
	int32 GroupMember = INDEX_NONE;
	double NextLaneRequest = 0.0;
	FVector SlotClaimLocation = FVector::ZeroVector;
	FVector GroupWaitingPoint = FVector::ZeroVector;
	double GroupMoveStarted = 0.0;
	bool bGroupApproachStarted = false;
	bool bLaneSeparationOff = false;
	double NextGroupMoveAttempt = 0.0;
	bool bFormationStalled = false;
	TArray<FVector> FormationWaypoints;
	TArray<FVector> FormationRouteSnapshot; // Reissuing the same route must not postpone recovery.
	FRTSMovementProgress FormationProgress;
	/** P1-03: queued orders, oldest first; only a genuinely finished order pops the next one. */
	TArray<FRTSOrder> OrderQueue;
	FVector AttackMoveGoal = FVector::ZeroVector;
	bool bHasAttackMoveGoal = false;
	/** Ends the active order; advances the queue or returns to idle. */
	void FinishOrder();
	FRTSMovementProgress TrafficProgress;
	void UpdateFormationCollision();
	float StandingCapsuleRadius = 40.f;
	double NextCollisionRestoreCheck = 0;
	/** Bounded pawn-collision waiver for a travelling unit that made no progress for 2 seconds. */
	void TickDeadlockUnstick();
	void BeginDeadlockUnstick();
	void EndDeadlockUnstick();
	FVector UnstickAnchor = FVector::ZeroVector;
	double UnstickLastMoveTime = 0.0;
	double UnstickUntil = 0.0;
	double UnstickCooldownUntil = 0.0;
	ECollisionResponse UnstickPawnResponse = ECR_Block;
	bool bUnstickActive = false;
	uint32 UnstickCount = 0;
	bool bUnstickHadRVO = false;
	void TickCongestionRecovery();
	void EndCongestionRecovery();
	void TickTrafficYield();
	bool TryTrafficPass(ARTSUnit* Blocker, const FVector& Direction);
	bool RequestTrafficPassMove();
	bool TickTrafficPass();
	bool bTrafficPassWasSettling = false;
	void EndTrafficPass(bool bResumeOrder = false);
	TWeakObjectPtr<ARTSUnit> TrafficPassPartner;
	bool bTrafficPassOwner = false;
	FVector TrafficPassOrigin = FVector::ZeroVector;
	FVector TrafficPassExit = FVector::ZeroVector;
	double TrafficPassUntil = 0;
	double TrafficPassCooldownUntil = 0;
	uint32 TrafficPassCount = 0;
	double GetWorldTimeForTraffic() const;
	double GetRemainingPathDistance() const;
	double GetRemainingFormationDistance() const;
	void BeginFormationRoute();
	bool IsTrafficSidesteppingOrWaiting() const { return YieldPhase == 1 || YieldPhase == 2; }
	friend struct FRTSMovementStallTestAccess;
	TWeakObjectPtr<ARTSUnit> YieldRequester;
	FVector YieldPoint = FVector::ZeroVector;
	FVector YieldOrigin = FVector::ZeroVector;
	FVector YieldDirection = FVector::ZeroVector;
	uint8 YieldPhase = 0; // 1: sidestep, 2: wait for passage, 3: return to reserved slot
	uint32 TrafficYieldCount = 0;
	double YieldStarted = 0;
	double YieldWaitUntil = 0;
	double YieldCooldownUntil = 0;
	double TrafficWaitUntil = 0;
	double NextTrafficSample = 0;
};
