#include "Economy/RTSProducerComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Core/RTSTypes.h"
#include "Economy/RTSResourceComponent.h"
#include "Units/RTSUnit.h"
#include "Match/RTSMatchSubsystem.h"
#include "Orders/RTSOrderLibrary.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"
#include "Campaign/RTSCampaign.h"
#include "Campaign/RTSCampaignProgression.h"
#include "Core/RTSHumanController.h"
#include "Data/RTSUnitData.h"
#include "EngineUtils.h"

URTSProducerComponent::URTSProducerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.1f;
}

bool URTSProducerComponent::Enqueue(FName UnitId, FString& Error)
{
	Error.Reset();
 if (ARTSCampaign* Campaign = ARTSCampaign::Get(this); Campaign)
 {
  if(Campaign->GetActorFaction(GetOwner())>=0)
  {if(!Campaign->CanFactionTrain(GetOwner(),UnitId)){Error=TEXT("该势力尚未研究此兵种，或研究尚未开放。");return false;}}
  else if(!Campaign->CanTrain(UnitId,Error))return false;
 }
	if (const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this); Match && !Match->AllowsGameplay())
	{ Error = TEXT("当前对局状态不能训练单位。"); return false; }
	if (bMutatingQueue)
	{
		Error = TEXT("生产队列正在处理，请稍后重试。");
		return false;
	}
	ARTSBuilding* Building = Cast<ARTSBuilding>(GetOwner());
	if (!Building || !Building->IsAlive() || !Building->IsComplete())
	{
		Error = TEXT("需要一座存活且已建成的建筑。");
		return false;
	}
	const FName BuildingId = Building->GetBuildingId();
	ARTSCampaign* Campaign = ARTSCampaign::Get(this);
	const ARTSHumanController* PC = Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController());
	const bool bAllowed = Campaign
  ? (BuildingId == ARTSCampaign::TrainingService(UnitId) && (Campaign->GetActorFaction(Building)>=0 || (Building->GetTeam()==0 && PC && PC->GetSelectedBuilding()==Building)))
		: ((BuildingId == TEXT("TownHall") && UnitId == TEXT("Peasant"))
		|| (BuildingId == TEXT("Barracks") && (UnitId == TEXT("Swordman") || UnitId == TEXT("Archer"))));
	if (!bAllowed)
	{
		Error = TEXT("这座建筑不能训练该兵种。");
		return false;
	}
	if (Queue.Num() >= MaxQueueSize)
	{
		Error = TEXT("生产队列已满，最多容纳5个订单。");
		return false;
	}
	URTSBalanceSubsystem* Subsystem = URTSBalanceSubsystem::Get(this);
	const FRTSBalanceRow* Row = Subsystem ? Subsystem->Find(UnitId) : nullptr;
 URTSResourceComponent* Resources = Campaign?Campaign->GetActorAccount(Building):(Subsystem?Subsystem->GetResources(Building->GetTeam()):nullptr);
	if (!Row || !Resources)
	{
		Error = TEXT("无法获取单位数值或队伍资源。");
		return false;
	}
	if (Row->Kind != TEXT("Unit") || Row->WoodCost < 0 || Row->FoodCost < 0 || !FMath::IsFinite(Row->TrainSeconds) || Row->TrainSeconds <= 0.0f)
	{
		Error = TEXT("单位训练费用或时间无效。");
		return false;
	}

	FPaidOrder Order;
	Order.Wood = Row->WoodCost;
	Order.Food = Row->FoodCost;
	Order.TrainSeconds = Row->TrainSeconds;
	Order.PaidFrom = Resources;
	TMap<ERTSResource, int32> Cost;
	Cost.Add(ERTSResource::Wood, Order.Wood);
	Cost.Add(ERTSResource::Food, Order.Food);
	if(Campaign){Cost=Campaign->GetTrainingCost(UnitId,Building);Order.TrainSeconds=RTSCampaignProgression::TrainSeconds(UnitId);}
	Order.Cost=Cost;
	// Resource notifications are synchronous. Prevent callbacks from changing this transaction.
	TGuardValue<bool> Guard(bMutatingQueue, true);
	if (!Resources->TrySpend(Cost))
	{
		Error = TEXT("木材或食物不足。");
		return false;
	}
	if (!Building->IsAlive())
	{
		Error = TEXT("建筑在付款期间被摧毁。");
		return false; // Death deliberately forfeits paid production.
	}
	const bool bWasEmpty = Queue.IsEmpty();
	Queue.Add(UnitId);
	PaidOrders.Add(Order);
	if (bWasEmpty)
	{
		TrainingSeconds = 0.0f;
		bSpawnBlocked = false;
		SpawnBlockReason.Reset(); NextSpawnAttempt = 0;
	}
	OnQueueChanged.Broadcast();
	return true;
}

bool URTSProducerComponent::CancelLast()
{
	ARTSBuilding* Building = Cast<ARTSBuilding>(GetOwner());
	if (const URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this); Match && !Match->AllowsGameplay()) { return false; }
	if (bMutatingQueue || Queue.IsEmpty() || !Building || !Building->IsAlive()) { return false; }
	const FPaidOrder Order = PaidOrders.Last();
	URTSResourceComponent* Resources = Order.PaidFrom.Get();
	// Never silently cancel a paid order if its original resource account is unavailable.
	if (!Resources) { return false; }
	TGuardValue<bool> Guard(bMutatingQueue, true);
	Queue.RemoveAt(Queue.Num() - 1);
	PaidOrders.RemoveAt(PaidOrders.Num() - 1);
	if (Queue.IsEmpty())
	{
		TrainingSeconds = 0.0f;
		bSpawnBlocked = false;
		SpawnBlockReason.Reset(); NextSpawnAttempt = 0;
	}
	for(const auto& Paid:Order.Cost)Resources->Add(Paid.Key,Paid.Value);
	OnQueueChanged.Broadcast();
	return true;
}

float URTSProducerComponent::GetProgress() const
{
	if (Queue.IsEmpty() || PaidOrders.IsEmpty()) { return 0.0f; }
	const float Required = PaidOrders[0].TrainSeconds;
	return Required > 0.0f ? FMath::Clamp(TrainingSeconds / Required, 0.0f, 1.0f) : 1.0f;
}
bool URTSProducerComponent::CaptureProduction(FRTSProductionSnapshot& Out) const
{
	if(bMutatingQueue || Queue.Num()!=PaidOrders.Num())return false;
	Out=FRTSProductionSnapshot();Out.TrainingSeconds=TrainingSeconds;Out.bSpawnBlocked=bSpawnBlocked;
	for(int32 I=0;I<Queue.Num();++I){FRTSPaidTrainingSnapshot P;P.Kind=Queue[I];P.Cost=PaidOrders[I].Cost;P.TrainSeconds=PaidOrders[I].TrainSeconds;Out.Orders.Add(MoveTemp(P));}
	return true;
}
bool URTSProducerComponent::IsValidProduction(const FRTSProductionSnapshot& S,FName BuildingId,bool bComplete,bool bCampaign)
{
	if(S.Orders.Num()>MaxQueueSize || !FMath::IsFinite(S.TrainingSeconds) || S.TrainingSeconds<0)return false;
	if(S.Orders.IsEmpty())return S.TrainingSeconds==0 && !S.bSpawnBlocked;
	if(!bComplete || S.TrainingSeconds>S.Orders[0].TrainSeconds || (S.bSpawnBlocked && S.TrainingSeconds!=S.Orders[0].TrainSeconds))return false;
	for(const auto& P:S.Orders)
	{
		const bool Allowed=bCampaign?(!ARTSCampaign::TrainingService(P.Kind).IsNone() && ARTSCampaign::TrainingService(P.Kind)==BuildingId):((BuildingId==TEXT("TownHall") && P.Kind==TEXT("Peasant")) || (BuildingId==TEXT("Barracks") && (P.Kind==TEXT("Swordman") || P.Kind==TEXT("Archer"))));
		if(!Allowed || !FMath::IsFinite(P.TrainSeconds) || P.TrainSeconds<=0 || P.TrainSeconds>3600)return false;
		int64 Total=0;for(const auto& C:P.Cost){if(C.Key>=ERTSResource::Count || C.Value<0 || C.Value>10000000)return false;Total+=C.Value;}if(Total<=0)return false;
	}
	return true;
}
bool URTSProducerComponent::RestoreProduction(const FRTSProductionSnapshot& S,URTSResourceComponent* StagedPayer)
{
	auto* B=Cast<ARTSBuilding>(GetOwner());const bool Campaign=ARTSCampaign::Get(this)!=nullptr;
 auto* Balance=URTSBalanceSubsystem::Get(this);auto* C=ARTSCampaign::Get(this);auto* Resources=StagedPayer?StagedPayer:(C?C->GetActorAccount(B):(B && Balance?Balance->GetResources(B->GetTeam()):nullptr));
	if(bMutatingQueue || !Queue.IsEmpty() || !PaidOrders.IsEmpty() || !B || !B->IsAlive() || !Resources || !IsValidProduction(S,B->GetBuildingId(),B->IsComplete(),Campaign))return false;
	for(const auto& P:S.Orders){const auto* Row=Balance->Find(P.Kind);if(!Row || Row->Kind!=TEXT("Unit"))return false;}
	TGuardValue<bool> Guard(bMutatingQueue,true);
	for(const auto& P:S.Orders){FPaidOrder O;O.Wood=P.Cost.FindRef(ERTSResource::Wood);O.Food=P.Cost.FindRef(ERTSResource::Food);O.Cost=P.Cost;O.TrainSeconds=P.TrainSeconds;O.PaidFrom=Resources;Queue.Add(P.Kind);PaidOrders.Add(MoveTemp(O));}
	TrainingSeconds=S.TrainingSeconds;bSpawnBlocked=S.bSpawnBlocked;OnQueueChanged.Broadcast();return true;
}

void URTSProducerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ARTSBuilding* Building = Cast<ARTSBuilding>(GetOwner());
	if (bMutatingQueue || Queue.IsEmpty() || !Building || !Building->IsAlive() || !Building->IsComplete()) { return; }
 if(auto* C=ARTSCampaign::Get(this);C && C->GetActorFaction(Building)>=0 && (!C->IsFactionEconomyActive(C->GetActorFaction(Building)) || C->GetGuideStep()<7))return;
	TGuardValue<bool> Guard(bMutatingQueue, true);
	const float Required = PaidOrders[0].TrainSeconds;
	if (FMath::IsFinite(DeltaTime) && DeltaTime > 0.0f)
	{
		TrainingSeconds = FMath::Min(TrainingSeconds + DeltaTime, Required);
	}
	if (TrainingSeconds < Required) { return; }

	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < NextSpawnAttempt) { return; }
	NextSpawnAttempt = Now + .5; // Bound expensive nav/collision retries while blocked.
	URTSBalanceSubsystem* Subsystem = URTSBalanceSubsystem::Get(this);
	ARTSCampaign* Campaign = ARTSCampaign::Get(this);
	FVector SpawnFeet=FVector::ZeroVector, VerifiedExit=FVector::ZeroVector;
	ARTSUnit* Spawned = nullptr;
	FString Reason;
	int32 Living = 0;
	if (Campaign) { for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It) { Living += It->IsAlive(); } }
	const auto* Shape = LoadObject<URTSUnitData>(nullptr, TEXT("/Game/RTS/Data/Units/DA_Swordman.DA_Swordman"));
	if (Campaign && Living >= 200) { Reason = TEXT("单位总数已达200上限，订单保留"); }
	else if (!Subsystem || !Shape) { Reason = TEXT("单位数据尚未就绪，订单保留"); }
	else
	{
		int32 Cursor = 0, Found = 0;
		for (int32 Attempt = 0; Attempt < 8; ++Attempt)
		{
			if (!Subsystem->FindProductionExit(Building, Shape->CapsuleRadius, Shape->CapsuleHalfHeight, SpawnFeet, VerifiedExit, Cursor, &Found)) { break; }
			Cursor = Found + 1;
			Spawned = Campaign ? Campaign->Spawn(Queue[0], Building->GetTeam(), SpawnFeet) : Subsystem->SpawnUnit(Queue[0], Building->GetTeam(), SpawnFeet);
			if (!Building->IsAlive() || Queue.IsEmpty()) { return; }
			if (IsValid(Spawned)) { break; }
		}
		if (!IsValid(Spawned)) { Reason = TEXT("周边没有可通行出口；移开单位或障碍后自动出兵"); }
	}
	if (!IsValid(Spawned))
	{
		const bool bChanged = !bSpawnBlocked || SpawnBlockReason != Reason;
		bSpawnBlocked = true; SpawnBlockReason = Reason;
		if (bChanged) { OnQueueChanged.Broadcast(); }
		return; // Keep the paid head at100%; never pop or charge twice.
	}
 if(Campaign)Campaign->RegisterProducedUnit(Spawned,Building);
	if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this)) { Match->NotifyUnitTrained(Building->GetTeam()); }

	Queue.RemoveAt(0);
	PaidOrders.RemoveAt(0);
	TrainingSeconds = 0.0f;
	bSpawnBlocked = false;
	SpawnBlockReason.Reset(); NextSpawnAttempt = 0;
	// A player rally point takes priority; it must still be reachable, otherwise the
	// ordinary outward exit step remains the fallback.
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	// VerifiedExit is retained as fallback when a user rally point is unreachable.
	if (Nav && Building->HasRallyPoint())
	{
		FNavLocation RallyTarget;
		if (Nav->ProjectPointToNavigation(Building->GetRallyPoint(), RallyTarget, FVector(60, 60, 200))
			&& FVector::Dist2D(RallyTarget.Location, SpawnFeet) > 1.0)
		{
			UNavigationPath* PlayerPath = Nav->FindPathToLocationSynchronously(GetWorld(), SpawnFeet, RallyTarget.Location, Spawned);
			if (PlayerPath && PlayerPath->IsValid() && !PlayerPath->IsPartial())
			{
				FRTSOrder RallyOrder; RallyOrder.Type = ERTSOrderType::Move; RallyOrder.TargetLocation = RallyTarget.Location;
				Spawned->ReceiveOrder(RallyOrder);
				OnQueueChanged.Broadcast();
				return;
			}
		}
	}
	// Use the exact escape step that passed capsule sweep and nav-path checks.
	// A radial step alone can leave units stranded at side exits, so the exit search covers the full footprint.
	FRTSOrder ExitOrder; ExitOrder.Type = ERTSOrderType::Move; ExitOrder.TargetLocation = VerifiedExit;
	Spawned->ReceiveOrder(ExitOrder);
	OnQueueChanged.Broadcast();
}

void URTSProducerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	TGuardValue<bool> Guard(bMutatingQueue, true);
	// Destruction/death forfeits all pending orders. Only explicit CancelLast refunds.
	const bool bHadState = !Queue.IsEmpty() || bSpawnBlocked;
	Queue.Reset();
	PaidOrders.Reset();
	TrainingSeconds = 0.0f;
	bSpawnBlocked = false;
	SpawnBlockReason.Reset(); NextSpawnAttempt = 0;
	SetComponentTickEnabled(false);
	if (bHadState) { OnQueueChanged.Broadcast(); }
	Super::EndPlay(EndPlayReason);
}
