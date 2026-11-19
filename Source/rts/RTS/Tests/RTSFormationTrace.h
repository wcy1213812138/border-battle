// Opt-in CSV trace of a group move: who stands where, what they target and which
// unit physically blocks the next centimetres of their lane. Never active in a
// normal run; enable with the console variable `rts.FormationTrace 1` (or
// `-rts.FormationTrace=1` when launching the automation command).
#pragma once

#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Units/RTSUnit.h"

namespace RTSFormationTrace
{
inline bool Enabled()
{
	// Never create the variable implicitly: the movement sandbox owns it.
	const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("rts.FormationTrace"));
	return CVar && CVar->GetInt() != 0;
}

/** One line per unit per sample, plus the shared formation gate state. */
struct FTrace
{
	bool bEnabled = false;
	bool bHeaderWritten = false;
	double NextSample = 0;
	double NextFlush = 0;
	TArray<FString> Lines;

	void Start()
	{
		bEnabled = RTSFormationTrace::Enabled();
		if (!bEnabled) { return; }
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("FormationTrace.csv"));
		IFileManager::Get().Delete(*Path, false, true, true);
		Lines.Reset();
		Lines.Add(TEXT("t,unit,type,lane,slot,row,order,px,py,pz,speed,goalx,goaly,blocked_by,lane_frontier,gates_open"));
		bHeaderWritten = true;
		Flush();
	}

	void Observe(const TArray<ARTSUnit*>& Units, double Now)
	{
		if (!bEnabled) { return; }
		if (Now < NextSample) { return; }
		NextSample = Now + 0.2;
		const FRTSMoveGroup* Group = Units.Num() ? Units[0]->GetMoveGroup() : nullptr;
		if (!Group) { return; }
		for (int32 Index = 0; Index < Units.Num(); ++Index)
		{
			const ARTSUnit* Unit = Units[Index];
			const int32 Slot = Unit->GetFormationSlotIndex();
			const FVector Location = Unit->GetActorLocation();
			// Direct blocker: closest other unit inside one capsule width ahead along the
			// formation's travel axis, which is the segment a follower cannot pass.
			FString Blocker = TEXT("-");
			double Closest = TNumericLimits<double>::Max();
			for (const ARTSUnit* Other : Units)
			{
				if (Other == Unit) { continue; }
				const FVector Delta = Other->GetActorLocation() - Location;
				const double Ahead = FVector::DotProduct(Delta, Group->Forward);
				const double Side = FMath::Abs(FVector::DotProduct(Delta, Group->Right));
				if (Ahead <= 0.0 || Side > 58.0) { continue; }
				const double Distance = Ahead + (Ahead > 58.0 ? 1000.0 : 0.0);
				if (Distance < Closest) { Closest = Distance; Blocker = Other->GetName(); }
			}
			Lines.Add(FString::Printf(TEXT("%.2f,%s,%s,%d,%d,%d,%d,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%s,%.1f,%d"),
				Now, *Unit->GetName(), *Unit->GetFormationUnitType().ToString(), Unit->GetFormationLane(),
				Slot, Group->SlotRow.IsValidIndex(Slot) ? Group->SlotRow[Slot] : INDEX_NONE,
				static_cast<int32>(Unit->GetCurrentOrder().Type),
				Location.X, Location.Y, Location.Z, Unit->GetVelocity().Size2D(),
				Unit->GetCurrentOrder().TargetLocation.X, Unit->GetCurrentOrder().TargetLocation.Y,
				*Blocker, Group->LaneFrontierOf(Unit->GetFormationLane()), Group->LaneGateCount()));
		}
		if (Now >= NextFlush) { NextFlush = Now + 0.5; Flush(); }
	}

	void Flush()
	{
		if (!bEnabled || Lines.IsEmpty()) { return; }
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Logs"), TEXT("FormationTrace.csv"));
		FFileHelper::SaveStringArrayToFile(Lines, *Path);
	}
};
}
