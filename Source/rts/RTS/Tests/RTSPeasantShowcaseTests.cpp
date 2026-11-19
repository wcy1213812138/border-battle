#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Units/RTSPeasantShowcase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Orders/RTSOrderLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Data/RTSInfantryCatalog.h"
#include "Data/RTSUnitData.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSPeasantTiersTest, "MiniRTS.Appearance.PeasantTiers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSSwordTiersTest, "MiniRTS.Appearance.SwordmanTiers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSArcherTiersTest, "MiniRTS.Appearance.ArcherTiers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSAllTiersTest, "MiniRTS.Appearance.AllUnitTiers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FRTSAssetInfantryTiersTest, "MiniRTS.Appearance.AssetInfantry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
class FCheckPeasantTiers : public IAutomationLatentCommand
{
public:
	explicit FCheckPeasantTiers(FAutomationTestBase* InTest, FString InCommand = TEXT("rts.TestPeasantTiers"), int32 InExpected = 5)
		: Test(InTest), Command(MoveTemp(InCommand)), Expected(InExpected) {}
	bool Update() override
	{
		UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
		if (!World || !World->HasBegunPlay())
		{ if (FPlatformTime::Seconds() - Started <= 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
		if (World->GetTimeSeconds() < 2) { return false; }
		if (SpawnedAt < 0)
		{
			IConsoleManager::Get().ProcessUserConsoleInput(*Command, *GLog, World);
			int32 SpawnCount = 0; for (TActorIterator<ARTSPeasantShowcase> It(World); It; ++It) { ++SpawnCount; }
			if (SpawnCount == 0 && FPlatformTime::Seconds() - Started < 30) { return false; }
			SpawnedAt = World->GetTimeSeconds(); return false;
		}
		if (World->GetTimeSeconds() - SpawnedAt < 2) { return false; }
		if (MoveAt >= 0)
		{
			if (World->GetTimeSeconds() - MoveAt < 6) { return false; }
			for (TActorIterator<ARTSPeasantShowcase> It(World); It; ++It)
			{
				const FVector Start = Starts[*It];
				const float Distance = FVector::Dist2D(Start, It->GetActorLocation());
				Test->AddInfo(FString::Printf(TEXT("%s Tier %d moved %.1f"), *It->GetPreviewKind().ToString(), It->GetPreviewTier(), Distance));
				Test->TestTrue(TEXT("Every tier reaches movement target"), FVector::Dist2D(Start + FVector(400, 0, 0), It->GetActorLocation()) < 15.1f);
			}
			IConsoleManager::Get().ProcessUserConsoleInput(*Command, *GLog, World);
			int32 Replaced = 0; for (TActorIterator<ARTSPeasantShowcase> It(World); It; ++It) { ++Replaced; }
			Test->TestEqual(TEXT("Repeat replaces prior display"), Replaced, Expected);
			return true;
		}
		int32 Count = 0;
		for (TActorIterator<ARTSPeasantShowcase> It(World); It; ++It) { ++Count; }
		if (Count == 0 && FPlatformTime::Seconds() - Started < 30) { return false; }
		Test->TestEqual(TEXT("Expected tier preview count"), Count, Expected);
		TSet<FString> Tiers;
		TMap<FName, ARTSPeasantShowcase*> Baselines;
		for (TActorIterator<ARTSPeasantShowcase> It(World); It; ++It) { if (It->GetPreviewTier() == 1) { Baselines.Add(It->GetPreviewKind(), *It); } }
		Test->TestEqual(TEXT("Expected unit kinds"), Baselines.Num(), Expected / 5);
		for (TActorIterator<ARTSPeasantShowcase> It(World); It; ++It)
		{
			Tiers.Add(It->GetPreviewKind().ToString() + FString::FromInt(It->GetPreviewTier()));
			ARTSPeasantShowcase* Baseline = Baselines.FindRef(It->GetPreviewKind());
			if (!Baseline) { Test->AddError(TEXT("Missing unit kind baseline")); continue; }
			Test->TestTrue(TEXT("Model loaded for unit kind"), It->GetMesh()->GetSkeletalMeshAsset() && It->GetMesh()->GetSkeletalMeshAsset() == Baseline->GetMesh()->GetSkeletalMeshAsset());
			const URTSUnitData* UnitData = It->GetUnitData();
			const URTSUnitData* PlainData = Baseline->GetUnitData();
			Test->TestTrue(TEXT("Unit data matches requested kind"), UnitData && UnitData->BalanceId == It->GetPreviewKind());
			if (UnitData && PlainData)
			{
				if (It->GetPreviewKind() == TEXT("Scout"))
				{
					Test->TestTrue(TEXT("Scout uses own shortbow mesh and archer attack"), UnitData->Mesh->GetName() == TEXT("SK_Scout") && UnitData->AttackAnim->GetName() == TEXT("Anim_archer_04_attack_A"));
					Test->TestTrue(TEXT("Scout is ranged"), UnitData->bRangedAttack && UnitData->AttackRange == 450);
				}
				if (It->GetPreviewKind() == TEXT("Priest") || It->GetPreviewKind() == TEXT("HighPriest"))
				{
					Test->TestTrue(TEXT("Priests heal with staff casting and no damage"), UnitData->bHealer && !UnitData->bRangedAttack && UnitData->AttackDamage == 0 && UnitData->HealAmount > 0 && UnitData->AttackAnim->GetName() == TEXT("Anim_staff_07_cast_A"));
				}
				Test->TestEqual(TEXT("Tier health unchanged"), UnitData->MaxHealth, PlainData->MaxHealth);
				Test->TestEqual(TEXT("Tier damage unchanged"), UnitData->AttackDamage, PlainData->AttackDamage);
				Test->TestEqual(TEXT("Tier range unchanged"), UnitData->AttackRange, PlainData->AttackRange);
				for (UAnimSequence* Anim : {UnitData->IdleAnim.Get(), UnitData->MoveAnim.Get(), UnitData->AttackAnim.Get(), UnitData->DeathAnim.Get()})
				{
					Test->TestTrue(TEXT("Required animation loads and has duration"), Anim && Anim->GetPlayLength() > 0);
					if (Anim && UnitData->Mesh) { Test->TestTrue(TEXT("Animation skeleton matches mesh"), Anim->GetSkeleton() == UnitData->Mesh->GetSkeleton()); }
				}
			}
			const UCapsuleComponent* Capsule = It->GetCapsuleComponent();
			const UCapsuleComponent* Plain = Baseline->GetCapsuleComponent();
			Test->TestEqual(TEXT("Tier capsule radius unchanged"), Capsule->GetUnscaledCapsuleRadius(), Plain->GetUnscaledCapsuleRadius());
			Test->TestEqual(TEXT("Tier capsule height unchanged"), Capsule->GetUnscaledCapsuleHalfHeight(), Plain->GetUnscaledCapsuleHalfHeight());
			Test->TestTrue(TEXT("Tier collision settings unchanged"), Capsule->GetCollisionProfileName() == Plain->GetCollisionProfileName()
				&& Capsule->GetCollisionEnabled() == Plain->GetCollisionEnabled()
				&& Capsule->GetCollisionResponseToChannels() == Plain->GetCollisionResponseToChannels());
			Test->TestEqual(TEXT("Tier move speed unchanged"), It->GetCharacterMovement()->MaxWalkSpeed, Baseline->GetCharacterMovement()->MaxWalkSpeed);
			Test->TestTrue(TEXT("Head bone exists"), It->GetMesh()->GetBoneIndex(TEXT("Bip001-Head")) != INDEX_NONE);
			Test->TestTrue(TEXT("Cape bone exists"), It->GetMesh()->GetBoneIndex(TEXT("Bip001-Neck")) != INDEX_NONE);
			TArray<UStaticMeshComponent*> Parts; It->GetComponents(Parts);
			int32 Decorations = 0;
			TSet<UMaterialInterface*> Materials;
			for (UStaticMeshComponent* Part : Parts)
			{
				if (!Part->GetName().StartsWith(TEXT("TierPart_"))) { continue; }
				++Decorations;
				Test->TestTrue(TEXT("Decoration mesh loaded"), Part->GetStaticMesh() != nullptr);
				Test->TestTrue(TEXT("Decoration cannot block or affect navigation"), Part->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Part->CanEverAffectNavigation());
				Test->TestTrue(TEXT("No root-attached halo decoration"), Part->GetAttachParent() != It->GetRootComponent());
				Materials.Add(Part->GetMaterial(0));
				Test->TestFalse(TEXT("Preview decoration casts no shadow"), Part->CastShadow);
				Test->TestTrue(TEXT("Decoration remains near body after animation"), FVector::Dist(Part->GetComponentLocation(), It->GetActorLocation()) < 180);
				const FVector Scale = Part->GetComponentScale().GetAbs();
				Test->TestTrue(TEXT("Decoration world scale is not lost or exploded"), Scale.GetMax() > 0.015 && Scale.GetMax() < 1);
			}
			Test->TestTrue(TEXT("Tier has intended decoration presence"), It->GetPreviewTier() == 1 ? Decorations == 0 : Decorations > 0);
			Test->TestTrue(TEXT("Decorations use shared materials"), Materials.Num() <= 6);
			Test->TestTrue(TEXT("Preview component budget"), Decorations <= 48);
			TArray<USceneComponent*> Frames; It->GetComponents(Frames);
			for (USceneComponent* Frame : Frames)
			{
				if (!Frame->GetName().StartsWith(TEXT("Preview"))) { continue; }
				Test->TestTrue(TEXT("Accessory frame keeps centimetre scale"), Frame->GetComponentScale().Equals(FVector::OneVector, 0.01));
				Test->AddInfo(FString::Printf(TEXT("Tier %d %s world=%s"), It->GetPreviewTier(), *Frame->GetName(), *Frame->GetComponentLocation().ToCompactString()));
			}
			Test->TestTrue(TEXT("Unit collision preserved"), It->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
		}
		Test->TestEqual(TEXT("Unique kind/tier pairs"), Tiers.Num(), Expected);
		for (TActorIterator<ARTSPeasantShowcase> It(World); It; ++It)
		{
			Starts.Add(*It, It->GetActorLocation());
			FRTSOrder Order; Order.Type = ERTSOrderType::Move; Order.TargetLocation = It->GetActorLocation() + FVector(400, 0, -82);
			It->ReceiveOrder(Order);
		}
		MoveAt = World->GetTimeSeconds(); return false;
	}
private:
	FAutomationTestBase* Test;
	double Started = FPlatformTime::Seconds();
	double SpawnedAt = -1;
	double MoveAt = -1;
	FString Command;
	int32 Expected;
	TMap<ARTSPeasantShowcase*, FVector> Starts;
};
}
bool FRTSPeasantTiersTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckPeasantTiers(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
bool FRTSSwordTiersTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckPeasantTiers(this, TEXT("rts.TestSwordmanTiers")));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
bool FRTSArcherTiersTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckPeasantTiers(this, TEXT("rts.TestArcherTiers")));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
bool FRTSAllTiersTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckPeasantTiers(this, TEXT("rts.TestUnitTiers"), 15));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
void FRTSAssetInfantryTiersTest::GetTests(TArray<FString>& Names, TArray<FString>& Commands) const
{
	for (const auto& Entry : RTSInfantryCatalog::Entries()) { Names.Add(Entry.Id); Commands.Add(Entry.Id); }
}
bool FRTSAssetInfantryTiersTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckPeasantTiers(this, TEXT("rts.TestInfantryTiers ") + Parameters));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
