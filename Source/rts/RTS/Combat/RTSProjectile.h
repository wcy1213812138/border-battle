// MiniRTS - visible targeted projectile; damage is applied on arrival, never on launch.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSProjectile.generated.h"

class UStaticMeshComponent;

UCLASS()
class ARTSProjectile : public AActor
{
	GENERATED_BODY()
public:
	ARTSProjectile();
	void Initialize(AActor* Source, AActor* Target, float Damage, float Speed);

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "RTS|Projectile")
	TObjectPtr<UStaticMeshComponent> Visual;

private:
	TWeakObjectPtr<AActor> SourceActor;
	TWeakObjectPtr<AActor> TargetActor;
	float HitDamage = 0.0f;
	float TravelSpeed = 1200.0f;
};
