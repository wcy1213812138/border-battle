// Opt-in, persistent PIE scenes for manually inspecting movement. No automation is launched.
#if !UE_BUILD_SHIPPING
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Core/RTSHumanController.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Match/RTSMatchSubsystem.h"
#include "Orders/RTSOrderLibrary.h"
#include "Selection/RTSSelectionComponent.h"
#include "Units/RTSUnit.h"

namespace RTSMovementSandbox
{
const FName FixtureTag(TEXT("RTSMovementSandbox"));
const FName MoverTag(TEXT("RTSMovementSandboxMover"));
const FName CompactTag(TEXT("RTSMovementSandboxCompact"));
const FName YieldTag(TEXT("RTSMovementSandboxYield"));
const FName BlockerTag(TEXT("RTSMovementSandboxBlocker"));
const FVector CompactGoal(500, 500, 0);

bool CheckWorld(UWorld* World)
{
	if (World && World->WorldType == EWorldType::PIE && World->HasBegunPlay()
		&& World->GetMapName().EndsWith(TEXT("L_M0Test"))) { return true; }
	UE_LOG(LogRTS, Warning, TEXT("请先打开 L_M0Test 并点击运行 PIE。rts.Test… 是手动场景，不是 Automation RunTests。"));
	return false;
}

void Clear(UWorld* World)
{
	if (ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController()))
	{ PC->GetSelection()->ClearSelection(); }
	TArray<AActor*> Previous;
	for (TActorIterator<AActor> It(World); It; ++It)
	{ if (It->ActorHasTag(FixtureTag)) { Previous.Add(*It); } }
	for (AActor* Actor : Previous) { Actor->Destroy(); }
}

void SelectAndFocus(UWorld* World, const TArray<ARTSUnit*>& Units, const FVector& Focus)
{
	if (ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController()))
	{
		PC->SelectBuilding(nullptr);
		PC->GetSelection()->ClearSelection();
		for (ARTSUnit* Unit : Units) { PC->GetSelection()->SelectSingle(Unit, true); }
		if (APawn* Camera = PC->GetPawn()) { Camera->SetActorLocation(Focus + FVector(0, 0, 90)); }
	}
}

AActor* AddBox(UWorld* World, const FVector& Center, const FVector& Extent, bool bBlocking, FLinearColor Color)
{
	AActor* Actor = World->SpawnActor<AActor>();
	if (!Actor) { return nullptr; }
	Actor->SetFlags(RF_Transient); Actor->Tags.Add(FixtureTag);
	UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
	Actor->SetRootComponent(Box); Actor->AddInstanceComponent(Box);
	Box->SetBoxExtent(Extent);
	Box->SetCollisionProfileName(bBlocking ? TEXT("BlockAll") : TEXT("NoCollision"));
	Box->SetCanEverAffectNavigation(bBlocking); Box->RegisterComponent(); Actor->SetActorLocation(Center);
	UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(Actor);
	Visual->SetupAttachment(Box); Actor->AddInstanceComponent(Visual);
	Visual->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Visual->SetRelativeScale3D(Extent * 0.02);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); Visual->SetCanEverAffectNavigation(false);
	Visual->SetCastShadow(false); Visual->RegisterComponent();
	Visual->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
	if (UMaterialInstanceDynamic* Material = Visual->CreateAndSetMaterialInstanceDynamic(0))
	{ Material->SetVectorParameterValue(TEXT("Color"), Color); }
	return Actor;
}

ARTSUnit* Spawn(UWorld* World, URTSUnitData* Data, const FVector& Position, FName Role)
{
	const FTransform Transform(Position);
	ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (Unit)
	{
		Unit->SetFlags(RF_Transient); Unit->Tags.Add(FixtureTag); Unit->Tags.Add(Role);
		Unit->Configure(Data, RTSTeam::Player); Unit->FinishSpawning(Transform);
	}
	return Unit;
}

TArray<ARTSUnit*> Movers(UWorld* World)
{
	TArray<ARTSUnit*> Units;
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{ if (It->ActorHasTag(FixtureTag) && It->ActorHasTag(MoverTag) && It->IsAlive()) { Units.Add(*It); } }
	Units.Sort([](const ARTSUnit& A, const ARTSUnit& B) { return A.GetUniqueID() < B.GetUniqueID(); });
	return Units;
}

void Compact(UWorld* World, bool bMixed)
{
	if (!CheckWorld(World)) { return; }
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
	URTSUnitData* Sword = Balance ? Balance->CreateUnitData(TEXT("Swordman"), World) : nullptr;
	URTSUnitData* Archer = bMixed && Balance ? Balance->CreateUnitData(TEXT("Archer"), World) : Sword;
	if (!Sword || !Archer) { UE_LOG(LogRTS, Warning, TEXT("场景未生成：单位数据缺失。")); return; }
	if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
	Clear(World);
	AddBox(World, CompactGoal + FVector(-140, -145, 120), FVector(350, 60, 120), true, FLinearColor(0.35f, 0.4f, 0.45f));
	AddBox(World, CompactGoal + FVector(-140, 145, 120), FVector(350, 60, 120), true, FLinearColor(0.35f, 0.4f, 0.45f));
	AddBox(World, CompactGoal + FVector(270, 0, 120), FVector(60, 205, 120), true, FLinearColor(0.35f, 0.4f, 0.45f));
	// Visible, nonblocking cross marks the exact command destination.
	AddBox(World, CompactGoal + FVector(0, 0, 2), FVector(24, 4, 1), false, FLinearColor(0.1f, 1.f, 0.25f));
	AddBox(World, CompactGoal + FVector(0, 0, 2), FVector(4, 24, 1), false, FLinearColor(0.1f, 1.f, 0.25f));
	TArray<ARTSUnit*> Units;
	for (int32 Index = 0; Index < 8; ++Index)
	{
		ARTSUnit* Unit = Spawn(World, bMixed && Index % 2 ? Archer : Sword,
			CompactGoal + FVector(-850 + (Index / 2) * 110, (Index % 2 - 0.5) * 110, 82), MoverTag);
		if (!Unit) { Clear(World); UE_LOG(LogRTS, Warning, TEXT("场景未生成：单位生成失败。")); return; }
		Unit->Tags.Add(CompactTag); Units.Add(Unit);
	}
	SelectAndFocus(World, Units, CompactGoal + FVector(-300, 0, 0));
	UE_LOG(LogRTS, Display, TEXT("手动紧凑场景已生成：%s，通道净宽170cm。单位原地等待；输入 rts.TestGo 前往绿色十字，或自行右键。停稳后输入 rts.TestLeave 观察离队；rts.TestStatus 查看半径/槽位。不会自动结束PIE。"), bMixed ? TEXT("4剑士+4弓手") : TEXT("8剑士"));
}

void Yield(UWorld* World)
{
	if (!CheckWorld(World)) { return; }
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
	URTSUnitData* Data = Balance ? Balance->CreateUnitData(TEXT("Swordman"), World) : nullptr;
	if (!Data) { UE_LOG(LogRTS, Warning, TEXT("场景未生成：单位数据缺失。")); return; }
	if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
	Clear(World);
	ARTSUnit* Requester = Spawn(World, Data, FVector(-500, -120, 82), MoverTag);
	ARTSUnit* Blocker = Spawn(World, Data, FVector(-500, 0, 82), BlockerTag);
	if (!Requester || !Blocker) { Clear(World); return; }
	Requester->Tags.Add(YieldTag);
	AddBox(World, FVector(-500, 500, 2), FVector(24, 24, 1), false, FLinearColor(0.1f, 1.f, 0.25f));
	SelectAndFocus(World, {Requester}, FVector(-500, 150, 0));
	UE_LOG(LogRTS, Display, TEXT("手动让路场景已生成：后方请求者正常行走，前方为空闲友军。输入 rts.TestGo 开始；能绕行则不强制侧移，受阻才协商让路。不会冻结速度。"));
}

void Go(UWorld* World)
{
	if (!CheckWorld(World)) { return; }
	TArray<ARTSUnit*> Units = Movers(World);
	if (Units.IsEmpty()) { UE_LOG(LogRTS, Warning, TEXT("请先输入 rts.TestCompact、rts.TestCompactMixed 或 rts.TestYield。")); return; }
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Nav || Nav->IsNavigationBuildInProgress())
	{ UE_LOG(LogRTS, Warning, TEXT("NavMesh正在更新，请稍后再输入 rts.TestGo。")); return; }
	if (Units[0]->ActorHasTag(CompactTag)) { URTSOrderLibrary::IssueMoveOrder(World, Units, CompactGoal); }
	else
	{
		ARTSUnit* Requester = Units[0];
		FRTSOrder Order; Order.Type = ERTSOrderType::Move; Order.TargetLocation = FVector(-500, 500, 10);
		Requester->ReceiveOrder(Order);
	}
	UE_LOG(LogRTS, Display, TEXT("已发送移动命令。场景会保留；可选单位/下新命令，rts.TestReset 清理。"));
}

void Leave(UWorld* World)
{
	if (!CheckWorld(World)) { return; }
	const TArray<ARTSUnit*> Units = Movers(World);
	if (Units.IsEmpty() || !Units[0]->ActorHasTag(CompactTag))
	{ UE_LOG(LogRTS, Warning, TEXT("rts.TestLeave 用于紧凑场景，请先输入 rts.TestCompact 或 rts.TestCompactMixed。")); return; }
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		FRTSOrder Order; Order.Type = ERTSOrderType::Move;
		Order.TargetLocation = CompactGoal + FVector(-550 + Index * 130, -1100, 10);
		Units[Index]->ReceiveOrder(Order);
	}
	UE_LOG(LogRTS, Display, TEXT("已发送各自离队目标；空间安全后半径会从28恢复到原值，输入 rts.TestStatus 查看。"));
}

void Status(UWorld* World)
{
	if (!CheckWorld(World)) { return; }
	int32 Count = 0;
	for (TActorIterator<ARTSUnit> It(World); It; ++It)
	{
		if (!It->ActorHasTag(FixtureTag)) { continue; }
		++Count;
		UE_LOG(LogRTS, Display, TEXT("%s position=%s radius=%.1f standing=%.1f slot=%d order=%d yields=%u yielding=%d passes=%u pairIgnores=%d"),
			*It->GetName(), *It->GetActorLocation().ToCompactString(), It->GetCapsuleComponent()->GetUnscaledCapsuleRadius(),
			It->GetStandingCollisionRadius(), It->GetFormationSlotIndex(), static_cast<int32>(It->GetCurrentOrder().Type),
			It->GetTrafficYieldCount(), It->IsYieldingToTraffic(), It->GetTrafficPassCount(), It->GetCapsuleComponent()->GetMoveIgnoreActors().Num());
	}
	UE_LOG(LogRTS, Display, TEXT("手动移动场景单位数：%d。"), Count);
}
}

// Opt-in diagnostic: rts.FormationTrace 1 writes Saved/Logs/FormationTrace.csv while a
// group move runs. Off by default, never changes behavior.
static TAutoConsoleVariable<int32> GFormationTrace(TEXT("rts.FormationTrace"), 0,
	TEXT("1 = trace a group move into Saved/Logs/FormationTrace.csv."), ECVF_Default);

#if WITH_EDITOR || UE_BUILD_DEBUG
static FAutoConsoleCommandWithWorld GCompactSandbox(TEXT("rts.TestCompact"), TEXT("生成170cm窄目标与8剑士，不自动移动/结束PIE。rts.TestGo 开始。"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { RTSMovementSandbox::Compact(World, false); }));
static FAutoConsoleCommandWithWorld GCompactMixedSandbox(TEXT("rts.TestCompactMixed"), TEXT("生成170cm窄目标与4剑士4弓手，不自动移动/结束PIE。"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { RTSMovementSandbox::Compact(World, true); }));
static FAutoConsoleCommandWithWorld GYieldSandbox(TEXT("rts.TestYield"), TEXT("生成可手动观察的友军让路场景，rts.TestGo 开始。"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&RTSMovementSandbox::Yield));
static FAutoConsoleCommandWithWorld GMovementSandboxGo(TEXT("rts.TestGo"), TEXT("开始当前手动移动场景，不启动Automation。"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&RTSMovementSandbox::Go));
static FAutoConsoleCommandWithWorld GMovementSandboxLeave(TEXT("rts.TestLeave"), TEXT("紧凑场景单位各自离队，观察碰撞半径恢复。"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&RTSMovementSandbox::Leave));
static FAutoConsoleCommandWithWorld GMovementSandboxStatus(TEXT("rts.TestStatus"), TEXT("输出手动场景单位半径、槽位和让路计数。"),
	FConsoleCommandWithWorldDelegate::CreateStatic(&RTSMovementSandbox::Status));
static FAutoConsoleCommandWithWorld GMovementSandboxReset(TEXT("rts.TestReset"), TEXT("仅清理本批手动移动场景；不改地图，不结束PIE。"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (RTSMovementSandbox::CheckWorld(World)) { RTSMovementSandbox::Clear(World); UE_LOG(LogRTS, Display, TEXT("手动移动场景已清理。")); }
	}));
#endif
#endif
