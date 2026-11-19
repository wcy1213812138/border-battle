#include "Orders/RTSLocalFormationPath.h"
#include "Orders/RTSReplanBudget.h"
#include "Units/RTSUnit.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"

namespace
{
	struct FDisc
	{
		FVector Center;
		float Radius;
	};
}

bool RTSLocalFormationPath::TryBeginPlanning(const ARTSUnit* Unit)
{
	static TMap<TWeakObjectPtr<UWorld>, FRTSReplanBudget> Budgets;
	if (!Unit || !Unit->GetWorld()) { return false; }
	for (auto It = Budgets.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) { It.RemoveCurrent(); }
	}
	return Budgets.FindOrAdd(TWeakObjectPtr<UWorld>(Unit->GetWorld())).TryBegin(Unit->GetUniqueID(), GFrameCounter);
}

bool RTSLocalFormationPath::Find(const ARTSUnit* Unit, const FVector& Goal, TArray<FVector>& OutWaypoints, const FRTSMoveGroup* EntryGroup)
{
	OutWaypoints.Reset();
	UWorld* World = Unit->GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav) { return false; }
	FNavLocation StartNav;
	const FVector Feet = Unit->GetActorLocation() - FVector(0, 0, Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	if (!Nav->ProjectPointToNavigation(Feet, StartNav, FVector(80, 80, 150))) { return false; }
	const FVector Start = StartNav.Location;
	const float Radius = Unit->GetSimpleCollisionRadius();
	TArray<FDisc> Discs;
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		if (*It == Unit || !It->IsAlive() || Unit->IsTrafficPassing(*It)) { continue; }
		// Moving neighbours are transient; wait/replan on blockage rather than freezing
		// their current positions into a permanent static route.
		if (It->GetCurrentOrder().Type != ERTSOrderType::None && It->GetVelocity().SizeSquared2D() > 625.0) { continue; }
		if (FVector::Dist2D(It->GetActorLocation(), Goal) > 1200.0) { continue; }
		Discs.Add({It->GetActorLocation(), Radius + It->GetSimpleCollisionRadius() + 3.0f});
	}
	FCollisionObjectQueryParams WallObjects;
	WallObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	WallObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams WallQuery(SCENE_QUERY_STAT(FormationWallClearance), false, Unit);
	const float HalfHeight = Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

	auto Clear = [&](const FVector& A, const FVector& B)
	{
		const FVector2D AB(B.X - A.X, B.Y - A.Y);
		if (EntryGroup)
		{
			// Route unclaimed units outside the parked formation, never through its
			// narrow internal gaps. Final-slot paths do not use this virtual obstacle.
			const FVector LocalA = A - EntryGroup->Center, LocalB = B - EntryGroup->Center;
			const double AX = FVector::DotProduct(LocalA, EntryGroup->Forward), AY = FVector::DotProduct(LocalA, EntryGroup->Right);
			const double BX = FVector::DotProduct(LocalB, EntryGroup->Forward), BY = FVector::DotProduct(LocalB, EntryGroup->Right);
			const double HX = EntryGroup->HalfDepth + Radius * 1.25, HY = EntryGroup->HalfWidth + Radius * 1.25;
			double Enter = 0.0, Exit = 1.0;
			auto Clip = [&](double Origin, double Delta, double Half)
			{
				if (FMath::Abs(Delta) < 0.01) { return FMath::Abs(Origin) < Half; }
				const double T1 = (-Half - Origin) / Delta, T2 = (Half - Origin) / Delta;
				Enter = FMath::Max(Enter, FMath::Min(T1, T2));
				Exit = FMath::Min(Exit, FMath::Max(T1, T2));
				return Enter <= Exit;
			};
			if (Clip(AX, BX - AX, HX) && Clip(AY, BY - AY, HY))
			{
				const bool bStartInside = FMath::Abs(AX) < HX && FMath::Abs(AY) < HY;
				const double DepthA = FMath::Min(HX - FMath::Abs(AX), HY - FMath::Abs(AY));
				const double DepthB = FMath::Min(HX - FMath::Abs(BX), HY - FMath::Abs(BY));
				if (!A.Equals(Start, 1.0) || !bStartInside || DepthB >= DepthA) { return false; }
			}
		}
		for (const FDisc& Disc : Discs)
		{
			const FVector2D AC(Disc.Center.X - A.X, Disc.Center.Y - A.Y);
			const double T = FMath::Clamp(FVector2D::DotProduct(AC, AB) / FMath::Max(AB.SizeSquared(), 0.01), 0.0, 1.0);
			if ((AC - AB * T).SizeSquared() < FMath::Square(Disc.Radius))
			{
				// At contact, discard only the extra planning margin, never the
				// physical capsule clearance. This permits a tangent/back-out step.
				const double PhysicalClearance = Disc.Radius - 3.0;
				if (A.Equals(Start, 1.0) && AC.SizeSquared() < FMath::Square(Disc.Radius)
					&& (AC - AB * T).SizeSquared() >= FMath::Min(AC.SizeSquared(), FMath::Square(PhysicalClearance)) - 0.01) { continue; }
				return false;
			}
		}
		FVector Hit;
		if (UNavigationSystemV1::NavigationRaycast(World, A, B, Hit, nullptr, Unit->GetController())) { return false; }
		// NavMesh may have been baked for a smaller agent. Keep the real capsule
		// clear of walls, while lifting the sweep slightly above the walkable floor.
		return !World->SweepTestByObjectType(A + FVector(0, 0, HalfHeight + 3), B + FVector(0, 0, HalfHeight + 3),
			FQuat::Identity, WallObjects, FCollisionShape::MakeCapsule(Radius + 2, HalfHeight - 2), WallQuery);
	};
	if (Clear(Start, Goal))
	{
		OutWaypoints.Add(Goal);
		return true;
	}

	// Sample inflated capsules finely enough to retain diagonal passages between parked rows.
	TArray<FVector> Nodes = {Start, Goal};
	// A unit wedged between parked rows may need to back out before reaching
	// any obstacle-ring vertex. All escape edges still use Clear's capsule checks.
	for (float Distance : {48.f, 100.f, 200.f, 350.f})
	{
		for (int32 Direction = 0; Direction < 16; ++Direction)
		{
			const double Angle = Direction * 2.0 * PI / 16;
			const FVector Candidate = Start + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * Distance;
			FNavLocation Projected;
			if (Nav->ProjectPointToNavigation(Candidate, Projected, FVector(10, 10, 60))
				&& FVector::Dist2D(Candidate, Projected.Location) < 10 && Clear(Start, Projected.Location)) { Nodes.Add(Projected.Location); }
		}
	}
	// Preserve NavMesh corner points around static and dynamic obstacles. Disc-only
	// nodes cannot route around a wall end when a stalled unit is also screened by
	// settled formation members.
	if (UNavigationPath* NavPath = Nav->FindPathToLocationSynchronously(World, Start, Goal))
	{
		if (NavPath->IsValid() && !NavPath->IsPartial())
		{
			for (const FNavPathPoint& Point : NavPath->PathPoints)
			{
				if (FVector::DistSquared2D(Point.Location, Start) > 4.0
					&& FVector::DistSquared2D(Point.Location, Goal) > 4.0)
				{
					Nodes.Add(Point.Location);
					for (int32 Direction = 0; Direction < 16; ++Direction)
					{
						const double Angle = Direction * 2.0 * PI / 16;
						const FVector Candidate = Point.Location + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (Radius + 12);
						FNavLocation Projected;
						if (Nav->ProjectPointToNavigation(Candidate, Projected, FVector(10, 10, 80))) { Nodes.Add(Projected.Location); }
					}
				}
			}
		}
	}
	if (EntryGroup)
	{
		const double HX = EntryGroup->HalfDepth + Radius * 1.25 + 20.0;
		const double HY = EntryGroup->HalfWidth + Radius * 1.25 + 20.0;
		for (int32 XSign : {-1, 1})
		{
			for (int32 YSign : {-1, 1})
			{
				FNavLocation Projected;
				const FVector Corner = EntryGroup->Center + EntryGroup->Forward * XSign * HX + EntryGroup->Right * YSign * HY;
				if (Nav->ProjectPointToNavigation(Corner, Projected, FVector(20, 20, 100))) { Nodes.Add(Projected.Location); }
			}
		}
	}
	constexpr int32 Sides = 24;
	for (const FDisc& Disc : Discs)
	{
		const double RingRadius = (Disc.Radius + 8.0) / FMath::Cos(PI / Sides);
		for (int32 i = 0; i < Sides; ++i)
		{
			const double Angle = 2.0 * PI * i / Sides;
			FVector Candidate(Disc.Center.X + FMath::Cos(Angle) * RingRadius,
				Disc.Center.Y + FMath::Sin(Angle) * RingRadius, Goal.Z);
			FNavLocation Projected;
			if (!Nav->ProjectPointToNavigation(Candidate, Projected, FVector(20, 20, 150))) { continue; }
			bool bFree = true;
			for (const FDisc& Other : Discs)
			{
				if (FVector::DistSquared2D(Projected.Location, Other.Center) < FMath::Square(Other.Radius + 1.0f))
				{
					bFree = false;
					break;
				}
			}
			if (bFree) { Nodes.Add(Projected.Location); }
		}
	}
	TArray<double> Distance;
	TArray<int32> Previous;
	TArray<bool> Visited;
	Distance.Init(TNumericLimits<double>::Max(), Nodes.Num());
	Previous.Init(INDEX_NONE, Nodes.Num());
	Visited.Init(false, Nodes.Num());
	Distance[0] = 0.0;
	for (int32 Iteration = 0; Iteration < Nodes.Num(); ++Iteration)
	{
		int32 Best = INDEX_NONE;
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			if (!Visited[i] && (Best == INDEX_NONE || Distance[i] < Distance[Best])) { Best = i; }
		}
		if (Best == INDEX_NONE || Distance[Best] == TNumericLimits<double>::Max()) { break; }
		if (Best == 1)
		{
			for (int32 Index = 1; Index != 0; Index = Previous[Index]) { OutWaypoints.Insert(Nodes[Index], 0); }
			return true;
		}
		Visited[Best] = true;
		for (int32 i = 1; i < Nodes.Num(); ++i)
		{
			if (Visited[i]) { continue; }
			const double Cost = Distance[Best] + FVector::Dist2D(Nodes[Best], Nodes[i]);
			if (Cost < Distance[i] && Clear(Nodes[Best], Nodes[i]))
			{
				Distance[i] = Cost;
				Previous[i] = Best;
			}
		}
	}
	return false;
}
