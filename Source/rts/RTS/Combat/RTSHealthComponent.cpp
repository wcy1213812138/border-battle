#include "Combat/RTSHealthComponent.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Campaign/RTSCampaign.h"
#include "Combat/RTSAcquisitionComponent.h"

URTSHealthComponent::URTSHealthComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URTSHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	if (RegenerationRate > 0.0f)
	{
		GetWorld()->GetTimerManager().SetTimer(RegenerationTimer, this, &URTSHealthComponent::RegenerateHealth, 1.0f, true, 1.0f);
	}
}

void URTSHealthComponent::SetRegeneration(float HealthPerSecond, float DelaySeconds)
{
	RegenerationRate = FMath::Max(0.0f, HealthPerSecond);
	RegenerationDelay = FMath::Max(0.0f, DelaySeconds);
	if (!GetWorld()) { return; }
	GetWorld()->GetTimerManager().ClearTimer(RegenerationTimer);
	if (RegenerationRate > 0.0f && GetWorld()->HasBegunPlay())
	{
		GetWorld()->GetTimerManager().SetTimer(RegenerationTimer, this, &URTSHealthComponent::RegenerateHealth,
			1.0f, true, 1.0f);
	}
}

void URTSHealthComponent::RegenerateHealth()
{
	if (IsDead() || Health >= MaxHealth || !GetWorld()
		|| GetWorld()->GetTimeSeconds() - LastDamageTime < RegenerationDelay) { return; }
	const float Old = Health;
	Health = FMath::Min(MaxHealth, Health + RegenerationRate);
	if (Health > Old) { OnHealthChanged.Broadcast(this, Health, Health - Old); }
}

float URTSHealthComponent::ApplyDamage(float Amount, AActor* DamageCauser)
{
	if (Amount <= 0.0f || IsDead())
	{
		return 0.0f;
	}

	const float Old = Health;
	Health = FMath::Max(0.0f, Health - Amount);
	const float Delta = Health - Old;
	LastDamageSource = DamageCauser;
	LastDamageTime = GetWorld() ? GetWorld()->GetTimeSeconds() : -1000;

	OnHealthChanged.Broadcast(this, Health, Delta);
	if(Delta<0)if(auto* Campaign=ARTSCampaign::Get(this))Campaign->NotifyCivilianAttacked(GetOwner());
	if (Delta < 0) { URTSAcquisitionComponent::NotifyNearbyEnemyAllies(GetOwner(), DamageCauser); }

	if (IsDead())
	{
		OnDied.Broadcast(this, DamageCauser);
	}
	return -Delta;
}

void URTSHealthComponent::SetMaxHealth(float NewMaxHealth)
{
	MaxHealth = FMath::Max(1.0f, NewMaxHealth);
	ResetHealth();
}

void URTSHealthComponent::ResetHealth()
{
	const float Old = Health;
	Health = MaxHealth;
	LastDamageSource.Reset(); LastDamageTime = -1000;
	OnHealthChanged.Broadcast(this, Health, Health - Old);
}

float URTSHealthComponent::ApplyHealing(float Amount)
{
	if (!FMath::IsFinite(Amount) || Amount <= 0 || IsDead()) { return 0; }
	const float Old = Health;
	Health = FMath::Min(MaxHealth, Health + Amount);
	if (Health > Old) { OnHealthChanged.Broadcast(this, Health, Health - Old); }
	return Health - Old;
}

void URTSHealthComponent::SetMaxHealthPreservingRatio(float NewMaxHealth)
{
	if (!FMath::IsFinite(NewMaxHealth) || NewMaxHealth <= 0) { return; }
	const float Ratio = GetHealthPercent();
	const float Old = Health;
	MaxHealth = NewMaxHealth;
	Health = Ratio > 0 ? FMath::Clamp(Ratio * MaxHealth, 0.f, MaxHealth) : 0;
	OnHealthChanged.Broadcast(this, Health, Health - Old);
}
