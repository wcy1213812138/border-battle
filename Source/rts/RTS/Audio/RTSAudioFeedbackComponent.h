#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSAudioFeedbackComponent.generated.h"

class URTSHealthComponent;
class URTSAttackComponent;

UCLASS(ClassGroup = (RTS))
class URTSAudioFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URTSAudioFeedbackComponent();
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UFUNCTION() void AttackStarted(AActor* Target);
	UFUNCTION() void HealthChanged(URTSHealthComponent* Health, float NewHealth, float Delta);
	UFUNCTION() void Died(URTSHealthComponent* Health, AActor* Killer);
	TWeakObjectPtr<URTSHealthComponent> BoundHealth;
	TWeakObjectPtr<URTSAttackComponent> BoundAttack;
};
