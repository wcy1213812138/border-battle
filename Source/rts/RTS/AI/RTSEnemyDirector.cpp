#include "AI/RTSEnemyDirector.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSAttackComponent.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Orders/RTSOrderLibrary.h"
#include "Units/RTSUnit.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NavigationSystem.h"

struct FRTSAIWorldState
{
	TArray<ARTSUnit*> Workers;
	TArray<ARTSUnit*> Army;
	TArray<ARTSUnit*> Enemies;
	TArray<ARTSBuilding*> Buildings;
	TArray<ARTSBuilding*> AllBuildings;
	TArray<ARTSBuilding*> EnemyBuildings;
	TArray<ARTSResourceNode*> Nodes;
};

namespace
{
	constexpr uint8 Team = 1;
	constexpr float DefenceRadius = 1400.0f;
	constexpr float SightRadius = 1100.0f;
	constexpr float AssignmentCooldown = 8.0f;

	bool UsableNode(const ARTSResourceNode* Node)
	{
		return IsValid(Node) && (Node->GetRemaining() > 0 || Node->IsRenewable()) && Node->CanGather(Team);
	}

	// A depleted node with cargo is still a valid return trip, not an idle worker.
	bool IdleWorker(const ARTSUnit* Unit)
	{
		const FRTSOrder& Order = Unit->GetCurrentOrder();
		if (Order.Type == ERTSOrderType::None || Order.Type == ERTSOrderType::Stop) { return true; }
		if (Order.Type == ERTSOrderType::Build)
		{
			const ARTSBuilding* Site = Cast<ARTSBuilding>(Order.TargetActor.Get());
			return !IsValid(Site) || !Site->IsAlive() || Site->IsComplete();
		}
		if (Order.Type == ERTSOrderType::Gather)
		{
			return !UsableNode(Cast<ARTSResourceNode>(Order.TargetActor.Get()))
				&& (!Unit->GetGatherer() || Unit->GetGatherer()->GetCarriedAmount() == 0);
		}
		if (Order.Type == ERTSOrderType::ReturnResources)
		{
			return !Unit->GetGatherer() || Unit->GetGatherer()->GetCarriedAmount() == 0;
		}
		return Order.Type == ERTSOrderType::Attack
			&& !URTSAttackComponent::IsHostileTarget(Unit, Order.TargetActor.Get());
	}

	bool VisibleTo(const ARTSUnit* Unit, const AActor* Target, float Radius = SightRadius)
	{
		return Unit->GetAttack() && URTSAttackComponent::IsHostileTarget(Unit, Target)
			&& FVector::DistSquared2D(Unit->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(Radius)
			&& Unit->GetAttack()->HasLineOfSight(Target);
	}

	bool ValidAttack(const ARTSUnit* Unit)
	{
		const FRTSOrder& Order = Unit->GetCurrentOrder();
		return Order.Type == ERTSOrderType::Attack
			&& URTSAttackComponent::IsHostileTarget(Unit, Order.TargetActor.Get());
	}

	void AttackIfChanged(ARTSUnit* Unit, AActor* Target)
	{
		if (!URTSAttackComponent::IsHostileTarget(Unit, Target)) { return; }
		const FRTSOrder& Order = Unit->GetCurrentOrder();
		if (Order.Type == ERTSOrderType::Attack && Order.TargetActor.Get() == Target) { return; }
		// Finish an existing wind-up before switching even to a different target.
		if (Unit->GetAttack() && Unit->GetAttack()->IsSwinging() && ValidAttack(Unit)) { return; }
		URTSOrderLibrary::IssueAttackOrder({Unit}, Target);
	}

	ARTSUnit* NearestEnemy(ARTSUnit* Unit, const TArray<ARTSUnit*>& Enemies, const FVector* Home = nullptr)
	{
		ARTSUnit* Best = nullptr;
		double BestDistance = FMath::Square(SightRadius);
		for (ARTSUnit* Enemy : Enemies)
		{
			if (Home && FVector::DistSquared2D(Enemy->GetActorLocation(), *Home) > FMath::Square(DefenceRadius)) { continue; }
			const double Distance = FVector::DistSquared2D(Unit->GetActorLocation(), Enemy->GetActorLocation());
			if (Distance < BestDistance && VisibleTo(Unit, Enemy)) { Best = Enemy; BestDistance = Distance; }
		}
		return Best;
	}
}

ARTSEnemyDirector::ARTSEnemyDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetActorEnableCollision(false);
}

bool ARTSEnemyDirector::LoadDifficulty(ERTSDifficulty Difficulty, FRTSAIDifficulty& OutParameters, FString& Error)
{
	Error.Reset();
	if (Difficulty != ERTSDifficulty::Easy && Difficulty != ERTSDifficulty::Normal && Difficulty != ERTSDifficulty::Hard)
	{
		Error = TEXT("未知的电脑难度。");
		return false;
	}
	FString CSV;
	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("RTS/Data/Balance/RTSAIDifficulty.csv"));
	if (!FFileHelper::LoadFileToString(CSV, *Path))
	{
		Error = FString::Printf(TEXT("无法读取电脑难度表：%s"), *Path);
		return false;
	}
	// Deliberately strict, simple CSV: eight unquoted fields; no hidden defaults.
	TArray<FString> Lines;
	CSV.ParseIntoArrayLines(Lines, true);
	if (Lines.Num() != 4 || Lines[0].TrimStartAndEnd() != TEXT("Name,DecisionInterval,WorkerTarget,ArmyTarget,AttackSize,FirstAttackSeconds,AttackInterval,ArcherFraction"))
	{
		Error = TEXT("电脑难度表必须包含规定的表头和 Easy/Normal/Hard 三行。");
		return false;
	}
	FRTSAIDifficulty Candidates[3];
	bool Seen[3] = {false, false, false};
	for (int32 Line = 1; Line < Lines.Num(); ++Line)
	{
		TArray<FString> Fields;
		Lines[Line].ParseIntoArray(Fields, TEXT(","), false);
		if (Fields.Num() != 8)
		{
			Error = FString::Printf(TEXT("电脑难度表第%d行字段数错误。"), Line + 1);
			return false;
		}
		for (FString& Field : Fields) { Field.TrimStartAndEndInline(); }
		const int32 Index = Fields[0] == TEXT("Easy") ? 0 : Fields[0] == TEXT("Normal") ? 1 : Fields[0] == TEXT("Hard") ? 2 : INDEX_NONE;
		if (Index == INDEX_NONE || Seen[Index])
		{
			Error = TEXT("电脑难度表有未知或重复的难度名称。");
			return false;
		}
		FRTSAIDifficulty& P = Candidates[Index];
		const bool bParsed = FDefaultValueHelper::ParseFloat(Fields[1], P.DecisionInterval)
			&& FDefaultValueHelper::ParseInt(Fields[2], P.WorkerTarget)
			&& FDefaultValueHelper::ParseInt(Fields[3], P.ArmyTarget)
			&& FDefaultValueHelper::ParseInt(Fields[4], P.AttackSize)
			&& FDefaultValueHelper::ParseFloat(Fields[5], P.FirstAttackSeconds)
			&& FDefaultValueHelper::ParseFloat(Fields[6], P.AttackInterval)
			&& FDefaultValueHelper::ParseFloat(Fields[7], P.ArcherFraction);
		if (!bParsed || !FMath::IsFinite(P.DecisionInterval) || !FMath::IsFinite(P.FirstAttackSeconds)
			|| !FMath::IsFinite(P.AttackInterval) || !FMath::IsFinite(P.ArcherFraction)
			|| P.DecisionInterval < 0.25f || P.DecisionInterval > 30.0f
			|| P.WorkerTarget < 2 || P.WorkerTarget > 32 || P.ArmyTarget < 2 || P.ArmyTarget > 64
			|| P.AttackSize < 2 || P.AttackSize > P.ArmyTarget
			|| P.FirstAttackSeconds < 0.0f || P.FirstAttackSeconds > 3600.0f
			|| P.AttackInterval < 10.0f || P.AttackInterval > 1800.0f
			|| P.ArcherFraction < 0.0f || P.ArcherFraction > 1.0f)
		{
			Error = FString::Printf(TEXT("电脑难度表第%d行含有非法数值或兵力阈值。"), Line + 1);
			return false;
		}
		Seen[Index] = true;
	}
	const int32 Selected = Difficulty == ERTSDifficulty::Easy ? 0 : Difficulty == ERTSDifficulty::Normal ? 1 : 2;
	OutParameters = Candidates[Selected]; // Rejected files never modify the caller's parameters.
	return true;
}

void ARTSEnemyDirector::Configure(ERTSDifficulty InDifficulty)
{
	FString Error;
	bConfigured = LoadDifficulty(InDifficulty, Parameters, Error);
	DecisionTimer = 0.0f;
	NextAttackSeconds = Parameters.FirstAttackSeconds;
	NextPlacementSeconds = 0.0f;
	Wave.Reset();
	Defenders.Reset();
	LastWorkAssignment.Reset();
	Intent = bConfigured ? TEXT("准备发展经济") : Error;
	PrimaryActorTick.bStartWithTickEnabled = bConfigured; // Also supports deferred-spawn Configure.
	SetActorTickEnabled(bConfigured);
	if (!bConfigured) { UE_LOG(LogRTS, Error, TEXT("电脑AI配置失败：%s"), *Error); }
}

void ARTSEnemyDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bConfigured) { return; }
	URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this);
	if (!Match) { return; }
	const ERTSMatchPhase Phase = Match->GetPhase();
	if (Phase == ERTSMatchPhase::Victory || Phase == ERTSMatchPhase::Defeat || Phase == ERTSMatchPhase::Draw)
	{
		Intent = TEXT("比赛结束，停止决策");
		SetActorTickEnabled(false); // Never changes unit/producer/combat component ticks.
		return;
	}
	if (Phase != ERTSMatchPhase::Playing) { return; }
	DecisionTimer -= FMath::Max(0.0f, DeltaSeconds);
	if (DecisionTimer > 0.0f) { return; }
	DecisionTimer = Parameters.DecisionInterval;
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
	if (!Balance) { Intent = TEXT("等待经济系统"); return; }
	FRTSAIWorldState State;
	CollectWorld(State);
	for (auto It = LastWorkAssignment.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid() || !It.Key()->IsAlive()) { It.RemoveCurrent(); }
	}
	RunEconomy(State, *Balance, *Match);
	RunMilitary(State, *Match);
}

void ARTSEnemyDirector::CollectWorld(FRTSAIWorldState& State) const
{
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ARTSUnit* Unit = *It;
		if (!Unit->IsAlive() || !Unit->GetUnitData()) { continue; }
		if (Unit->GetTeam() == Team)
		{
			(Unit->GetUnitData()->bWorker ? State.Workers : State.Army).Add(Unit);
		}
		else if (Unit->GetTeam() != RTSTeam::Neutral) { State.Enemies.Add(Unit); }
	}
	for (TActorIterator<ARTSBuilding> It(GetWorld()); It; ++It)
	{
		if (!It->IsAlive()) { continue; }
		State.AllBuildings.Add(*It);
		if (It->GetTeam() == Team) { State.Buildings.Add(*It); }
		else if (It->GetTeam() != RTSTeam::Neutral) { State.EnemyBuildings.Add(*It); }
	}
	for (TActorIterator<ARTSResourceNode> It(GetWorld()); It; ++It)
	{
		if (UsableNode(*It)) { State.Nodes.Add(*It); }
	}
}

ARTSBuilding* ARTSEnemyDirector::TryBuild(FName Id, const FVector& Home, const FRTSAIWorldState& State, URTSBalanceSubsystem& Balance)
{
	const FRTSBalanceRow* Row = Balance.Find(Id);
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Row || !Nav) { return nullptr; }
	const double Clearance = Row->Footprint * 0.71 + 150.0;
	// Bounded ring search; CanPlace checks flat NavMesh coverage and collisions.
	for (int32 Ring = 0; Ring < 4; ++Ring)
	{
		for (int32 Slot = 0; Slot < 16; ++Slot)
		{
			const double Angle = (Slot + (Ring % 2) * 0.5) * 2.0 * PI / 16.0;
			const FVector Candidate = Home + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * (550.0 + Ring * 260.0);
			FNavLocation Projected;
			if (!Nav->ProjectPointToNavigation(Candidate, Projected, FVector(50, 50, 150))
				|| FMath::Abs(Projected.Location.Z - Home.Z) > 100.0) { continue; }
			const FVector Ground = Projected.Location;
			bool bClear = true;
			for (const ARTSBuilding* Building : State.AllBuildings)
			{
				if (FVector::Dist2D(Ground, Building->GetActorLocation()) < Clearance + Building->GetFootprint() * 0.71)
				{ bClear = false; break; }
			}
			if (!bClear) { continue; }
			// Keep both a resource apron and a corridor from the hall to each local node.
			for (TActorIterator<ARTSResourceNode> It(GetWorld()); It; ++It)
			{
				if (It->GetRemaining() <= 0 && !It->IsRenewable()) { continue; }
				const FVector Node = It->GetActorLocation();
				if (FVector::Dist2D(Ground, Node) < Clearance + 100.0)
				{ bClear = false; break; }
				if (FVector::DistSquared2D(Home, Node) < FMath::Square(2200.0))
				{
					const FVector Closest = FMath::ClosestPointOnSegment(FVector(Ground.X, Ground.Y, 0),
						FVector(Home.X, Home.Y, 0), FVector(Node.X, Node.Y, 0));
					if (FVector::Dist2D(Ground, Closest) < Clearance) { bClear = false; break; }
				}
			}
			FString Error;
			if (bClear && Balance.CanPlace(Id, Ground, Error))
			{
				if (ARTSBuilding* Site = Balance.PlaceBuilding(Id, Team, Ground, Error)) { return Site; }
			}
		}
	}
	return nullptr;
}

void ARTSEnemyDirector::RunEconomy(FRTSAIWorldState& State, URTSBalanceSubsystem& Balance, URTSMatchSubsystem& Match)
{
	URTSResourceComponent* Stock = Balance.GetResources(Team);
	if (!Stock) { Intent = TEXT("等待本队库存"); return; }
	const FVector Home = Match.GetBaseLocation(Team);
	const float Now = Match.GetElapsedSeconds();
	ARTSBuilding* Hall = Match.GetMainBase(Team);
	TArray<ARTSBuilding*> Sites;
	TArray<ARTSBuilding*> Barracks;
	int32 Farms = 0, Towers = 0, QueuedWorkers = 0, QueuedArmy = 0, Archers = 0;
	for (ARTSUnit* Unit : State.Army) { if (Unit->GetUnitData()->BalanceId == TEXT("Archer")) { ++Archers; } }
	for (ARTSBuilding* Building : State.Buildings)
	{
		if (!Building->IsComplete()) { Sites.Add(Building); }
		if (Building->GetBuildingId() == TEXT("Farm")) { ++Farms; }
		if (Building->GetBuildingId() == TEXT("Tower")) { ++Towers; }
		if (Building->GetBuildingId() == TEXT("Barracks")) { Barracks.Add(Building); }
		if (URTSProducerComponent* Producer = Building->GetProducer())
		{
			for (FName Id : Producer->GetQueue())
			{
				if (Id == TEXT("Peasant")) { ++QueuedWorkers; }
				else { ++QueuedArmy; if (Id == TEXT("Archer")) { ++Archers; } }
			}
		}
	}
	const FRTSBalanceRow* WorkerRow = Balance.Find(TEXT("Peasant"));
	const FRTSBalanceRow* FarmRow = Balance.Find(TEXT("Farm"));
	const bool bNeedWorkers = State.Workers.Num() + QueuedWorkers < Parameters.WorkerTarget;
	int64 LocalFood = 0;
	for (ARTSResourceNode* Node : State.Nodes)
	{
		if (Node->GetResourceType() == ERTSResource::Food && FVector::DistSquared2D(Home, Node->GetActorLocation()) < FMath::Square(1800.0))
		{ LocalFood += Node->GetRemaining(); }
	}
	const int32 Food = Stock->GetAmount(ERTSResource::Food);
	const bool bNeedFarm = Farms < 3 && (LocalFood < Parameters.WorkerTarget * 60 || (Farms == 0 && Food < 150));
	const int32 ReserveWood = bNeedWorkers && QueuedWorkers == 0 && WorkerRow ? WorkerRow->WoodCost : 0;
	const int32 ReserveFood = bNeedWorkers && QueuedWorkers == 0 && WorkerRow ? WorkerRow->FoodCost : 0;
	auto CanSpend = [&](FName Id, int32 KeepWood, int32 KeepFood)
	{
		const FRTSBalanceRow* Cost = Balance.Find(Id);
		return Cost && Stock->GetAmount(ERTSResource::Wood) >= Cost->WoodCost + KeepWood
			&& Stock->GetAmount(ERTSResource::Food) >= Cost->FoodCost + KeepFood;
	};

	// A single paid worker order at a time, including all halls in the census.
	if (bNeedWorkers && QueuedWorkers == 0 && IsValid(Hall) && Hall->IsAlive() && Hall->IsComplete()
		&& Hall->GetProducer() && Hall->GetProducer()->GetQueue().IsEmpty() && CanSpend(TEXT("Peasant"), 0, 0))
	{
		FString Error;
		if (Hall->GetProducer()->Enqueue(TEXT("Peasant"), Error)) { ++QueuedWorkers; }
	}

	TArray<ARTSUnit*> Idle;
	for (ARTSUnit* Worker : State.Workers)
	{
		if (IdleWorker(Worker) && (!Worker->GetGatherer() || Worker->GetGatherer()->GetCarriedAmount() == 0)) { Idle.Add(Worker); }
	}
	auto AssignBuilders = [&](ARTSBuilding* Site, int32 Wanted)
	{
		Idle.Sort([Site](const ARTSUnit& A, const ARTSUnit& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), Site->GetActorLocation())
				< FVector::DistSquared2D(B.GetActorLocation(), Site->GetActorLocation());
		});
		const int32 Count = FMath::Min(Wanted, Idle.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			ARTSUnit* Worker = Idle[0];
			Idle.RemoveAt(0);
			URTSOrderLibrary::IssueWorkOrder({Worker}, Site);
			LastWorkAssignment.Add(Worker, Now);
		}
	};
	for (ARTSBuilding* Site : Sites)
	{
		int32 Builders = 0;
		for (ARTSUnit* Worker : State.Workers)
		{
			const FRTSOrder& Order = Worker->GetCurrentOrder();
			if (Order.Type == ERTSOrderType::Build && Order.TargetActor.Get() == Site) { ++Builders; }
		}
		if (Builders == 0) { AssignBuilders(Site, State.Workers.Num() >= 6 ? 2 : 1); }
	}
	// Only one outstanding site; food recovery takes priority over military expansion.
	FName Plan = NAME_None;
	if (bNeedFarm) { Plan = TEXT("Farm"); }
	else if (Barracks.IsEmpty()) { Plan = TEXT("Barracks"); }
	else if (Towers == 0 && Farms > 0 && State.Army.Num() >= Parameters.AttackSize) { Plan = TEXT("Tower"); }
	if (Sites.IsEmpty() && !Plan.IsNone() && !Idle.IsEmpty() && Now >= NextPlacementSeconds
		&& CanSpend(Plan, ReserveWood, ReserveFood))
	{
		NextPlacementSeconds = Now + 5.0f; // Bounded searches, not every frame on a blocked map.
		if (ARTSBuilding* Site = TryBuild(Plan, Home, State, Balance))
		{
			State.Buildings.Add(Site);
			State.AllBuildings.Add(Site);
			Sites.Add(Site);
			AssignBuilders(Site, State.Workers.Num() >= 6 ? 2 : 1);
			if (Plan == TEXT("Farm")) { ++Farms; }
		}
	}

	// Keep an idle reserve for expansion and replacement of a killed builder.
	// Valid Gather/Return/Build orders are never interrupted to recruit one.
	ARTSUnit* Volunteer = nullptr;
	if (State.Workers.Num() >= 3 && !Idle.IsEmpty())
	{
		Idle.Sort([Home](const ARTSUnit& A, const ARTSUnit& B)
		{
			return FVector::DistSquared2D(A.GetActorLocation(), Home) < FVector::DistSquared2D(B.GetActorLocation(), Home);
		});
		Volunteer = Idle[0];
	}
	int32 WoodWorkers = 0, FoodWorkers = 0;
	for (ARTSUnit* Worker : State.Workers)
	{
		const FRTSOrder& Order = Worker->GetCurrentOrder();
		if (Order.Type == ERTSOrderType::Gather)
		{
			const ARTSResourceNode* Node = Cast<ARTSResourceNode>(Order.TargetActor.Get());
			if (UsableNode(Node)) { (Node->GetResourceType() == ERTSResource::Wood ? WoodWorkers : FoodWorkers)++; }
		}
		else if (Order.Type == ERTSOrderType::ReturnResources && Worker->GetGatherer())
		{ (Worker->GetGatherer()->GetCarriedType() == ERTSResource::Wood ? WoodWorkers : FoodWorkers)++; }
	}
	for (ARTSUnit* Worker : State.Workers)
	{
		if (Worker == Volunteer || !IdleWorker(Worker)) { continue; }
		const float* Last = LastWorkAssignment.Find(Worker);
		if (Last && Now - *Last < AssignmentCooldown) { continue; }
		if (Worker->GetGatherer() && Worker->GetGatherer()->GetCarriedAmount() > 0)
		{
			if (IsValid(Hall) && Hall->IsDropOff())
			{ URTSOrderLibrary::IssueWorkOrder({Worker}, Hall); LastWorkAssignment.Add(Worker, Now); }
			continue;
		}
		// Stock and assigned labour both contribute to the next idle worker's choice.
		const double WoodSupply = Stock->GetAmount(ERTSResource::Wood) + WoodWorkers * 75.0;
		const double FoodSupply = Stock->GetAmount(ERTSResource::Food) + FoodWorkers * 75.0;
		const ERTSResource Wanted = WoodWorkers == 0 ? ERTSResource::Wood
			: FoodWorkers == 0 ? ERTSResource::Food
			: WoodSupply < FoodSupply * 0.9 + (bNeedFarm ? 80.0 : 0.0) ? ERTSResource::Wood : ERTSResource::Food;
		ARTSResourceNode* Best = nullptr;
		double BestScore = TNumericLimits<double>::Max();
		for (ARTSResourceNode* Node : State.Nodes)
		{
			const double Score = FVector::Dist2D(Home, Node->GetActorLocation()) * 2.0
				+ FVector::Dist2D(Worker->GetActorLocation(), Node->GetActorLocation())
				+ (Node->GetResourceType() == Wanted ? 0.0 : 1000000.0);
			if (Score < BestScore) { Best = Node; BestScore = Score; }
		}
		if (Best)
		{
			URTSOrderLibrary::IssueWorkOrder({Worker}, Best);
			LastWorkAssignment.Add(Worker, Now);
			(Best->GetResourceType() == ERTSResource::Wood ? WoodWorkers : FoodWorkers)++;
		}
	}

	// Reserve the next worker and necessary farm BEFORE adding a military order.
	const int32 KeepWood = ReserveWood + (bNeedFarm && FarmRow ? FarmRow->WoodCost : 0);
	const int32 KeepFood = ReserveFood + (bNeedFarm && FarmRow ? FarmRow->FoodCost : 0);
	for (ARTSBuilding* Building : Barracks)
	{
		if (State.Army.Num() + QueuedArmy >= Parameters.ArmyTarget) { break; }
		URTSProducerComponent* Producer = Building->GetProducer();
		if (!Building->IsComplete() || !Producer || !Producer->GetQueue().IsEmpty()) { continue; }
		const int32 DesiredArchers = FMath::RoundToInt((State.Army.Num() + QueuedArmy + 1) * Parameters.ArcherFraction);
		const FName UnitId = Archers < DesiredArchers ? FName(TEXT("Archer")) : FName(TEXT("Swordman"));
		if (!CanSpend(UnitId, KeepWood, KeepFood)) { continue; }
		FString Error;
		if (Producer->Enqueue(UnitId, Error)) { ++QueuedArmy; if (UnitId == TEXT("Archer")) { ++Archers; } }
	}
	Intent = FString::Printf(TEXT("发展经济：工人%d/%d，军队%d/%d，农场%d，工地%d"),
		State.Workers.Num(), Parameters.WorkerTarget, State.Army.Num(), Parameters.ArmyTarget, Farms, Sites.Num());
}

void ARTSEnemyDirector::RunMilitary(const FRTSAIWorldState& State, URTSMatchSubsystem& Match)
{
	const FVector Home = Match.GetBaseLocation(Team);
	const float Now = Match.GetElapsedSeconds();
	ARTSBuilding* PlayerHall = Match.GetMainBase(RTSTeam::Player);
	Wave.RemoveAll([](const TWeakObjectPtr<ARTSUnit>& Unit) { return !Unit.IsValid() || !Unit->IsAlive() || Unit->GetTeam() != Team; });
	for (auto It = Defenders.CreateIterator(); It; ++It)
	{
		if (!It->IsValid() || !(*It)->IsAlive()) { It.RemoveCurrent(); }
	}
	bool bInvasion = false;
	for (ARTSUnit* Enemy : State.Enemies)
	{
		if (FVector::DistSquared2D(Enemy->GetActorLocation(), Home) > FMath::Square(DefenceRadius)) { continue; }
		for (ARTSUnit* Observer : State.Workers)
		{
			if (VisibleTo(Observer, Enemy)) { bInvasion = true; break; }
		}
		if (!bInvasion)
		{
			for (ARTSUnit* Observer : State.Army) { if (VisibleTo(Observer, Enemy)) { bInvasion = true; break; } }
		}
		if (bInvasion) { break; }
	}
	TSet<TWeakObjectPtr<ARTSUnit>> DefendingNow;
	if (bInvasion)
	{
		for (ARTSUnit* Soldier : State.Army)
		{
			// Do not recall a distant active wave across the whole map.
			if (Wave.Contains(Soldier) && FVector::DistSquared2D(Soldier->GetActorLocation(), Home) > FMath::Square(DefenceRadius * 1.5f)) { continue; }
			AActor* Current = Soldier->GetCurrentOrder().TargetActor.Get();
			if (ValidAttack(Soldier) && Cast<ARTSUnit>(Current)
				&& FVector::DistSquared2D(Current->GetActorLocation(), Home) <= FMath::Square(DefenceRadius)
				&& VisibleTo(Soldier, Current, DefenceRadius * 1.5f))
			{ Defenders.Add(Soldier); DefendingNow.Add(Soldier); continue; }
			if (ARTSUnit* Enemy = NearestEnemy(Soldier, State.Enemies, &Home))
			{ AttackIfChanged(Soldier, Enemy); Defenders.Add(Soldier); DefendingNow.Add(Soldier); }
		}
	}

	// Each launch selects a fixed roster of previously uncommitted soldiers.
	// Wave stores their union, so veterans do not block later timed launches.
	if (!bInvasion && Now >= NextAttackSeconds)
	{
		TArray<ARTSUnit*> Candidates;
		for (ARTSUnit* Soldier : State.Army) { if (!Wave.Contains(Soldier)) { Candidates.Add(Soldier); } }
		const bool bEnough = Candidates.Num() >= Parameters.AttackSize;
		const bool bDeadline = Now >= NextAttackSeconds + Parameters.AttackInterval && Candidates.Num() >= 2;
		if (bEnough || bDeadline)
		{
			Candidates.Sort([Home](const ARTSUnit& A, const ARTSUnit& B)
			{
				return FVector::DistSquared2D(A.GetActorLocation(), Home) < FVector::DistSquared2D(B.GetActorLocation(), Home);
			});
			const int32 Count = FMath::Min(Parameters.AttackSize, Candidates.Num());
			for (int32 Index = 0; Index < Count; ++Index) { Wave.Add(Candidates[Index]); }
			NextAttackSeconds = Now + Parameters.AttackInterval;
		}
	}
	for (ARTSUnit* Soldier : State.Army)
	{
		if (DefendingNow.Contains(Soldier)) { continue; }
		if (Wave.Contains(Soldier))
		{
			AActor* Current = Soldier->GetCurrentOrder().TargetActor.Get();
			// Keep a live local combat target; the strategic hall target can be
			// interrupted by a visible enemy along the march and resumed afterwards.
			if (ValidAttack(Soldier) && Current != PlayerHall && VisibleTo(Soldier, Current)) { continue; }
			if (ARTSUnit* Enemy = NearestEnemy(Soldier, State.Enemies)) { AttackIfChanged(Soldier, Enemy); continue; }
			ARTSBuilding* Best = nullptr;
			double BestDistance = FMath::Square(SightRadius);
			for (ARTSBuilding* Building : State.EnemyBuildings)
			{
				const double Distance = FVector::DistSquared2D(Soldier->GetActorLocation(), Building->GetActorLocation());
				if (Distance < BestDistance && VisibleTo(Soldier, Building)) { Best = Building; BestDistance = Distance; }
			}
			if (Best) { AttackIfChanged(Soldier, Best); }
			else if (IsValid(PlayerHall) && PlayerHall->IsAlive()) { AttackIfChanged(Soldier, PlayerHall); }
		}
		else if (Defenders.Contains(Soldier))
		{
			// A hidden/fleeing raider is no reason to chase indefinitely away from home.
			if (ValidAttack(Soldier) && VisibleTo(Soldier, Soldier->GetCurrentOrder().TargetActor.Get())
				&& FVector::DistSquared2D(Soldier->GetActorLocation(), Home) <= FMath::Square(DefenceRadius * 1.5f)) { continue; }
			URTSOrderLibrary::IssueMoveOrder(this, {Soldier}, Home);
			Defenders.Remove(Soldier); // Return only once; normal unit movement handles arrival.
		}
	}
	if (bInvasion) { Intent = TEXT("防守：军队迎击基地附近可见入侵者，工人继续工作"); }
	else if (!Wave.IsEmpty())
	{
		Intent = FString::Printf(TEXT("进攻：已出征%d人，沿途接敌后继续攻击玩家主基地"), Wave.Num());
	}
}
