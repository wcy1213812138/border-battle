#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSEconomyScenario.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "AIController.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSResourceComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Buildings/RTSBuilding.h"
#include "Orders/RTSOrderLibrary.h"
#include "Orders/RTSMoveGroup.h"
#include "Units/RTSUnit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSMultiWorkerTest, "MiniRTS.Congestion.SharedResourceWorkers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSLargeCrowdProductionTest, "MiniRTS.Congestion.ProductionBeyondFifty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSNarrowFormationTest, "MiniRTS.Congestion.NarrowDestination", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSDenseGateTest, "MiniRTS.Congestion.DenseGateAdvance", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckCongestion : public IAutomationLatentCommand
	{
	public:
		FCheckCongestion(FAutomationTestBase* InTest, int32 InMode) : Test(InTest), Mode(InMode) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
			FString Error;
			if (Stage == 0)
			{
				if (Mode == 3)
				{
					if (!Balance || !FNavigationSystem::GetCurrent<UNavigationSystemV1>(World)) { Test->AddError(TEXT("RTS navigation unavailable")); return true; }
					Scenario.Center = FVector::ZeroVector;
				}
				else if (!RTSEconomyScenario::Create(World, Scenario, Error)) { Test->AddError(Error); return true; }
				SetupAt = World->GetTimeSeconds();
				if (Mode == 0)
				{
					Workers = Scenario.Workers;
					for (int32 i = 0; i < 9; ++i)
					{
						FVector Feet;
						if (!Balance->FindSpawnPoint(Scenario.Center, 200, Feet)) { Test->AddError(TEXT("No space for worker crowd")); return true; }
						ARTSUnit* Unit = Balance->SpawnUnit(TEXT("Peasant"), 0, Feet);
						if (!Unit) { Test->AddError(TEXT("Cannot spawn worker crowd")); return true; }
						Workers.Add(Unit);
					}
				}
				else if (Mode == 1)
				{
					URTSUnitData* Data = Balance->CreateUnitData(TEXT("Swordman"), World);
					for (int32 i = 0; i < 54; ++i)
					{
						const FTransform T(FVector(700 + (i % 9) * 110, -1200 + (i / 9) * 110, 92));
						ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
						Unit->Configure(Data, 0); Unit->FinishSpawning(T);
					}
					FVector Place = Scenario.Center + FVector(0, 700, 0);
					bool bFound = false;
					for (int32 i = 0; i < 16; ++i)
					{
						const double Angle = i * PI / 8;
						Place = Scenario.Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0) * 750;
						if (Balance->CanPlace(TEXT("Barracks"), Place, Error)) { bFound = true; break; }
					}
					if (!bFound) { Test->AddError(TEXT("No barracks fixture location")); return true; }
					const FTransform T(Place);
					Barracks = World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					Barracks->Configure(TEXT("Barracks"), 0, true); Barracks->FinishSpawning(T);
					const FVector Extent = ARTSBuilding::CollisionExtent(TEXT("Barracks"), Barracks->GetFootprint());
					Test->TestTrue(TEXT("Barracks solid body smaller than placement footprint"), Extent.Y < Barracks->GetFootprint() * 0.4 && Extent.X < Barracks->GetFootprint() * 0.5);
					for (TActorIterator<ARTSUnit> It(World); It; ++It) { Existing.Add(*It); }
				}
				else
				{
					const FVector Home = Scenario.Center;
					Gate = Home + FVector(0, Mode == 3 ? 600 : 1200, 0);
					Goal = Home + FVector(0, Mode == 3 ? 1600 : 1200, 0);
					for (int32 Side : {-1, 1})
					{
						AActor* Wall = World->SpawnActor<AActor>();
						if (!Wall) { Test->AddError(TEXT("Cannot spawn corridor wall")); return true; }
						UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
						Wall->SetRootComponent(Box); Wall->AddInstanceComponent(Box);
						Box->SetBoxExtent(FVector(60, 650, 120));
						Box->SetCollisionProfileName(TEXT("BlockAll"));
						Box->SetCanEverAffectNavigation(true); Box->RegisterComponent();
						Wall->SetActorLocation(Gate + FVector(Side * 180.0f, 0, 120));
						UStaticMeshComponent* WallVisual = NewObject<UStaticMeshComponent>(Wall);
						WallVisual->SetupAttachment(Box);
						WallVisual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
						WallVisual->SetRelativeScale3D(FVector(1.2f, 13.0f, 2.4f));
						WallVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						WallVisual->SetCastShadow(false);
						Wall->AddInstanceComponent(WallVisual); WallVisual->RegisterComponent();
						if (UMaterialInstanceDynamic* Material = WallVisual->CreateAndSetMaterialInstanceDynamic(0))
						{ Material->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.85f, 0.08f, 0.04f)); }
					}
					for (ARTSUnit* Unit : Scenario.Workers) { Unit->Destroy(); }
					URTSUnitData* Data = Balance->CreateUnitData(TEXT("Swordman"), World);
					const int32 UnitCount = Mode == 3 ? 16 : 8;
					for (int32 i = 0; i < UnitCount; ++i)
					{
						const FVector Offset = Mode == 3
							? FVector((i % 4 - 1.5) * 110, -650 + (i / 4) * 110, 82)
							: FVector((i % 4 - 1.5) * 150, 350 + (i / 4) * 150, 82);
						const FTransform T(Home + Offset);
						ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), T, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
						if (!Unit) { Test->AddError(TEXT("Cannot spawn corridor unit")); return true; }
						Unit->Configure(Data, 0); Unit->FinishSpawning(T); Workers.Add(Unit);
					}
				}
				Stage = 1; return false;
			}
			if (Stage == 1)
			{
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				if (World->GetTimeSeconds() - SetupAt < 2 || (Nav && Nav->IsNavigationBuildInProgress())) { return false; }
				if (Mode == 0) { URTSOrderLibrary::IssueWorkOrder(Workers, Scenario.Wood); }
				else if (Mode == 1)
				{
					if (!Barracks->GetProducer()->Enqueue(TEXT("Swordman"), Error)) { Test->AddError(Error); return true; }
				}
				else { URTSOrderLibrary::IssueMoveOrder(World, Workers, Goal); }
				IssuedAt = World->GetTimeSeconds(); Stage = 2; return false;
			}
			const double Elapsed = World->GetTimeSeconds() - IssuedAt;
			if (Mode == 0)
			{
				bool bReservedOverlap = false;
				for (int32 i = 0; i < Workers.Num(); ++i)
				{
					URTSGathererComponent* G = Workers[i]->GetGatherer();
					if (G->GetCarriedAmount() > 0) { Participated.Add(Workers[i]); }
					bSawWaiting |= G->IsWaitingForWorkPosition();
					if (!G->HasWorkPosition()) { continue; }
					for (int32 j = i + 1; j < Workers.Num(); ++j)
					{
						const URTSGathererComponent* Other = Workers[j]->GetGatherer();
						// Reservations can be for different targets (resource versus drop-off).
						if (Other->HasWorkPosition() && FVector::Dist2D(G->GetWorkPosition(), Other->GetWorkPosition()) < 90) { bReservedOverlap = true; }
					}
				}
				if (bReservedOverlap) { Test->AddError(TEXT("Worker reservations overlap")); return true; }
				const int32 Delivered = Balance->GetResources(0)->GetAmount(ERTSResource::Wood) - 400;
				if (Delivered >= 100 && Participated.Num() == Workers.Num())
				{
					Test->TestTrue(TEXT("Overflow workers waited rather than pushing into resource"), bSawWaiting);
					Test->AddInfo(FString::Printf(TEXT("12 workers shared one node: all carried cargo, delivered %d wood in %.1fs, unique work reservations."), Delivered, Elapsed));
					return true;
				}
				if (Elapsed > 90) { Test->AddError(FString::Printf(TEXT("Worker crowd stalled: delivered=%d participated=%d/12"), Delivered, Participated.Num())); return true; }
			}
			else if (Mode == 1)
			{
				if (!Trained)
				{
					for (TActorIterator<ARTSUnit> It(World); It; ++It)
					{ if (!Existing.Contains(*It)) { Trained = *It; StartLocation = It->GetActorLocation(); break; } }
					if (Trained)
					{
						Test->TestNotNull(TEXT("Produced unit has controller beyond 50 agents"), Cast<AAIController>(Trained->GetController()));
					}
				}
				else if (FVector::Dist2D(StartLocation, Trained->GetActorLocation()) > 120)
				{ Test->AddInfo(TEXT("Produced swordman moved more than 120 cm with over 50 existing crowd agents.")); return true; }
				if (Elapsed > 20) { Test->AddError(TEXT("Produced swordman failed to move beyond crowd default capacity")); return true; }
			}
			else
			{
				bool bDone = true;
				for (ARTSUnit* Unit : Workers)
				{
					const FRTSMoveGroup* Group = Unit->GetMoveGroup();
					if (!Group || Unit->GetFormationSlotIndex() == INDEX_NONE || Unit->GetCurrentOrder().Type != ERTSOrderType::None) { bDone = false; continue; }
					if (FVector::Dist2D(Unit->GetActorLocation(), Group->Slots[Unit->GetFormationSlotIndex()]) > 15.1
						|| Unit->GetVelocity().SizeSquared2D() > 1 || Unit->IsRecoveringFromCongestion()
						|| (Mode == 3 && Unit->GetActorLocation().Y <= Gate.Y + 650 + Unit->GetSimpleCollisionRadius())) { bDone = false; }
				}
				if (!bDone) { SettledAt = -1; }
				else if (SettledAt < 0) { SettledAt = World->GetTimeSeconds(); }
				if (bDone && World->GetTimeSeconds() - SettledAt < 2) { return false; }
				if (bDone)
				{
					for (int32 i = 0; i < Workers.Num(); ++i)
					{
						Test->TestEqual(TEXT("Pawn collision retained after corridor advance"), Workers[i]->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
						for (int32 j = i + 1; j < Workers.Num(); ++j)
						{
							Test->TestTrue(TEXT("Formation slots remain unique"), FVector::Dist2D(Workers[i]->GetMoveGroup()->Slots[Workers[i]->GetFormationSlotIndex()], Workers[j]->GetMoveGroup()->Slots[Workers[j]->GetFormationSlotIndex()]) > 1);
							Test->TestFalse(TEXT("Pair ignore list restored"), Workers[i]->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Workers[j]));
							Test->TestFalse(TEXT("Reverse pair ignore list restored"), Workers[j]->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Workers[i]));
						}
					}
					Test->AddInfo(FString::Printf(TEXT("%d units crossed the gate and stayed settled for 2s with collision enabled in %.2fs."), Workers.Num(), Elapsed)); return true;
				}
				if (Elapsed > (Mode == 3 ? 80 : 60))
				{
					for (int32 Index = 0; Index < Workers.Num(); ++Index)
					{
						ARTSUnit* Unit = Workers[Index];
						const FRTSMoveGroup* Group = Unit ? Unit->GetMoveGroup() : nullptr;
						const int32 SlotIndex = Unit ? Unit->GetFormationSlotIndex() : INDEX_NONE;
						const AAIController* AI = Unit ? Cast<AAIController>(Unit->GetController()) : nullptr;
						const UPathFollowingComponent* Path = AI ? AI->GetPathFollowingComponent() : nullptr;
						Test->AddError(FString::Printf(TEXT("DenseGate unit %d: loc=%s velocity=%s slot=%d order=%d move=%d destination=%s"),
							Index, Unit ? *Unit->GetActorLocation().ToCompactString() : TEXT("null"),
							Unit ? *Unit->GetVelocity().ToCompactString() : TEXT("null"), SlotIndex,
							Unit ? static_cast<int32>(Unit->GetCurrentOrder().Type) : -1,
							AI ? static_cast<int32>(AI->GetMoveStatus()) : -1,
							Path ? *Path->GetPathDestination().ToCompactString() : TEXT("none")));
					}
					Test->AddError(TEXT("Narrow destination formation stalled or failed to fit before unit timeout")); return true;
				}
			return false;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		int32 Mode, Stage = 0;
		double Started = FPlatformTime::Seconds(), SetupAt = 0, IssuedAt = 0, SettledAt = -1;
		FRTSEconomyScenario Scenario;
		TArray<ARTSUnit*> Workers;
		TSet<ARTSUnit*> Existing, Participated;
		ARTSBuilding* Barracks = nullptr;
		ARTSUnit* Trained = nullptr;
		FVector Gate = FVector::ZeroVector;
		FVector StartLocation = FVector::ZeroVector, Goal = FVector::ZeroVector;
		bool bSawWaiting = false;
	};
	void RunCongestion(FAutomationTestBase* Test, int32 Mode)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FCheckCongestion(Test, Mode));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
}

static void SpawnDenseGateScenario(UWorld* World)
{
	if (!World || !World->IsGameWorld() || !World->GetMapName().EndsWith(TEXT("L_M0Test"))) { UE_LOG(LogRTS, Warning, TEXT("请先在 L_M0Test 点击运行 PIE，再输入 rts.TestDenseGate。")); return; }
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
	if (!Balance) { UE_LOG(LogRTS, Warning, TEXT("RTS 数值表尚未就绪。")); return; }
	const FName ScenarioTag(TEXT("RTSDenseGateFixture"));
	TArray<AActor*> Previous;
	for (TActorIterator<AActor> It(World); It; ++It) { if (It->ActorHasTag(ScenarioTag)) { Previous.Add(*It); } }
	for (AActor* Actor : Previous) { Actor->Destroy(); }
	const FVector Home = FVector::ZeroVector;
	const FVector Gate = Home + FVector(0, 600, 0);
	for (int32 Side : {-1, 1})
	{
		AActor* Wall = World->SpawnActor<AActor>();
		if (!Wall) { UE_LOG(LogRTS, Warning, TEXT("窄门场景生成中断：无法创建墙体。")); return; }
		Wall->Tags.Add(ScenarioTag);
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall);
		Wall->SetRootComponent(Box); Wall->AddInstanceComponent(Box);
		Box->SetBoxExtent(FVector(60, 650, 120));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->SetCanEverAffectNavigation(true); Box->RegisterComponent();
		Wall->SetActorLocation(Gate + FVector(Side * 180.0f, 0, 120));
		UStaticMeshComponent* WallVisual = NewObject<UStaticMeshComponent>(Wall);
		WallVisual->SetupAttachment(Box);
		WallVisual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		WallVisual->SetRelativeScale3D(FVector(1.2f, 13.0f, 2.4f));
		WallVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WallVisual->SetCastShadow(false);
		Wall->AddInstanceComponent(WallVisual); WallVisual->RegisterComponent();
	}
	URTSUnitData* Data = Balance->CreateUnitData(TEXT("Swordman"), World);
	if (!Data) { UE_LOG(LogRTS, Warning, TEXT("窄门场景生成中断：Swordman 数据缺失。")); return; }
	TArray<ARTSUnit*> Units;
	for (int32 i = 0; i < 16; ++i)
	{
		const FTransform Transform(Home + FVector((i % 4 - 1.5) * 110, -650 + (i / 4) * 110, 82));
		ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Unit) { UE_LOG(LogRTS, Warning, TEXT("窄门场景生成中断：无法创建第 %d 名单位。"), i + 1); return; }
		Unit->Tags.Add(ScenarioTag);
		Unit->Configure(Data, 0); Unit->FinishSpawning(Transform); Units.Add(Unit);
	}
	URTSOrderLibrary::IssueMoveOrder(World, Units, Home + FVector(0, 1600, 0));
	UE_LOG(LogRTS, Display, TEXT("窄门压力场景已生成：16 名剑士正在穿越位于 X=0、Y=600 的 240cm 通道；碰撞墙与单位仅存在于当前 PIE。"));
}

static FAutoConsoleCommandWithWorld GDenseGateScenarioCommand(
	TEXT("rts.TestDenseGate"), TEXT("在 PIE 中生成并运行 16 人窄门拥堵场景，门宽约 240cm。"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&SpawnDenseGateScenario));
bool FRTSMultiWorkerTest::RunTest(const FString&) { RunCongestion(this, 0); return true; }
bool FRTSLargeCrowdProductionTest::RunTest(const FString&) { RunCongestion(this, 1); return true; }
	bool FRTSDenseGateTest::RunTest(const FString&) { RunCongestion(this, 3); return true; }
bool FRTSNarrowFormationTest::RunTest(const FString&) { RunCongestion(this, 2); return true; }
#endif
