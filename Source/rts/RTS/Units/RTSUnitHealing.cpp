#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Combat/RTSHealFeedbackComponent.h"
#include "AIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"

void ARTSUnit::TickHealing(float DeltaSeconds)
{
	if (!UnitData || !UnitData->bHealer || !IsAlive()) { return; }
	const double Now = GetWorld()->GetTimeSeconds();
	if (CurrentOrder.Type != ERTSOrderType::None || bHealingStopped)
	{ HealingTarget.Reset(); HealResolveTime = -1; return; }
	auto ValidAlly = [&](ARTSUnit* Other)
	{
		return IsValid(Other) && Other != this && Other->IsAlive() && Other->GetTeam() == GetTeam()
			&& GetTeam() != RTSTeam::Neutral && Other->GetHealth()->GetHealth() < Other->GetHealth()->GetMaxHealth()
			&& FVector::DistSquared2D(GetActorLocation(), Other->GetActorLocation()) <= FMath::Square(900.f);
	};
	if (HealResolveTime >= 0)
	{
		ARTSUnit* Patient = HealingTarget.Get();
		if (!ValidAlly(Patient)) { StopAIMovement(); HealResolveTime = -1; HealingTarget.Reset(); return; }
		FaceTowards(Patient->GetActorLocation(), DeltaSeconds);
		if (Now < HealResolveTime) { return; }
		HealResolveTime = -1;
		if (URTSAttackComponent::EdgeDistance2D(this, Patient) <= UnitData->HealRange && Attack->HasLineOfSight(Patient))
		{
			if (Patient->GetHealth()->ApplyHealing(UnitData->HealAmount) > 0)
			{
				auto Feedback = [](ARTSUnit* Unit, bool bCaster)
				{
					URTSHealFeedbackComponent* FX = Unit->FindComponentByClass<URTSHealFeedbackComponent>();
					if (!FX) { FX = NewObject<URTSHealFeedbackComponent>(Unit); Unit->AddInstanceComponent(FX); FX->RegisterComponent(); }
					FX->Pulse(bCaster);
				};
				Feedback(Patient, false); Feedback(this, true);
			}
		}
		return;
	}
	if (Now < NextHealTime) { return; }
	ARTSUnit* Patient = HealingTarget.Get();
	if (!ValidAlly(Patient))
	{
		StopAIMovement();
		HealingTarget.Reset();
		float BestPercent = 1.f;
		for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
		{
			if (ValidAlly(*It) && It->GetHealth()->GetHealthPercent() < BestPercent)
			{ Patient = *It; BestPercent = It->GetHealth()->GetHealthPercent(); HealingTarget = Patient; }
		}
		if (!HealingTarget.IsValid()) { NextHealTime = Now + 0.25; return; }
	}
	if (URTSAttackComponent::EdgeDistance2D(this, Patient) > UnitData->HealRange || !Attack->HasLineOfSight(Patient))
	{
		if (AAIController* AI = Cast<AAIController>(GetController()))
		{
			AI->MoveToActor(Patient, FMath::Max(5.f, UnitData->HealRange * 0.65f), true, true, false, nullptr, false);
		}
		NextHealTime = Now + 0.5;
		return;
	}
	StopAIMovement();
	FaceTowards(Patient->GetActorLocation(), DeltaSeconds);
	HandleAttackStarted(Patient); // Uses the configured staff casting animation, not damage.
	HealResolveTime = Now + FMath::Max(0.05f, UnitData->HealCastDelay);
	NextHealTime = Now + FMath::Max(UnitData->HealInterval, UnitData->HealCastDelay + 0.05f);
}
