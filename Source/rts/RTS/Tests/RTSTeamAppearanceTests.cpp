#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Data/RTSUnitData.h"
#include "Units/RTSUnit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSTeamAppearanceTest, "MiniRTS.Appearance.TeamColours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckTeamAppearance : public IAutomationLatentCommand
	{
	public:
		explicit FCheckTeamAppearance(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			URTSBalanceSubsystem* Balance = URTSBalanceSubsystem::Get(World);
			if (!Balance) { Test->AddError(TEXT("Balance unavailable")); return true; }
			int32 Index = 0;
			for (FName Id : {FName("Peasant"), FName("Swordman"), FName("Archer")})
			{
				for (uint8 Team = 0; Team < 2; ++Team)
				{
					const FTransform Transform(FVector(-1500 + Index++ * 180, -1000, 100));
					ARTSUnit* Unit = World->SpawnActorDeferred<ARTSUnit>(ARTSUnit::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					URTSUnitData* Data = Unit ? Balance->CreateUnitData(Id, Unit) : nullptr;
					if (!Data) { Test->AddError(TEXT("Cannot create appearance fixture")); return true; }
					Unit->Configure(Data, Team); Unit->FinishSpawning(Transform);
					auto Check = [&](uint8 ExpectedTeam)
					{
						const int32 Slot = Unit->GetMesh()->GetMaterialIndex(TEXT("TT_RTS_Units"));
						UMaterialInterface* Mat = Slot != INDEX_NONE ? Unit->GetMesh()->GetMaterial(Slot) : nullptr;
						Test->TestEqual(TEXT("Team clothing material"), Mat ? Mat->GetName() : FString(), ExpectedTeam == 0 ? FString("MI_Units_Blue") : FString("MI_Units_Red"));
						UStaticMeshComponent* Band = nullptr;
						TArray<UStaticMeshComponent*> Components; Unit->GetComponents(Components);
						for (UStaticMeshComponent* Component : Components) { if (Component->GetFName() == TEXT("TeamArmband")) { Band = Component; } }
						if (!Test->TestNotNull(TEXT("Worker marker component"), Band)) { return; }
						Test->TestEqual(TEXT("Only workers display armband"), Band->IsVisible(), Id == TEXT("Peasant"));
						Test->TestTrue(TEXT("Marker has no collision/navigation effect"), Band->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Band->CanEverAffectNavigation());
						Test->TestTrue(TEXT("Pawn collision preserved"), Unit->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
						if (Id == TEXT("Peasant"))
						{
							Test->TestTrue(TEXT("Worker marker follows valid arm bone"), Band->GetAttachParent() == Unit->GetMesh()
								&& Band->GetAttachSocketName() == TEXT("Bip001-L-UpperArm") && Unit->GetMesh()->GetBoneIndex(Band->GetAttachSocketName()) != INDEX_NONE);
							UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Band->GetMaterial(0));
							const FLinearColor Color = MID ? MID->K2_GetVectorParameterValue(TEXT("Color")) : FLinearColor::Black;
							Test->TestTrue(TEXT("Worker marker has team colour"), ExpectedTeam == 0 ? Color.B > 0.9f && Color.R < 0.1f : Color.R > 0.9f && Color.B < 0.1f);
						}
					};
					Check(Team);
					Unit->SetGenericTeamId(FGenericTeamId(1 - Team)); Check(1 - Team);
					Unit->SetGenericTeamId(FGenericTeamId(RTSTeam::Neutral));
					Test->TestTrue(TEXT("Neutral restores original material"), Unit->GetMesh()->GetMaterial(0) && Unit->GetMesh()->GetMaterial(0)->GetName() == TEXT("M_Units_Parent"));
					Unit->Destroy();
				}
			}
			return true;
		}
	private:
		FAutomationTestBase* Test;
		double Started = FPlatformTime::Seconds();
	};
}
bool FRTSTeamAppearanceTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckTeamAppearance(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
