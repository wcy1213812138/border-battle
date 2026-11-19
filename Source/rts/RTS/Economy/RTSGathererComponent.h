#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/RTSTypes.h"
#include "Economy/RTSEconomySnapshot.h"
#include "RTSGathererComponent.generated.h"
class ARTSResourceNode;
class ARTSBuilding;
UCLASS(ClassGroup = (RTS))
class URTSGathererComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URTSGathererComponent();
	void NotifyNewOrder();
	bool IsWorking() const { return bWorking; }
	int32 GetCarriedAmount() const { return Carried; }
	ERTSResource GetCarriedType() const { return CargoType; }
	const FString& GetStatus() const { return Status; }
	bool IsWaitingForWorkPosition() const { return bWaitingForWorkPosition; }
	bool HasWorkPosition() const { return bHasWorkPosition; }
	FVector GetWorkPosition() const { return WorkPosition; }
	/** Read-only HUD destination for the worker's actual return trip. */
	AActor* GetReturnGuideTarget() const { return bReturning ? ApproachTarget.Get() : nullptr; }
	FRTSGathererSnapshot CaptureEconomy() const;
	bool RestoreEconomy(const FRTSGathererSnapshot& State);
	static bool IsValidEconomy(const FRTSGathererSnapshot& State,int32 CarryCapacity);
	FSimpleMulticastDelegate OnWorkChanged;
protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	/** Extra horizontal interaction reach, beyond the target's existing work envelope (cm). */
	UPROPERTY(EditAnywhere, Category = "RTS|Work", meta = (ClampMin = "0", ClampMax = "200"))
	float FoodReachExtra = 120.f;
	UPROPERTY(EditAnywhere, Category = "RTS|Work", meta = (ClampMin = "0", ClampMax = "200"))
	float WoodReachExtra = 60.f;
	UPROPERTY(EditAnywhere, Category = "RTS|Work", meta = (ClampMin = "0", ClampMax = "200"))
	float BuildingReachExtra = 60.f;

private:
	bool Approach(AActor* Target, float Radius, bool bOuterWaitOnly = false);
	ARTSBuilding* FindDropOff() const;
	void ResetApproach();
	bool RequestApproachStep();
	TWeakObjectPtr<AActor> ApproachTarget;
	TArray<FVector> ApproachPath;
	FVector ApproachTargetLocation = FVector::ZeroVector;
	FVector WorkPosition = FVector::ZeroVector;
	bool bHasWorkPosition = false;
	bool bWaitingForWorkPosition = false;
	double NextPlanTime = 0;
	double LastProgressTime = 0;
	double BestStepDistance = TNumericLimits<double>::Max();
	FVector BlockedWorkPosition = FVector::ZeroVector;
	double AvoidBlockedWorkUntil = 0;
	bool bWaitingForNodeSlot = false;
	bool bWorkPreciseArrival = false;
	void SetStatus(const FString& Text);
	int32 Carried = 0;
	ERTSResource CargoType = ERTSResource::Wood;
	bool bReturning = false;
	bool bWorking = false;
	float WorkElapsed = 0;
	FString Status = TEXT("空闲");
};
