#pragma once
#include "Units/RTSUnit.h"

/** Measures visible parking before arrival, including units waiting to claim a slot. */
struct FRTSMovementContinuity
{
	bool Observe(const ARTSUnit* Unit, double Now)
	{
		const FRTSMoveGroup* Group = Unit->GetMoveGroup();
		const bool bWaiting = Group && FVector::Dist2D(Unit->GetActorLocation(), Group->Center) < 300
			&& Unit->GetCurrentOrder().Type == ERTSOrderType::Move && Unit->GetVelocity().SizeSquared2D() < 100;
		double& Since = WaitingSince.FindOrAdd(Unit->GetUniqueID(), -1.0);
		if (!bWaiting) { Since = -1; return true; }
		if (Since < 0) { Since = Now; }
		LongestPause = FMath::Max(LongestPause, Now - Since);
		return Now - Since <= 2.0;
	}
	double LongestPause = 0;
	TMap<uint32, double> WaitingSince;
};

struct FRTSFormationHandoff
{
	bool Observe(const TArray<ARTSUnit*>& Units)
	{
		if (bMeasured || Units.IsEmpty()) { return true; }
		const FRTSMoveGroup* Group = Units[0]->GetMoveGroup();
		if (!Group || Group->SlotUnitTypes.IsEmpty()) { return true; }
		const FName FrontType = Group->SlotUnitTypes[0];
		bool bFrontDone = true, bHasRear = false;
		for (ARTSUnit* Unit : Units)
		{
			if (Unit->GetFormationUnitType() != FrontType) { bHasRear = true; continue; }
			bFrontDone &= Unit->GetCurrentOrder().Type == ERTSOrderType::None && Unit->GetFormationSlotIndex() != INDEX_NONE;
		}
		if (!bFrontDone || !bHasRear) { return true; }
		bMeasured = true;
		for (ARTSUnit* Unit : Units)
		{
			if (Unit->GetFormationUnitType() == FrontType) { continue; }
			RearDistance = FMath::Max(RearDistance, FVector::Dist2D(Unit->GetActorLocation(), Group->Center));
		}
		if (RearDistance > 350)
		{
			for (ARTSUnit* Unit : Units) { UE_LOG(LogTemp, Display, TEXT("Handoff %s at=%s goal=%s slot=%d order=%d"), *Unit->GetName(), *Unit->GetActorLocation().ToCompactString(), *Unit->GetCurrentOrder().TargetLocation.ToCompactString(), Unit->GetFormationSlotIndex(), static_cast<int32>(Unit->GetCurrentOrder().Type)); }
		}
		return RearDistance <= 350;
	}
	bool bMeasured = false;
	double RearDistance = 0;
};
