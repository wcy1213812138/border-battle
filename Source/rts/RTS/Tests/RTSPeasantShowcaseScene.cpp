#if WITH_EDITOR || UE_BUILD_DEBUG
#include "Units/RTSPeasantShowcase.h"
#include "Core/RTSTypes.h"
#include "Core/RTSHumanController.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Match/RTSMatchSubsystem.h"
#include "Selection/RTSSelectionComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "NavigationSystem.h"
#include "GameFramework/SpringArmComponent.h"
#include "NavigationPath.h"
#include "Data/RTSInfantryCatalog.h"

namespace
{
void ShowUnitTiers(UWorld* World, FName Kind)
{
	if (!World || World->WorldType != EWorldType::PIE || !World->HasBegunPlay() || !World->GetMapName().EndsWith(TEXT("L_M0Test")))
	{ UE_LOG(LogRTS, Warning, TEXT("请在L_M0Test运行PIE后输入外观展示命令。")); return; }
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Balance || !Nav) { return; }
	const TArray<FName> Kinds = Kind.IsNone() ? TArray<FName>{TEXT("Peasant"), TEXT("Swordman"), TEXT("Archer")} : TArray<FName>{Kind};
	TArray<URTSUnitData*> Data;
	TArray<FVector> Points;
	// Validate every row before replacing the previous preview.
	for (int32 Row = 0; Row < Kinds.Num(); ++Row)
	{
		URTSUnitData* UnitData = Balance->CreateUnitData(Kinds[Row], World);
		if (!UnitData) { return; }
		Data.Add(UnitData);
		for (int32 Tier = 0; Tier < 5; ++Tier)
		{
			FNavLocation P;
			const FVector Wanted(-1400 - Row * 300, -1500 + Tier * 260, 0);
			if (!Nav->ProjectPointToNavigation(Wanted, P, FVector(100, 100, 500)))
			{ UE_LOG(LogRTS, Warning, TEXT("展示区导航未就绪，请稍后重试。")); return; }
			UNavigationPath* Path = Nav->FindPathToLocationSynchronously(World, P.Location, P.Location + FVector(400, 0, 0));
			if (!Path || !Path->IsValid() || Path->IsPartial())
			{ UE_LOG(LogRTS, Warning, TEXT("展示区缺少完整出口，请稍后重试。")); return; }
			Points.Add(P.Location);
		}
	}
	if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World)) { Match->EnterSandbox(); }
	TArray<ARTSPeasantShowcase*> Old;
	for (TActorIterator<ARTSPeasantShowcase> It(World); It; ++It) { Old.Add(*It); }
	for (ARTSPeasantShowcase* Unit : Old) { Unit->Destroy(); }
	ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController());
	if (PC) { PC->SelectBuilding(nullptr); if (PC->GetSelection()) { PC->GetSelection()->ClearSelection(); } }
	for (int32 Row = 0; Row < Kinds.Num(); ++Row)
	{
		for (int32 Tier = 0; Tier < 5; ++Tier)
		{
			const FTransform Transform(FRotator(0, 180, 0), Points[Row * 5 + Tier] + FVector(0, 0, Data[Row]->CapsuleHalfHeight + 2));
			ARTSPeasantShowcase* Unit = World->SpawnActorDeferred<ARTSPeasantShowcase>(ARTSPeasantShowcase::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Unit) { continue; }
			Unit->SetFlags(RF_Transient); Unit->Tags.Add(TEXT("RTSPeasantTierPreview"));
			Unit->SetPreviewKind(Kinds[Row]); Unit->SetPreviewTier(Tier + 1);
			Unit->Configure(Data[Row], RTSTeam::Player); Unit->FinishSpawning(Transform);
			Unit->SetCombatStance(ERTSCombatStance::Guard);
		}
	}
	if (PC && PC->GetPawn())
	{
		PC->GetPawn()->SetActorLocation((Points[2] + Points[Points.Num() - 3]) * 0.5 + FVector(0, 0, 90));
		if (USpringArmComponent* Arm = PC->GetPawn()->FindComponentByClass<USpringArmComponent>()) { Arm->TargetArmLength = Kinds.Num() > 1 ? 2600 : 1800; }
		PC->SetEconomyMessage(TEXT("五级衣饰展示：工人旅行装备 / 剑士重装护胸 / 弓手箭囊轻装。无光环，数值与碰撞不变；滚轮近看，可框选移动，不自动结束PIE。"));
	}
	UE_LOG(LogRTS, Display, TEXT("衣饰展示生成%d兵种，每种五级。仅外观，不改变单位碰撞或数值。"), Kinds.Num());
}
}
static FAutoConsoleCommandWithWorld GPeasantTierScene(TEXT("rts.TestPeasantTiers"), TEXT("工人五级衣饰。"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { ShowUnitTiers(World, TEXT("Peasant")); }));
static FAutoConsoleCommandWithWorld GSwordTierScene(TEXT("rts.TestSwordmanTiers"), TEXT("剑士五级衣饰。"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { ShowUnitTiers(World, TEXT("Swordman")); }));
static FAutoConsoleCommandWithWorld GArcherTierScene(TEXT("rts.TestArcherTiers"), TEXT("弓箭手五级衣饰。"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { ShowUnitTiers(World, TEXT("Archer")); }));
static FAutoConsoleCommandWithWorld GAllTierScene(TEXT("rts.TestUnitTiers"), TEXT("全部三个兵种，各五级衣饰。"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { ShowUnitTiers(World, NAME_None); }));
static FAutoConsoleCommandWithWorldAndArgs GInfantryTierScene(TEXT("rts.TestInfantryTiers"),
	TEXT("rts.TestInfantryTiers <Id>：查看素材步兵五级；不传参数列出全部16种。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const RTSInfantryCatalog::FEntry* Entry = Args.Num() == 1 ? RTSInfantryCatalog::Find(FName(*Args[0])) : nullptr;
		if (!Entry)
		{
			for (const auto& Item : RTSInfantryCatalog::Entries()) { UE_LOG(LogRTS, Display, TEXT("%s : %s"), Item.Id, Item.Label); }
			return;
		}
		ShowUnitTiers(World, Entry->Id);
	}));
#endif
