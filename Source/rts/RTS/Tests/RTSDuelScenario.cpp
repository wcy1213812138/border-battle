#include "Tests/RTSDuelScenario.h"
#include "Tests/RTSCombatScenario.h"
#include "Units/RTSPeasantShowcase.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Data/RTSInfantryCatalog.h"
#include "Core/RTSTypes.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Combat/RTSHealthComponent.h"

bool RTSDuelScenario::Create(UWorld* World, FName Left, FName Right, int32 Tier, TArray<ARTSUnit*>& OutUnits, FString& Error)
{
	OutUnits.Reset();
#if !UE_BUILD_SHIPPING
	if (!World || World->WorldType != EWorldType::PIE || !World->HasBegunPlay()
		|| !World->GetMapName().EndsWith(TEXT("L_M0Test"))) { Error = TEXT("请在L_M0Test运行PIE后使用。"); return false; }
	if (!RTSInfantryCatalog::Find(Left) || !RTSInfantryCatalog::Find(Right) || Left == Right || Tier < 1 || Tier > 5)
	{ Error = TEXT("需要两个不同的步兵ID，以及1..5外观等级。"); return false; }
	URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
	URTSUnitData* Data[2] = {Balance ? Balance->CreateUnitData(Left, World) : nullptr, Balance ? Balance->CreateUnitData(Right, World) : nullptr};
	if (!Data[0] || !Data[1]) { Error = TEXT("兵种数据或动画不可用。"); return false; }
	for (URTSUnitData* Definition : Data) { Definition->bAutoAcquireTargets = false; }
	// Reuse the established full-capsule combat corridor validation and sandbox cleanup.
	TArray<ARTSUnit*> Placeholders;
	if (!RTSCombatScenario::Create(World, 1, 0, Placeholders, Error)) { return false; }
	if (Placeholders.Num() != 2) { Error = TEXT("对战场地生成失败。"); return false; }
	FTransform Transforms[2] = {Placeholders[0]->GetActorTransform(), Placeholders[1]->GetActorTransform()};
	for (ARTSUnit* Placeholder : Placeholders) { Placeholder->Destroy(); }
	for (int32 Side = 0; Side < 2; ++Side)
	{
		ARTSPeasantShowcase* Unit = World->SpawnActorDeferred<ARTSPeasantShowcase>(ARTSPeasantShowcase::StaticClass(), Transforms[Side],
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Unit)
		{
			for (ARTSUnit* Existing : OutUnits) { Existing->Destroy(); }
			OutUnits.Reset(); Error = TEXT("生成对战单位失败。"); return false;
		}
		Unit->SetFlags(RF_Transient); Unit->Tags.Add(TEXT("RTSDuelPreview"));
		Unit->SetPreviewKind(Side == 0 ? Left : Right); Unit->SetPreviewTier(Tier);
		Unit->Configure(Data[Side], Side == 0 ? RTSTeam::Player : RTSTeam::Enemy);
		Unit->FinishSpawning(Transforms[Side]); Unit->SetCombatStance(ERTSCombatStance::Guard);
		OutUnits.Add(Unit);
	}
	ARTSUnit* Fighters[2] = {OutUnits[0], OutUnits[1]};
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (!Data[Side]->bHealer) { continue; }
		URTSUnitData* EscortData = Balance->CreateUnitData(TEXT("Swordman"), World);
		if (!EscortData) { Error = TEXT("治疗支援前排数据不可用。"); return false; }
		EscortData->bAutoAcquireTargets = false;
		const FVector Towards = (Transforms[1 - Side].GetLocation() - Transforms[Side].GetLocation()).GetSafeNormal2D();
		const FTransform EscortTransform(Transforms[Side].GetRotation(), Transforms[Side].GetLocation() + Towards * 160);
		ARTSUnit* Escort = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), EscortTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Escort) { Error = TEXT("治疗支援前排生成失败。"); return false; }
		Escort->SetFlags(RF_Transient); Escort->Configure(EscortData, Side == 0 ? RTSTeam::Player : RTSTeam::Enemy);
		Escort->FinishSpawning(EscortTransform); Escort->Tags.Add(TEXT("RTSDuelEscort"));
		Escort->GetHealth()->ApplyDamage(30, nullptr); // Visible wounded ally for the first cast.
		Fighters[Side] = Escort; OutUnits.Add(Escort);
	}
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FRTSOrder Order; Order.Type = ERTSOrderType::Attack; Order.TargetActor = Fighters[1 - Side];
		Fighters[Side]->ReceiveOrder(Order);
	}
	const FString Caption = FString::Printf(TEXT("%s VS %s | 外观%d级 | 牧师为治疗支援（附剑士前排），其他兵种1对1。rts.TestDuelNext下一组。"),
		RTSInfantryCatalog::Find(Left)->Label, RTSInfantryCatalog::Find(Right)->Label, Tier);
	if (ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController()))
	{
		PC->SelectBuilding(nullptr);
		if (PC->GetSelection()) { PC->GetSelection()->ClearSelection(); PC->GetSelection()->SelectSingle(OutUnits[0], false); }
		if (APawn* Camera = PC->GetPawn())
		{
			Camera->SetActorLocation((OutUnits[0]->GetActorLocation() + OutUnits[1]->GetActorLocation()) * 0.5);
			if (USpringArmComponent* Arm = Camera->FindComponentByClass<USpringArmComponent>()) { Arm->TargetArmLength = 1500; }
		}
		PC->SetEconomyMessage(Caption);
	}
	UE_LOG(LogRTS, Display, TEXT("%s。无自动结束PIE；每次命令替换当前战斗。"), *Caption);
	return true;
#else
	Error = TEXT("仅开发场景可用。"); return false;
#endif
}

#if WITH_EDITOR || UE_BUILD_DEBUG
namespace
{
void RunDuel(UWorld* World, FName Left, FName Right, int32 Tier)
{
	TArray<ARTSUnit*> Units; FString Error;
	if (!RTSDuelScenario::Create(World, Left, Right, Tier, Units, Error)) { UE_LOG(LogRTS, Warning, TEXT("%s"), *Error); }
}
}
static FAutoConsoleCommandWithWorldAndArgs GDuel(TEXT("rts.TestDuel"),
	TEXT("rts.TestDuel [LeftId RightId [Tier]]，默认Swordman Spearman 1。替换当前沙盒单位并自动1对1，不结束PIE。"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		int32 Tier = 1;
		if (Args.IsEmpty()) { RunDuel(World, TEXT("Swordman"), TEXT("Spearman"), Tier); return; }
		if ((Args.Num() != 2 && Args.Num() != 3) || (Args.Num() == 3 && !LexTryParseString(Tier, *Args[2])))
		{ UE_LOG(LogRTS, Warning, TEXT("用法：rts.TestDuel Swordman Spearman 3")); return; }
		RunDuel(World, FName(*Args[0]), FName(*Args[1]), Tier);
	}));
static FAutoConsoleCommandWithWorld GDuelNext(TEXT("rts.TestDuelNext"), TEXT("循环八组不同步兵1对1；不自动切组。"),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!World || World->WorldType != EWorldType::PIE) { return; }
		const TCHAR* Pairs[][2] = {
			{TEXT("Swordman"), TEXT("Spearman")}, {TEXT("Archer"), TEXT("Crossbowman")},
			{TEXT("Light_Infantry"), TEXT("Heavy_Infantry")}, {TEXT("HeavySwordman"), TEXT("Halberdier")},
			{TEXT("Mage"), TEXT("Priest")}, {TEXT("Paladin"), TEXT("HighPriest")},
			{TEXT("Commander"), TEXT("King")}, {TEXT("Peasant"), TEXT("Scout")}};
		static TWeakObjectPtr<UWorld> LastWorld;
		static int32 Index = 0;
		if (LastWorld.Get() != World) { LastWorld = World; Index = 0; }
		RunDuel(World, Pairs[Index][0], Pairs[Index][1], 1); Index = (Index + 1) % UE_ARRAY_COUNT(Pairs);
	}));
#endif
