#include "Audio/RTSAudioFeedbackComponent.h"
#include "Audio/RTSAudioSubsystem.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"

URTSAudioFeedbackComponent::URTSAudioFeedbackComponent()
{ PrimaryComponentTick.bCanEverTick = false; }
void URTSAudioFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();
	BoundHealth = GetOwner()->FindComponentByClass<URTSHealthComponent>();
	BoundAttack = GetOwner()->FindComponentByClass<URTSAttackComponent>();
	if (BoundHealth.IsValid())
	{
		BoundHealth->OnHealthChanged.AddDynamic(this, &URTSAudioFeedbackComponent::HealthChanged);
		BoundHealth->OnDied.AddDynamic(this, &URTSAudioFeedbackComponent::Died);
	}
	if (BoundAttack.IsValid()) { BoundAttack->OnAttackStarted.AddDynamic(this, &URTSAudioFeedbackComponent::AttackStarted); }
}
void URTSAudioFeedbackComponent::AttackStarted(AActor* Target)
{
	if (URTSAudioSubsystem* Audio = URTSAudioSubsystem::Get(this))
	{ Audio->PlayCue(BoundAttack.IsValid() && BoundAttack->IsRanged() ? ERTSAudioCue::Bow : ERTSAudioCue::Swing, GetOwner()); }
}
void URTSAudioFeedbackComponent::HealthChanged(URTSHealthComponent* Health, float NewHealth, float Delta)
{
	if (Delta < 0 && NewHealth > 0)
	{ if (URTSAudioSubsystem* Audio = URTSAudioSubsystem::Get(this)) { Audio->PlayCue(ERTSAudioCue::Hit, GetOwner()); } }
}
void URTSAudioFeedbackComponent::Died(URTSHealthComponent* Health, AActor* Killer)
{ if (URTSAudioSubsystem* Audio = URTSAudioSubsystem::Get(this)) { Audio->PlayCue(ERTSAudioCue::Death, GetOwner()); } }
void URTSAudioFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (BoundHealth.IsValid())
	{
		BoundHealth->OnHealthChanged.RemoveDynamic(this, &URTSAudioFeedbackComponent::HealthChanged);
		BoundHealth->OnDied.RemoveDynamic(this, &URTSAudioFeedbackComponent::Died);
	}
	if (BoundAttack.IsValid()) { BoundAttack->OnAttackStarted.RemoveDynamic(this, &URTSAudioFeedbackComponent::AttackStarted); }
	Super::EndPlay(Reason);
}
