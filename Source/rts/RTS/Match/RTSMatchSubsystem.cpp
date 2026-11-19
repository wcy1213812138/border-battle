#include "Match/RTSMatchSubsystem.h"
#include "AI/RTSEnemyDirector.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSProjectile.h"
#include "Core/RTSHumanController.h"
#include "Core/RTSTypes.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Orders/RTSOrderLibrary.h"
#include "Selection/RTSSelectionComponent.h"
#include "Units/RTSTrainingDummy.h"
#include "Units/RTSUnit.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "HAL/IConsoleManager.h"
#include "Debug/RTSDebugSubsystem.h"

URTSMatchSubsystem* URTSMatchSubsystem::Get(const UObject* Context)
{
	UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<URTSMatchSubsystem>() : nullptr;
}
bool URTSMatchSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{ return Type == EWorldType::Game || Type == EWorldType::PIE; }
bool URTSMatchSubsystem::IsTickable() const { return !IsTemplate() && Phase == ERTSMatchPhase::Playing; }
TStatId URTSMatchSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(URTSMatchSubsystem, STATGROUP_Tickables); }
FText URTSMatchSubsystem::DifficultyName(ERTSDifficulty Value)
{
	switch (Value)
	{
	case ERTSDifficulty::Easy: return NSLOCTEXT("MiniRTS", "Easy", "简单");
	case ERTSDifficulty::Hard: return NSLOCTEXT("MiniRTS", "Hard", "困难");
	default: return NSLOCTEXT("MiniRTS", "Normal", "普通");
	}
}
ARTSEnemyDirector* URTSMatchSubsystem::GetEnemyDirector() const { return EnemyDirector.Get(); }
ARTSBuilding* URTSMatchSubsystem::GetMainBase(uint8 Team) const
{ return Team < 2 ? MainBases[Team].Get() : nullptr; }
void URTSMatchSubsystem::Deinitialize()
{
	OnMatchChanged.Clear(); ObservedUnits.Reset();
	Super::Deinitialize();
}
void URTSMatchSubsystem::ClearRuntimeActors()
{
	if (URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(this)) { Debug->ResetSession(); }
	bDebugUsed = false;
	if (ARTSHumanController* PC = Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController()))
	{
		PC->GetSelection()->ClearSelection(); PC->SelectBuilding(nullptr);
		if (PC->IsPlacingBuilding()) { PC->CancelBuildingPlacement(); }
	}
	TArray<AActor*> Old;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->IsA<ARTSUnit>() || It->IsA<ARTSBuilding>() || It->IsA<ARTSResourceNode>()
			|| It->IsA<ARTSProjectile>() || It->IsA<ARTSTrainingDummy>() || It->IsA<ARTSEnemyDirector>()) { Old.Add(*It); }
	}
	for (AActor* Actor : Old) { Actor->Destroy(); }
	MainBases[0].Reset(); MainBases[1].Reset(); EnemyDirector.Reset(); ObservedUnits.Reset();
}
void URTSMatchSubsystem::EnterSandbox()
{
	const bool bClear = IsManagedMatch() || (bMenuHasReturnState && MenuReturnPhase != ERTSMatchPhase::Sandbox);
	bMenuHasReturnState = false;
	Phase = ERTSMatchPhase::Sandbox;
	if (URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(this)) { Debug->ResetSession(); }
	bDebugUsed = false;
	UGameplayStatics::SetGamePaused(GetWorld(), false);
	if (bClear) { ClearRuntimeActors(); }
	if (EnemyDirector.IsValid()) { EnemyDirector->Destroy(); EnemyDirector.Reset(); }
	Elapsed = 0; Message.Reset(); OnMatchChanged.Broadcast();
}
void URTSMatchSubsystem::ShowMainMenu()
{
	bMenuHasReturnState = false;
	Phase = ERTSMatchPhase::MainMenu;
	UGameplayStatics::SetGamePaused(GetWorld(), false);
	ClearRuntimeActors(); Elapsed = 0; Message.Reset();
	OnMatchChanged.Broadcast();
}
void URTSMatchSubsystem::ToggleMainMenu()
{
	if (Phase == ERTSMatchPhase::MainMenu)
	{
		Phase = bMenuHasReturnState ? MenuReturnPhase : ERTSMatchPhase::Sandbox;
		const bool bRestorePause = bMenuHasReturnState && bMenuReturnPaused;
		bMenuHasReturnState = false;
		UGameplayStatics::SetGamePaused(GetWorld(), bRestorePause);
	}
	else
	{
		MenuReturnPhase = Phase;
		bMenuReturnPaused = UGameplayStatics::IsGamePaused(GetWorld());
		bMenuHasReturnState = true;
		Phase = ERTSMatchPhase::MainMenu;
		UGameplayStatics::SetGamePaused(GetWorld(), true);
	}
	OnMatchChanged.Broadcast();
}
bool URTSMatchSubsystem::FindStartingAreas(FVector& Player, FVector& Enemy, FString& Error) const
{
	if (bHasStartingAreas) { Player = BaseLocations[0]; Enemy = BaseLocations[1]; return true; }
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav || Nav->IsNavigationBuildInProgress()) { Error = TEXT("导航仍在生成，请稍后点击开始。"); return false; }
	TArray<FVector> Areas;
	FCollisionObjectQueryParams Objects; Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSMatchSetup), false);
	auto ClearPoint = [&](const FVector& Point, const FVector& Extent)
	{
		FNavLocation P;
		return Nav->ProjectPointToNavigation(Point, P, FVector(20, 20, 40))
			&& FVector::Dist2D(Point, P.Location) < 10 && FMath::Abs(P.Location.Z - Point.Z) < 25
			&& !GetWorld()->OverlapAnyTestByObjectType(Point + FVector(0, 0, 100), FQuat::Identity,
				Objects, FCollisionShape::MakeBox(Extent), Params);
	};
	for (int32 X = -1800; X <= 1800; X += 150)
	{
		for (int32 Y = -1800; Y <= 1800; Y += 150)
		{
			const FVector Center(X, Y, 10);
			bool bClear = ClearPoint(Center, FVector(155, 155, 80));
			for (int32 A = -2; A <= 2 && bClear; ++A)
			{
				for (int32 B = -2; B <= 2 && bClear; ++B)
				{ bClear = ClearPoint(Center + FVector(A * 75, B * 75, 0), FVector(20, 20, 80)); }
			}
			if (bClear) { Areas.Add(Center); }
		}
	}
	auto ClearLayout = [&](const FVector& Center, const FVector& Other)
	{
		const FVector Forward = (Other - Center).GetSafeNormal2D();
		const FVector Across(-Forward.Y, Forward.X, 0);
		for (int32 i = 0; i < 4; ++i)
		{ if (!ClearPoint(Center + Forward * 280 + Across * (i - 1.5) * 130, FVector(45, 45, 78))) { return false; } }
		for (int32 i = 0; i < 2; ++i)
		{
			if (!ClearPoint(Center + Forward * 470 + Across * (i == 0 ? -90 : 90), FVector(45, 45, 78))
				|| !ClearPoint(Center - Forward * 330 + Across * (i == 0 ? -350 : 350), FVector(55, 55, 80))) { return false; }
		}
		return true;
	};
	// Validate actual occupied footprints and a connected route, preserving terrain.
	double Best = 0;
	for (int32 A = 0; A < Areas.Num(); ++A)
	{
		for (int32 B = A + 1; B < Areas.Num(); ++B)
		{
			const double D = FVector::DistSquared2D(Areas[A], Areas[B]);
			if (D <= Best || D < FMath::Square(2200.0)
				|| !ClearLayout(Areas[A], Areas[B]) || !ClearLayout(Areas[B], Areas[A])) { continue; }
			UNavigationPath* Path = Nav->FindPathToLocationSynchronously(GetWorld(), Areas[A], Areas[B]);
			if (Path && Path->IsValid() && !Path->IsPartial()) { Best = D; Player = Areas[A]; Enemy = Areas[B]; }
		}
	}
	if (Best == 0) { Error = TEXT("地图没有两处足够宽敞且连通的基地区域，请等待导航完成后重试。"); return false; }
	return true;
}
bool URTSMatchSubsystem::StartMatch(ERTSDifficulty Value, FString& Error)
{
	Error.Reset();
	if (Value != ERTSDifficulty::Easy && Value != ERTSDifficulty::Normal && Value != ERTSDifficulty::Hard)
	{ Error = TEXT("难度无效。"); return false; }
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(this);
	FVector Starts[2];
	FRTSAIDifficulty Parameters;
	if (!Balance || !Balance->GetResources(0) || !Balance->GetResources(1)) { Error = TEXT("对局资源尚未就绪。"); return false; }
	if (!ARTSEnemyDirector::LoadDifficulty(Value, Parameters, Error) || !FindStartingAreas(Starts[0], Starts[1], Error))
	{ Message = Error; OnMatchChanged.Broadcast(); return false; }
	// Validate art before replacing the current runtime scene.
	if (!Balance->CreateUnitData(TEXT("Peasant"), this) || !Balance->CreateUnitData(TEXT("Swordman"), this)
		|| !Balance->CreateUnitData(TEXT("Archer"), this)) { Error = TEXT("缺少单位模型或动画，无法开始对局。"); return false; }
	Phase = ERTSMatchPhase::MainMenu;
	bMenuHasReturnState = false;
	UGameplayStatics::SetGamePaused(GetWorld(), false);
	ClearRuntimeActors();
	Stats[0] = FRTSMatchStats(); Stats[1] = FRTSMatchStats();
	Elapsed = 0; ObserveTimer = 0; Difficulty = Value;
	BaseLocations[0] = Starts[0]; BaseLocations[1] = Starts[1];
	bHasStartingAreas = true;
	bool bSuccess = true;
	for (uint8 Team = 0; Team < 2; ++Team)
	{
		const FVector Forward = (Starts[1 - Team] - Starts[Team]).GetSafeNormal2D();
		const FVector Across(-Forward.Y, Forward.X, 0);
		const FTransform Transform(Starts[Team]);
		ARTSBuilding* Hall = GetWorld()->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Hall) { bSuccess = false; break; }
		Hall->Configure(TEXT("TownHall"), Team, true); Hall->FinishSpawning(Transform); MainBases[Team] = Hall;
		for (int32 i = 0; i < 4; ++i)
		{
			ARTSUnit* Unit = Balance->SpawnUnit(TEXT("Peasant"), Team, Starts[Team] + Forward * 280 + Across * (i - 1.5) * 130);
			bSuccess &= Unit != nullptr;
		}
		for (int32 i = 0; i < 2; ++i)
		{
			ARTSUnit* Unit = Balance->SpawnUnit(i == 0 ? TEXT("Swordman") : TEXT("Archer"), Team,
				Starts[Team] + Forward * 470 + Across * (i == 0 ? -90 : 90));
			bSuccess &= Unit != nullptr;
		}
		for (int32 Type = 0; Type < 2; ++Type)
		{
			const FTransform ResourceTransform(Starts[Team] - Forward * 330 + Across * (Type == 0 ? -350 : 350) + FVector(0, 0, 90));
			ARTSResourceNode* Node = GetWorld()->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(), ResourceTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Node) { bSuccess = false; continue; }
			Node->Configure(Type == 0 ? ERTSResource::Wood : ERTSResource::Food, Type == 0 ? 5000 : 1800);
			Node->FinishSpawning(ResourceTransform);
		}
		URTSResourceComponent* Stock = Balance->GetResources(Team);
		Stock->Add(ERTSResource::Wood, 300 - Stock->GetAmount(ERTSResource::Wood));
		Stock->Add(ERTSResource::Food, 250 - Stock->GetAmount(ERTSResource::Food));
	}
	if (!bSuccess)
	{
		ClearRuntimeActors(); Error = TEXT("初始单位位置被阻挡，请重新开始。"); Message = Error; OnMatchChanged.Broadcast(); return false;
	}
	EnemyDirector = GetWorld()->SpawnActor<ARTSEnemyDirector>();
	if (!EnemyDirector.IsValid()) { ClearRuntimeActors(); Error = TEXT("无法创建电脑指挥器。"); return false; }
	EnemyDirector->Configure(Value);
	Phase = ERTSMatchPhase::Playing;
	Message = TEXT("摧毁敌方开局主基地获胜；保护己方主基地。选工人右键资源采集，先建兵营再训练军队。");
	ObserveUnits();
	if (ARTSHumanController* PC = Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController()))
	{
		if (APawn* Camera = PC->GetPawn()) { Camera->SetActorLocation(Starts[0] + FVector(0, 0, 80)); }
		for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
		{ if (It->GetTeam() == 0 && It->GetUnitData()->bWorker) { PC->GetSelection()->SelectSingle(*It, true); } }
		PC->SetEconomyMessage(Message);
	}
	OnMatchChanged.Broadcast(); return true;
}
void URTSMatchSubsystem::SetMatchPaused(bool bPaused)
{
	if ((bPaused && Phase != ERTSMatchPhase::Playing) || (!bPaused && Phase != ERTSMatchPhase::Paused)) { return; }
	Phase = bPaused ? ERTSMatchPhase::Paused : ERTSMatchPhase::Playing;
	UGameplayStatics::SetGamePaused(GetWorld(), bPaused); OnMatchChanged.Broadcast();
}
void URTSMatchSubsystem::TogglePause()
{ SetMatchPaused(Phase == ERTSMatchPhase::Playing); }
void URTSMatchSubsystem::Surrender()
{
	if (Phase == ERTSMatchPhase::Playing || Phase == ERTSMatchPhase::Paused) { Finish(ERTSMatchPhase::Defeat); }
}
void URTSMatchSubsystem::Finish(ERTSMatchPhase Result)
{
	Phase = Result;
	if (URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(this)) { Debug->RestoreSpeed(); }
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It) { URTSOrderLibrary::IssueStopOrder({*It}); }
	if (ARTSHumanController* PC = Cast<ARTSHumanController>(GetWorld()->GetFirstPlayerController()))
	{ if (PC->IsPlacingBuilding()) { PC->CancelBuildingPlacement(); } }
	UGameplayStatics::SetGamePaused(GetWorld(), true); OnMatchChanged.Broadcast();
}
void URTSMatchSubsystem::ObserveUnits()
{
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		if (!ObservedUnits.Contains(*It) && It->IsAlive())
		{ ObservedUnits.Add(*It); It->GetHealth()->OnDied.AddUniqueDynamic(this, &URTSMatchSubsystem::OnUnitDied); }
	}
}
void URTSMatchSubsystem::Tick(float DeltaTime)
{
	if (Phase != ERTSMatchPhase::Playing || UGameplayStatics::IsGamePaused(GetWorld())) { return; }
	Elapsed += DeltaTime; ObserveTimer += DeltaTime;
	ObserveUnits();
	const bool bPlayerAlive = MainBases[0].IsValid() && MainBases[0]->IsAlive();
	const bool bEnemyAlive = MainBases[1].IsValid() && MainBases[1]->IsAlive();
	if (!bPlayerAlive || !bEnemyAlive)
	{ Finish(!bPlayerAlive && !bEnemyAlive ? ERTSMatchPhase::Draw : bEnemyAlive ? ERTSMatchPhase::Defeat : ERTSMatchPhase::Victory); return; }
	if (ObserveTimer >= 1) { ObserveTimer = 0; OnMatchChanged.Broadcast(); }
}
void URTSMatchSubsystem::OnUnitDied(URTSHealthComponent* Health, AActor* Killer)
{
	if (Phase != ERTSMatchPhase::Playing) { return; }
	const ARTSUnit* Unit = Health ? Cast<ARTSUnit>(Health->GetOwner()) : nullptr;
	if (Unit && Unit->GetTeam() < 2) { ++Stats[Unit->GetTeam()].UnitsLost; }
	const IGenericTeamAgentInterface* Source = Cast<IGenericTeamAgentInterface>(Killer);
	if (Unit && Source && Source->GetGenericTeamId().GetId() < 2 && Source->GetGenericTeamId().GetId() != Unit->GetTeam())
	{ ++Stats[Source->GetGenericTeamId().GetId()].UnitsKilled; }
}
int32 URTSMatchSubsystem::GetUnitCount(uint8 Team, bool bWorkers) const
{
	int32 Count = 0;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{ if (It->IsAlive() && It->GetTeam() == Team && It->GetUnitData() && It->GetUnitData()->bWorker == bWorkers) { ++Count; } }
	return Count;
}
void URTSMatchSubsystem::NotifyUnitTrained(uint8 Team)
{ if (Phase == ERTSMatchPhase::Playing && Team < 2) { ++Stats[Team].UnitsTrained; ObserveUnits(); } }
void URTSMatchSubsystem::NotifyBuildingCompleted(uint8 Team)
{ if (Phase == ERTSMatchPhase::Playing && Team < 2) { ++Stats[Team].BuildingsBuilt; } }
void URTSMatchSubsystem::NotifyGathered(uint8 Team, bool bWood, int32 Amount)
{ if (Phase == ERTSMatchPhase::Playing && Team < 2) { (bWood ? Stats[Team].WoodGathered : Stats[Team].FoodGathered) += FMath::Max(0, Amount); } }

#if WITH_EDITOR || UE_BUILD_DEBUG
static FAutoConsoleCommandWithWorldAndArgs GStartRTSMatch(TEXT("rts.StartMatch"), TEXT("开始完整对局：0简单、1普通、2困难（默认1）。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World))
		{
			int32 Value = 1;
			if (Args.Num() > 1 || (Args.Num() == 1 && (!LexTryParseString(Value, *Args[0]) || Value < 0 || Value > 2)))
			{ UE_LOG(LogRTS, Warning, TEXT("用法：rts.StartMatch 0/1/2")); return; }
			FString Error; if (!Match->StartMatch(static_cast<ERTSDifficulty>(Value), Error)) { UE_LOG(LogRTS, Warning, TEXT("%s"), *Error); }
		}
	}));

// Development console entry point: drives the real match loop instead of scripted income or victory.
static FAutoConsoleCommandWithWorldAndArgs GTestRTSGameLoop(TEXT("rts.TestGameLoop"), TEXT("完整循环试玩：0简单、1普通、2困难（默认0）；替换当前运行时场景，不自动结束PIE。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || World->WorldType != EWorldType::PIE || !World->HasBegunPlay()
			|| !World->GetMapName().EndsWith(TEXT("L_M0Test")))
		{ UE_LOG(LogRTS, Warning, TEXT("请先打开 L_M0Test 并运行PIE，再输入 rts.TestGameLoop。")); return; }
		int32 Value = 0;
		if (Args.Num() > 1 || (Args.Num() == 1 && (!LexTryParseString(Value, *Args[0]) || Value < 0 || Value > 2)))
		{ UE_LOG(LogRTS, Warning, TEXT("用法：rts.TestGameLoop 0/1/2（默认简单）。")); return; }
		if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World))
		{
			FString Error;
			if (!Match->StartMatch(static_cast<ERTSDifficulty>(Value), Error))
			{ UE_LOG(LogRTS, Warning, TEXT("完整循环场景启动失败：%s"), *Error); return; }
			UE_LOG(LogRTS, Display, TEXT("完整循环场景已启动｜%s｜蓝方基地 %s｜红方基地 %s"),
				*URTSMatchSubsystem::DifficultyName(static_cast<ERTSDifficulty>(Value)).ToString(),
				*Match->GetBaseLocation(0).ToCompactString(), *Match->GetBaseLocation(1).ToCompactString());
			UE_LOG(LogRTS, Display, TEXT("试玩顺序：分配工人采木材/食物 → 建兵营并施工 → 训练剑士/弓手 → 防守/进攻 → 摧毁红方开局主基地 → 结算后重开。双方各4工人+2士兵、300木材/250食物，无自动采集或额外赠送。"));
			UE_LOG(LogRTS, Display, TEXT("P暂停；WASD移动镜头；滚轮缩放；暂停/结算面板可重开或返回菜单。不会自动结束PIE。操作清单见 Docs/GAMELOOP_PLAYTEST.md。"));
		}
	}));
#endif
