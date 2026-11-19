#include "Tests/RTSEconomyScenario.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Units/RTSUnit.h"
#include "Units/RTSTrainingDummy.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "Economy/RTSResourceComponent.h"
#include "Combat/RTSProjectile.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Match/RTSMatchSubsystem.h"

bool RTSEconomyScenario::Create(UWorld* World, FRTSEconomyScenario& Out, FString& Error)
{
#if !UE_BUILD_SHIPPING
	if (!World || !World->IsGameWorld() || !World->GetMapName().EndsWith(TEXT("L_M0Test")))
	{ Error = TEXT("请先打开测试地图 L_M0Test 并点击运行。"); return false; }
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Balance || !Nav || !Balance->GetResources(0) || !Balance->Find(TEXT("Peasant"))) { Error = TEXT("数值表或导航尚未就绪。"); return false; }
	FVector Center = FVector::ZeroVector, Forward = FVector::ForwardVector;
	bool bFound = false;
	for (int32 X = -1800; X <= 1800 && !bFound; X += 150)
	{
		for (int32 Y = -1500; Y <= 1500 && !bFound; Y += 150)
		{
			for (const FVector F : {FVector::RightVector, FVector::ForwardVector})
			{
				const FVector Across(-F.Y, F.X, 0);
				const FVector Candidate(X, Y, 10);
				bool bClear = true;
				FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic);
				for (int32 A = -3; A <= 3 && bClear; ++A)
				{
					for (int32 B = -6; B <= 6 && bClear; ++B)
					{
						const FVector Point = Candidate + Across * A * 80 + F * B * 80;
						FNavLocation P;
						bClear = Nav->ProjectPointToNavigation(Point, P, FVector(20, 20, 40)) && FVector::Dist2D(P.Location, Point) < 10
							&& FMath::Abs(P.Location.Z - 10) < 20 && !World->OverlapAnyTestByObjectType(P.Location + FVector(0, 0, 82), FQuat::Identity, Objects, FCollisionShape::MakeCapsule(40, 78));
					}
				}
				if (bClear) { Center = Candidate; Forward = F; bFound = true; break; }
			}
		}
	}
	if (!bFound) { Error = TEXT("未找到足够的空地，请等待导航生成后重试。"); return false; }
	// All replacements are runtime-only. Never save the level or its external actors.
	if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
	TArray<AActor*> Old;
	for (TActorIterator<AActor> It(World); It; ++It)
	{ if (It->IsA<ARTSUnit>() || It->IsA<ARTSBuilding>() || It->IsA<ARTSResourceNode>() || It->IsA<ARTSTrainingDummy>() || It->IsA<ARTSProjectile>()) { Old.Add(*It); } }
	if (auto* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController())) { PC->SelectBuilding(nullptr); PC->GetSelection()->ClearSelection(); if (PC->IsPlacingBuilding()) { PC->CancelBuildingPlacement(); } }
	for (AActor* Actor : Old) { Actor->Destroy(); }
	Out = FRTSEconomyScenario(); Out.Center = Center;
	const FVector Across(-Forward.Y, Forward.X, 0);
	const FTransform HallTransform(Center - Forward * 320);
	Out.Hall = World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), HallTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Out.Hall) { Error = TEXT("无法创建主基地。"); return false; }
	Out.Hall->Configure(TEXT("TownHall"), 0, true); Out.Hall->FinishSpawning(HallTransform);
	for (int32 i = 0; i < 3; ++i)
	{
		ARTSUnit* Worker = Balance->SpawnUnit(TEXT("Peasant"), 0, Center + Across * (i - 1) * 150);
		if (!Worker) { Error = TEXT("无法生成工人。"); return false; }
		Out.Workers.Add(Worker);
	}
	for (int32 i = 0; i < 2; ++i)
	{
		const FTransform Transform(Center + Forward * 350 + Across * (i == 0 ? -130 : 130) + FVector(0, 0, 90));
		ARTSResourceNode* Node = World->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Node) { Error = TEXT("无法生成资源点。"); return false; }
		Node->Configure(i == 0 ? ERTSResource::Wood : ERTSResource::Food, 500); Node->FinishSpawning(Transform);
		if (i == 0) { Out.Wood = Node; } else { Out.Food = Node; }
	}
	URTSResourceComponent* Resources = Balance->GetResources(0);
	Resources->Add(ERTSResource::Wood, 400 - Resources->GetAmount(ERTSResource::Wood));
	Resources->Add(ERTSResource::Food, 300 - Resources->GetAmount(ERTSResource::Food));
	if (auto* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController()))
	{
		for (ARTSUnit* Worker : Out.Workers) { PC->GetSelection()->SelectSingle(Worker, true); }
		if (APawn* Camera = PC->GetPawn()) { Camera->SetActorLocation(Center + FVector(0, 0, 80)); }
		PC->SetEconomyMessage(TEXT("经济场景已就绪：工人右键绿色木材或橙色食物采集；选中工人可建造，点击主基地可训练工人。"));
	}
	return true;
#else
	Error = TEXT("发行版本不提供经济测试场景。"); return false;
#endif
}
#if WITH_EDITOR || UE_BUILD_DEBUG
static FAutoConsoleCommandWithWorld GEconomyScenario(TEXT("rts.TestEconomy"), TEXT("测试地图运行时创建经济场景：3名工人、主基地、木材和食物，初始库存400木材/300食物。"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		FRTSEconomyScenario Scenario; FString Error;
		if (!RTSEconomyScenario::Create(World, Scenario, Error)) { UE_LOG(LogRTS, Warning, TEXT("%s"), *Error); }
		else { UE_LOG(LogRTS, Display, TEXT("经济场景已就绪：3名工人、主基地、木材和食物；停止运行后恢复原地图。")); }
	}));
#endif
