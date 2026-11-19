#include "Combat/RTSProjectile.h"
#include "Combat/RTSAttackComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"

ARTSProjectile::ARTSProjectile()
{
	PrimaryActorTick.bCanEverTick = true;
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileVisual"));
	SetRootComponent(Visual);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Mesh.Succeeded()) { Visual->SetStaticMesh(Mesh.Object); }
	Visual->SetRelativeScale3D(FVector(0.36, 0.035, 0.035));
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetCastShadow(false);
}

void ARTSProjectile::Initialize(AActor* Source, AActor* Target, float Damage, float Speed)
{
	SourceActor = Source;
	TargetActor = Target;
	HitDamage = FMath::Max(0.0f, Damage);
	TravelSpeed = FMath::Max(100.0f, Speed);
	SetLifeSpan(10.0f);
}

void ARTSProjectile::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AActor* Source = SourceActor.Get();
	AActor* Target = TargetActor.Get();
	// A projectile already in flight survives its shooter's death, but not target death.
	if (!URTSAttackComponent::IsHostileTarget(Source, Target)) { Destroy(); return; }
	const FVector Start = GetActorLocation();
	const FVector Destination = URTSAttackComponent::AimLocation(Target);
	const FVector Delta = Destination - Start;
	const float Step = TravelSpeed * DeltaSeconds;
	const bool bArrived = Delta.SizeSquared() <= FMath::Square(Step);
	const FVector End = bArrived ? Destination : Start + Delta.GetSafeNormal() * Step;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSProjectile), false, Source);
	Params.AddIgnoredActor(this);
	Params.AddIgnoredActor(Target);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		Destroy();
		return;
	}
	SetActorLocation(End);
	SetActorRotation(Delta.Rotation());
	if (bArrived)
	{
		if (URTSHealthComponent* Health = Target->FindComponentByClass<URTSHealthComponent>())
		{
			Health->ApplyDamage(HitDamage, Source);
		}
		Destroy();
	}
}
