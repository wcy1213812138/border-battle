#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSHealFeedbackComponent.generated.h"
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

/** Bounded, reusable emissive healing motes. No lights, collision or body material swaps. */
UCLASS(ClassGroup = (RTS))
class URTSHealFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URTSHealFeedbackComponent();
	void Pulse(bool bCaster = false);
	bool IsPlaying() const { return bPlaying; }
	int32 GetPulseCount() const { return PulseCount; }
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
	float Age = 0;
	bool bPlaying = false;
	bool bCasterPulse = false;
	int32 PulseCount = 0;
};
