#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSHitFeedbackComponent.generated.h"
class URTSHealthComponent;
class UMeshComponent;
class UMaterialInterface;

/** Presentation only: listens to real health loss and temporarily swaps body materials. */
UCLASS(ClassGroup = (RTS))
class URTSHitFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URTSHitFeedbackComponent();
	bool IsFlashing() const { return bFlashing; }
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UFUNCTION() void HealthChanged(URTSHealthComponent* Health, float NewHealth, float Delta);
private:
	void Restore();
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> FlashMaterial;
	UPROPERTY(Transient) TArray<TObjectPtr<UMeshComponent>> Meshes;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> PreviousMaterials;
	TArray<int32> MaterialSlots;
	TWeakObjectPtr<URTSHealthComponent> BoundHealth;
	FTimerHandle FlashTimer;
	bool bFlashing = false;
};
