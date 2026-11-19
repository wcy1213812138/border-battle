#include "Combat/RTSAcquisitionComponent.h"
#include "Combat/RTSAttackComponent.h"
#include "Combat/RTSHealthComponent.h"
#include "Units/RTSUnit.h"
#include "Data/RTSUnitData.h"
#include "Orders/RTSOrderLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Buildings/RTSBuilding.h"
#include "Campaign/RTSCampaign.h"

URTSAcquisitionComponent::URTSAcquisitionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.25f;
}
void URTSAcquisitionComponent::NotifyNewOrder()
{
	AutomaticTarget.Reset(); bHasCombatOrigin = false; bSupportingAlly = false;
	NearbyCombatTarget.Reset(); NextNearbyScan = 0.0;
	if (GetWorld())
	{
		SuppressedUntil = GetWorld()->GetTimeSeconds() + 0.75;
		GuardDamageAfter = GetWorld()->GetTimeSeconds();
	}
}
void URTSAcquisitionComponent::SetStance(ERTSCombatStance InStance)
{
	if (InStance != ERTSCombatStance::Guard && InStance != ERTSCombatStance::Patrol && InStance != ERTSCombatStance::Aggressive) { return; }
	if (Stance == InStance) { return; }
	ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	if (Unit && AutomaticTarget.IsValid() && Unit->GetCurrentOrder().Type == ERTSOrderType::Attack
		&& Unit->GetCurrentOrder().TargetActor == AutomaticTarget) { URTSOrderLibrary::IssueStopOrder({Unit}); }
	Stance = InStance;
	NotifyNewOrder();
}

bool URTSAcquisitionComponent::IsSupportingAlly() const
{
	const auto* Unit = Cast<ARTSUnit>(GetOwner());
	return bSupportingAlly && Unit && Unit->IsAlive() && Unit->GetCurrentOrder().Type == ERTSOrderType::Attack
		&& Unit->GetCurrentOrder().TargetActor == AutomaticTarget && URTSAttackComponent::IsHostileTarget(Unit, AutomaticTarget.Get());
}

void URTSAcquisitionComponent::NotifyNearbyEnemyAllies(AActor* Victim, AActor* Attacker)
{
	if (!IsValid(Victim) || !Victim->GetWorld() || Victim->ActorHasTag(TEXT("RTS_CampaignRestoreStaged")) || !URTSAttackComponent::IsHostileTarget(Victim, Attacker)) { return; }
	auto* VictimUnit = Cast<ARTSUnit>(Victim);
	const auto* VictimBuilding = Cast<ARTSBuilding>(Victim);
	if ((!VictimUnit || VictimUnit->GetTeam() != 1) && (!VictimBuilding || VictimBuilding->GetTeam() != 1)) { return; }
	if (FVector::DistSquared2D(Victim->GetActorLocation(), Attacker->GetActorLocation()) > FMath::Square(2400.f)) { return; }
	const auto* Campaign = ARTSCampaign::Get(Victim);
	const int32 Faction = Campaign ? (VictimUnit ? Campaign->GetUnitFaction(VictimUnit) : Campaign->GetActorFaction(Victim)) : -1;
	if (Campaign && Faction < 0) { return; }
	for (TActorIterator<ARTSUnit> It(Victim->GetWorld()); It; ++It)
	{
		if (Campaign && Campaign->GetUnitFaction(*It) != Faction) { continue; }
		if (auto* Acquisition = It->FindComponentByClass<URTSAcquisitionComponent>()) { Acquisition->RespondToAlly(Victim, Attacker); }
	}
}

bool URTSAcquisitionComponent::RespondToAlly(AActor* Victim, AActor* Attacker)
{
	auto* Unit = Cast<ARTSUnit>(GetOwner());
	const auto* Data = Unit ? Unit->GetUnitData() : nullptr;
	if (!Unit || !Unit->IsAlive() || Unit->GetTeam() != 1 || !Data || !Data->bAutoAcquireTargets
		|| Data->bWorker || Data->bHealer || Data->AttackDamage <= 0 || Unit->GetHealth()->GetHealthPercent() < .3f
		|| Unit->ActorHasTag(TEXT("RTS_CampaignRestoreStaged")) || Unit->IsYieldingToTraffic() || Unit->IsInTrafficPass()
		|| !URTSAttackComponent::IsHostileTarget(Unit, Attacker)) { return false; }
	const auto Type = Unit->GetCurrentOrder().Type;
	// Do not steal an existing fight, gather/build job, march or retreat.
	if (Type != ERTSOrderType::None && Type != ERTSOrderType::Stop) { return false; }
	if (FVector::DistSquared2D(Unit->GetActorLocation(), Victim->GetActorLocation()) > FMath::Square(1800.f)
		|| FVector::DistSquared2D(Unit->GetActorLocation(), Attacker->GetActorLocation()) > FMath::Square(2400.f)) { return false; }
	const FVector Origin = Unit->GetActorLocation();
	// Rear-rank helpers need not see through their own formation to hear the alarm.
	// Ordinary Attack still enforces navigation, attack range and line of sight.
	URTSOrderLibrary::IssueAttackOrder({Unit}, Attacker);
	if (Unit->GetCurrentOrder().Type != ERTSOrderType::Attack || Unit->GetCurrentOrder().TargetActor.Get() != Attacker) { return false; }
	AutomaticTarget = Attacker; CombatOrigin = Origin; bHasCombatOrigin = true; bSupportingAlly = true;
	return true;
}

AActor* URTSAcquisitionComponent::ResolveAttackTarget(AActor* OrderedTarget)
{
	auto* Unit = Cast<ARTSUnit>(GetOwner());
	const auto* Data = Unit ? Unit->GetUnitData() : nullptr;
	if (!Unit || !Data || !Data->bAutoAcquireTargets || Data->bWorker || Data->bHealer
		|| Stance == ERTSCombatStance::Guard || Unit->GetCurrentOrder().Type != ERTSOrderType::Attack
		|| AutomaticTarget.IsValid() || Unit->IsYieldingToTraffic() || Unit->IsInTrafficPass())
	{ NearbyCombatTarget.Reset(); return OrderedTarget; }
	auto* Attack = Unit->GetAttack();
	const double Now = GetWorld()->GetTimeSeconds();
	const float Radius = FMath::Min(FMath::Max(0.f, Data->AcquisitionRadius)
		* (Stance == ERTSCombatStance::Aggressive ? 1.8f : 1.f), FMath::Max(350.f, Attack->GetRange() + 150.f));
	const float Leash = FMath::Min(FMath::Max(0.f, Data->AutoChaseLeash), Radius + 200.f);
	// Keep an already-started hit intact and retain one local target instead of
	// switching every frame as neighbours move. A fleeing/occluded target is released.
	if (auto* Local = NearbyCombatTarget.Get())
	{
		if (Attack->IsSwinging() || (Now < NearbyCombatUntil && URTSAttackComponent::IsHostileTarget(Unit, Local)
			&& FVector::DistSquared2D(Local->GetActorLocation(), NearbyCombatOrigin) <= FMath::Square(Leash)
			&& FVector::DistSquared2D(Unit->GetActorLocation(), NearbyCombatOrigin) <= FMath::Square(Leash)
			&& Attack->HasLineOfSight(Local))) { return Local; }
		NearbyCombatTarget.Reset(); NextNearbyScan = Now + 2;
	}
	if (Attack->IsSwinging() || Now < NextNearbyScan) { return OrderedTarget; }
	NextNearbyScan = Now + .25;
	// A focus-fired unit already within reach stays the priority. Buildings may
	// be interrupted by their nearby defenders; the building order is not lost.
	const bool bOrderedInRange = Attack->IsInRange(OrderedTarget) && Attack->HasLineOfSight(OrderedTarget);
	if (bOrderedInRange && !Cast<ARTSBuilding>(OrderedTarget)) { return OrderedTarget; }
	ARTSUnit* Best = nullptr; double BestDistance = FMath::Square(Radius);
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (*It == OrderedTarget || !URTSAttackComponent::IsHostileTarget(Unit, *It) || !Attack->HasLineOfSight(*It)) { continue; }
		const double Distance = FVector::DistSquared2D(Unit->GetActorLocation(), It->GetActorLocation());
		if (Distance >= BestDistance || (bOrderedInRange && !Attack->IsInRange(*It))) { continue; }
		Best = *It; BestDistance = Distance;
	}
	if (Best) { NearbyCombatTarget = Best; NearbyCombatOrigin = Unit->GetActorLocation(); NearbyCombatUntil = Now + 8; return Best; }
	return OrderedTarget;
}

void URTSAcquisitionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ARTSUnit* Unit = Cast<ARTSUnit>(GetOwner());
	const URTSUnitData* Data = Unit ? Unit->GetUnitData() : nullptr;
	if (!Unit || !Unit->IsAlive() || Unit->IsYieldingToTraffic() || Unit->IsInTrafficPass() || !Data || !Data->bAutoAcquireTargets) { return; }
	const FRTSOrder& Order = Unit->GetCurrentOrder();
	if (bSupportingAlly && !IsSupportingAlly()) { bSupportingAlly = false; AutomaticTarget.Reset(); bHasCombatOrigin = false; }
	const float Leash = bSupportingAlly ? 2400.f : FMath::Max(0.f, Data->AutoChaseLeash) * (Stance == ERTSCombatStance::Aggressive ? 2.f : 1.f);
	if (Order.Type == ERTSOrderType::Attack && AutomaticTarget.IsValid() && Order.TargetActor == AutomaticTarget)
	{
		const bool bStopGuard = Stance == ERTSCombatStance::Guard && !bSupportingAlly
			&& (!Unit->GetAttack()->IsInRange(AutomaticTarget.Get()) || !Unit->GetAttack()->HasLineOfSight(AutomaticTarget.Get()));
		if (bStopGuard || (bSupportingAlly && Unit->GetHealth()->GetHealthPercent() < .3f) || FVector::DistSquared2D(Unit->GetActorLocation(), CombatOrigin) > FMath::Square(Leash)
			|| FVector::DistSquared2D(AutomaticTarget->GetActorLocation(), CombatOrigin) > FMath::Square(Leash))
		{
			const FVector Origin = CombatOrigin;
			URTSOrderLibrary::IssueStopOrder({Unit});
			CombatOrigin = Origin; bHasCombatOrigin = true;
		}
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const bool bMarching = Order.Type == ERTSOrderType::AttackMove;
	if ((Order.Type != ERTSOrderType::None && !bMarching) || Now < SuppressedUntil) { return; }
	AActor* Best = nullptr;
	if (Stance == ERTSCombatStance::Guard)
	{
		const URTSHealthComponent* Health = Unit->GetHealth();
		AActor* Source = Health->GetLastDamageSource();
		if (Health->GetLastDamageTime() > GuardDamageAfter && Now - Health->GetLastDamageTime() < 4
			&& URTSAttackComponent::IsHostileTarget(Unit, Source) && Unit->GetAttack()->IsInRange(Source)
			&& Unit->GetAttack()->HasLineOfSight(Source)) { Best = Source; }
	}
	else
	{
		double BestDistance = FMath::Square(FMath::Max(0.f, Data->AcquisitionRadius) * (Stance == ERTSCombatStance::Aggressive ? 1.8f : 1.f));
		for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
		{
			const double Distance = FVector::DistSquared(Unit->GetActorLocation(), It->GetActorLocation());
			if (bHasCombatOrigin && (FVector::DistSquared2D(It->GetActorLocation(), CombatOrigin) > FMath::Square(Leash)
				|| FVector::DistSquared2D(Unit->GetActorLocation(), CombatOrigin) > FMath::Square(Leash))) { continue; }
			if (Distance >= BestDistance || !URTSAttackComponent::IsHostileTarget(Unit, *It) || !Unit->GetAttack()->HasLineOfSight(*It)) { continue; }
			Best = *It; BestDistance = Distance;
		}
		// Attack-move must also engage hostile buildings, not march past an undefended base.
		for(TActorIterator<ARTSBuilding> It(GetWorld());It;++It)
		{
			const double Distance=FVector::DistSquared(Unit->GetActorLocation(),It->GetActorLocation());
			if(bHasCombatOrigin && FVector::DistSquared2D(It->GetActorLocation(),CombatOrigin)>FMath::Square(Leash))continue;
			if(Distance>=BestDistance || !URTSAttackComponent::IsHostileTarget(Unit,*It) || !Unit->GetAttack()->HasLineOfSight(*It))continue;
			Best=*It;BestDistance=Distance;
		}
	}
	if (Best)
	{
		const FVector Origin = bHasCombatOrigin ? CombatOrigin : Unit->GetActorLocation();
		// Attack-move: remember the march destination so the fight cannot cancel the order.
		if (bMarching) { Unit->SetAttackMoveGoal(Order.TargetLocation); }
		URTSOrderLibrary::IssueAttackOrder({Unit}, Best);
		AutomaticTarget = Best; CombatOrigin = Origin; bHasCombatOrigin = true;
	}
}
