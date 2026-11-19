// MiniRTS - balance-driven construction, production and a simple defending tower.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GenericTeamAgentInterface.h"
#include "Core/RTSTypes.h"
#include "Data/RTSBalanceSubsystem.h"
#include "RTSBuilding.generated.h"

class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;
class URTSAttackComponent;
class URTSHealthComponent;
class URTSProducerComponent;
class ARTSResourceNode;

UCLASS()
class ARTSBuilding : public AActor, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	ARTSBuilding();

	/** Call on a deferred spawn, before FinishSpawning. Actor location is the ground/feet position. */
	void Configure(FName Id, uint8 Team, bool bCompleted = false);
	FName GetBuildingId() const { return BuildingId; }
	uint8 GetTeam() const { return TeamId; }
	bool IsComplete() const { return bCompleted; }
	bool IsAlive() const;
	bool IsDropOff() const;
	float GetBuildProgress() const;
	/** Presentation only: distinguishes active work, approaching workers and abandoned sites. */
	FString GetConstructionStatus() const;
	float GetFootprint() const;
	/** Solid body only; placement still reserves the full footprint. */
	static FVector CollisionExtent(FName Id, float Footprint);
	URTSProducerComponent* GetProducer() const { return Producer; }
	URTSHealthComponent* GetHealth() const { return Health; }
	ARTSResourceNode* GetFoodSource() const { return FoodSource.Get(); }

	/** P1-03: production buildings send each new unit here before ordinary idle behaviour. */
	void SetRallyPoint(const FVector& Ground) { RallyPoint = Ground; bHasRallyPoint = true; }
	void ClearRallyPoint() { bHasRallyPoint = false; }
	bool HasRallyPoint() const { return bHasRallyPoint; }
	const FVector& GetRallyPoint() const { return RallyPoint; }

	/** Work seconds, not elapsed wall time. The caller must verify the worker is beside us. */
	/** Campaign-only world label text, drawn by the HUD with a CJK-capable font. */
	FString GetCampaignWorldLabel() const { return CampaignWorldLabel; }
	void AddConstructionWork(float Seconds);
	/** Restore only; no payment, work event or completion reward. */
	bool RestoreConstructionProgress(float Progress);
	bool RestoreFoodSource(const FVector& Feet,int32 Remaining,float Remainder,bool bStaged);
	FSimpleMulticastDelegate OnBuildingChanged;

	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamId) override;
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(TeamId); }
	virtual void GetSimpleCollisionCylinder(float& CollisionRadius, float& CollisionHalfHeight) const override;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void ApplyConfiguration();
	void UpdateVisuals();

	UFUNCTION()
	void HandleDied(URTSHealthComponent* HealthComp, AActor* Killer);

	UPROPERTY(VisibleAnywhere, Category = "RTS|Building")
	TObjectPtr<UBoxComponent> Collision;
	UPROPERTY(VisibleAnywhere, Category = "RTS|Building")
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, Category = "RTS|Building")
	TObjectPtr<UStaticMeshComponent> Roof;
	UPROPERTY(VisibleAnywhere, Category = "RTS|Building")
	TObjectPtr<URTSHealthComponent> Health;
	UPROPERTY(VisibleAnywhere, Category = "RTS|Building")
	TObjectPtr<URTSAttackComponent> Attack;
	UPROPERTY(VisibleAnywhere, Category = "RTS|Building")
	TObjectPtr<URTSProducerComponent> Producer;
	UPROPERTY(VisibleAnywhere, Category = "RTS|Feedback")
	TObjectPtr<class URTSHitFeedbackComponent> HitFeedback;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY(EditAnywhere, Category = "RTS|Building")
	FName BuildingId = TEXT("TownHall");
	UPROPERTY(EditAnywhere, Category = "RTS|Building")
	uint8 TeamId = RTSTeam::Player;
	UPROPERTY(EditAnywhere, Category = "RTS|Building")
	bool bCompleted = false;

	FRTSBalanceRow Balance;
bool bHasBalance = false;
	float ConstructionSeconds = 0.0f;
	bool bHealthInitialized = false;
	TWeakObjectPtr<ARTSResourceNode> FoodSource;
	bool bFarmSupplied = false;
	int32 AppliedVisualTeam = INDEX_NONE;
	/** Campaign HUD world label; empty outside the campaign. */
	FVector RallyPoint = FVector::ZeroVector;
	bool bHasRallyPoint = false;
	FString CampaignWorldLabel;
};
