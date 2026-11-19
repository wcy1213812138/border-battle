#include "Combat/RTSAttackComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSProjectile.h"
#include "GenericTeamAgentInterface.h"
#include "TimerManager.h"
#include "Engine/World.h"

URTSAttackComponent::URTSAttackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URTSAttackComponent::SetStats(float InDamage, float InRange, float InInterval, float InHitDelay)
{
	Damage = FMath::Max(0.0f, InDamage);
	Range = FMath::Max(0.0f, InRange);
	Interval = FMath::Max(0.05f, InInterval);
	HitDelay = FMath::Clamp(InHitDelay, 0.0f, Interval);
}

void URTSAttackComponent::SetProjectileStats(bool bInRanged, float InSpeed)
{
	bRanged = bInRanged;
	ProjectileSpeed = FMath::Max(100.0f, InSpeed);
}

bool URTSAttackComponent::IsHostileTarget(const AActor* Source, const AActor* Target)
{
	if (!IsValid(Source) || !IsValid(Target) || Source == Target) { return false; }
	const IGenericTeamAgentInterface* A = Cast<IGenericTeamAgentInterface>(Source);
	const IGenericTeamAgentInterface* B = Cast<IGenericTeamAgentInterface>(Target);
	if (!A || !B || A->GetGenericTeamId() == FGenericTeamId::NoTeam
		|| B->GetGenericTeamId() == FGenericTeamId::NoTeam || A->GetGenericTeamId() == B->GetGenericTeamId()) { return false; }
	const URTSHealthComponent* Health = Target->FindComponentByClass<URTSHealthComponent>();
	return Health && !Health->IsDead();
}

float URTSAttackComponent::EdgeDistance2D(const AActor* A, const AActor* B)
{
	if (!A || !B) { return TNumericLimits<float>::Max(); }
	float RadiusA = 0.0f, HalfA = 0.0f, RadiusB = 0.0f, HalfB = 0.0f;
	A->GetSimpleCollisionCylinder(RadiusA, HalfA);
	B->GetSimpleCollisionCylinder(RadiusB, HalfB);
	// Buildings use rectangular collision; an inscribed circle makes melee units
	// push into the long wall forever before the range check can succeed.
	const ARTSBuilding* Building=Cast<ARTSBuilding>(B);
	const AActor* Other=A;float OtherRadius=RadiusA;
	if(!Building){Building=Cast<ARTSBuilding>(A);Other=B;OtherRadius=RadiusB;}
	if(Building)
	{
		const FVector P=Building->GetActorTransform().InverseTransformPosition(Other->GetActorLocation());
		const FVector E=ARTSBuilding::CollisionExtent(Building->GetBuildingId(),Building->GetFootprint());
		const FVector Scale=Building->GetActorScale3D().GetAbs();
		const double DX=FMath::Max(0.0,FMath::Abs(P.X)-E.X)*Scale.X;
		const double DY=FMath::Max(0.0,FMath::Abs(P.Y)-E.Y)*Scale.Y;
		return FMath::Max(0.f,float(FMath::Sqrt(DX*DX+DY*DY))-OtherRadius);
	}
	return FMath::Max(0.0f, static_cast<float>(FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation())) - RadiusA - RadiusB);
}

FVector URTSAttackComponent::AimLocation(const AActor* Actor)
{
	if (!Actor) { return FVector::ZeroVector; }
	FVector Origin, Extent;
	Actor->GetActorBounds(true, Origin, Extent);
	return Origin;
}

bool URTSAttackComponent::HasLineOfSight(const AActor* Target) const
{
	if (!IsValid(Target) || !GetWorld()) { return false; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSAttackSight), false, GetOwner());
	Params.AddIgnoredActor(Target);
	FHitResult Hit;
	return !GetWorld()->LineTraceSingleByChannel(Hit, AimLocation(GetOwner()), AimLocation(Target), ECC_Visibility, Params);
}

bool URTSAttackComponent::IsInRange(const AActor* Target) const
{
	if (!IsHostileTarget(GetOwner(), Target) || EdgeDistance2D(GetOwner(), Target) > Range) { return false; }
	float RadiusA, HalfA, RadiusB, HalfB;
	GetOwner()->GetSimpleCollisionCylinder(RadiusA, HalfA);
	Target->GetSimpleCollisionCylinder(RadiusB, HalfB);
	return FMath::Abs(GetOwner()->GetActorLocation().Z - Target->GetActorLocation().Z) <= HalfA + HalfB + Range;
}

bool URTSAttackComponent::IsReady() const
{
	const UWorld* World = GetWorld();
	return World && !IsSwinging() && (World->GetTimeSeconds() - LastAttackTime) >= Interval;
}

bool URTSAttackComponent::IsSwinging() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(HitTimer);
}

bool URTSAttackComponent::TryStartAttack(AActor* Target)
{
	const URTSHealthComponent* OwnHealth = GetOwner()->FindComponentByClass<URTSHealthComponent>();
	if (!OwnHealth || OwnHealth->IsDead() || !IsReady() || !IsInRange(Target) || !HasLineOfSight(Target)) { return false; }
	UWorld* World = GetWorld();
	LastAttackTime = World->GetTimeSeconds();
	PendingTarget = Target;
	if (HitDelay <= 0.0f) { ResolveHit(); }
	else { World->GetTimerManager().SetTimer(HitTimer, this, &URTSAttackComponent::ResolveHit, HitDelay, false); }
	OnAttackStarted.Broadcast(Target);
	return true;
}

void URTSAttackComponent::CancelAttack()
{
	if (UWorld* World = GetWorld()) { World->GetTimerManager().ClearTimer(HitTimer); }
	PendingTarget.Reset();
}

void URTSAttackComponent::ResolveHit()
{
	AActor* Target = PendingTarget.Get();
	PendingTarget.Reset();
	const URTSHealthComponent* OwnHealth = GetOwner()->FindComponentByClass<URTSHealthComponent>();
	if (!OwnHealth || OwnHealth->IsDead() || !IsHostileTarget(GetOwner(), Target)
		|| EdgeDistance2D(GetOwner(), Target) > Range + HitRangeTolerance || !HasLineOfSight(Target)) { return; }
	if (bRanged)
	{
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ARTSProjectile* Projectile = GetWorld()->SpawnActor<ARTSProjectile>(AimLocation(GetOwner()), FRotator::ZeroRotator, Params))
		{
			Projectile->Initialize(GetOwner(), Target, Damage, ProjectileSpeed);
		}
	}
	else if (URTSHealthComponent* Health = Target->FindComponentByClass<URTSHealthComponent>())
	{
		Health->ApplyDamage(Damage, GetOwner());
	}
}

void URTSAttackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelAttack();
	Super::EndPlay(EndPlayReason);
}
