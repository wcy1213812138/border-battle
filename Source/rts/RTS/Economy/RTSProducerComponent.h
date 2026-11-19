// MiniRTS - paid FIFO training queue. Progress is polled; structural changes are signalled.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Core/RTSTypes.h"
#include "Economy/RTSEconomySnapshot.h"
#include "RTSProducerComponent.generated.h"

class URTSResourceComponent;

UCLASS(ClassGroup = (RTS), meta = (BlueprintSpawnableComponent))
class URTSProducerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSProducerComponent();

	bool Enqueue(FName UnitId, FString& Error);
	/** Removes the tail, including an in-progress head if it is the only order. Full refund. */
	bool CancelLast();
	const TArray<FName>& GetQueue() const { return Queue; }
	float GetProgress() const;
	bool IsSpawnBlocked() const { return bSpawnBlocked; }
	FString GetSpawnBlockReason() const { return bSpawnBlocked ? (SpawnBlockReason.IsEmpty() ? TEXT("等待可用出口，正在重试") : SpawnBlockReason) : FString(); }
	bool CaptureProduction(FRTSProductionSnapshot& Out) const;
	static bool IsValidProduction(const FRTSProductionSnapshot& State,FName BuildingId,bool bComplete,bool bCampaign);
 bool RestoreProduction(const FRTSProductionSnapshot& State,URTSResourceComponent* StagedPayer = nullptr);

	/** Queue/head changes or blocked-state transitions only. Poll GetProgress for UI animation. */
	FSimpleMulticastDelegate OnQueueChanged;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FPaidOrder
	{
		int32 Wood = 0;
		int32 Food = 0;
		TMap<ERTSResource,int32> Cost;
		float TrainSeconds = 0.0f;
		TWeakObjectPtr<URTSResourceComponent> PaidFrom;
	};

	UPROPERTY(VisibleInstanceOnly, Category = "RTS|Production")
	TArray<FName> Queue;
	// Parallel to Queue; costs and duration are immutable snapshots, never shared UnitData.
	TArray<FPaidOrder> PaidOrders;
	float TrainingSeconds = 0.0f;
	bool bSpawnBlocked = false;
	bool bMutatingQueue = false;
	FString SpawnBlockReason;
	double NextSpawnAttempt = 0.0;
	static constexpr int32 MaxQueueSize = 5;
};
