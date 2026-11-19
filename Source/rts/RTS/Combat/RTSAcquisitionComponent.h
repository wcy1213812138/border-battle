// MiniRTS - idle target acquisition. Uses the same Orders entry point as human input.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/RTSTypes.h"
#include "RTSAcquisitionComponent.generated.h"

UCLASS(ClassGroup = (RTS))
class URTSAcquisitionComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	URTSAcquisitionComponent();
	void NotifyNewOrder();
	/** Local combat diversion; the explicit attack order and queued orders stay intact. */
	AActor* ResolveAttackTarget(AActor* OrderedTarget);
	AActor* GetNearbyCombatTarget() const { return NearbyCombatTarget.Get(); }
	/** Enemy-only local alarm. Does not change player stances or interrupt explicit moves. */
	static void NotifyNearbyEnemyAllies(AActor* Victim, AActor* Attacker);
	bool IsSupportingAlly() const;
	void SetStance(ERTSCombatStance InStance);
	ERTSCombatStance GetStance() const { return Stance; }
	bool IsHoldingPosition() const { return Stance == ERTSCombatStance::Guard && AutomaticTarget.IsValid() && !bSupportingAlly; }
protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
	TWeakObjectPtr<AActor> AutomaticTarget;
	TWeakObjectPtr<AActor> NearbyCombatTarget;
	FVector NearbyCombatOrigin = FVector::ZeroVector;
	double NextNearbyScan = 0.0;
	double NearbyCombatUntil = 0.0;
	bool bSupportingAlly = false;
	bool RespondToAlly(AActor* Victim, AActor* Attacker);
	bool bHasCombatOrigin = false;
	FVector CombatOrigin = FVector::ZeroVector;
	double SuppressedUntil = 0.0;
	ERTSCombatStance Stance = ERTSCombatStance::Patrol;
	double GuardDamageAfter = -1000;
};
