#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSEconomyScenario.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Units/RTSUnit.h"
#include "Buildings/RTSBuilding.h"
#include "Combat/RTSHealthComponent.h"
#include "Combat/RTSHitFeedbackComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InputComponent.h"
#include "Core/RTSHumanController.h"
#include "Debug/RTSDebugSubsystem.h"
#include "Data/RTSBalanceSubsystem.h"
#include "Economy/RTSResourceComponent.h"
#include "Match/RTSMatchSubsystem.h"
#include "UI/RTSDebugWidget.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSHitFeedbackTest, "MiniRTS.Presentation.HitFlash", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSDebugToolsTest, "MiniRTS.Debug.PanelActions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckPresentation : public IAutomationLatentCommand
	{
	public:
		FCheckPresentation(FAutomationTestBase* InTest, bool bInDebug) : Test(InTest), bDebug(bInDebug) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			const double Now = World->GetTimeSeconds();
			FString Error;
			if (Stage == 0)
			{
				if (!RTSEconomyScenario::Create(World, Scenario, Error)) { Test->AddError(Error); return true; }
				Unit = Scenario.Workers[0]; At = Now; ++Stage; return false;
			}
			if (Now - At > 15) { Test->AddError(TEXT("Presentation/debug stage timed out")); return true; }
			if (bDebug)
			{
				if (Now - At < 1.5) { return false; }
				URTSDebugSubsystem* Debug = URTSDebugSubsystem::Get(World);
				URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World);
				ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController());
				bool bBound = false;
				for (const FInputKeyBinding& Binding : PC->InputComponent->KeyBindings) { bBound |= Binding.Chord.Key == EKeys::F3 && Binding.KeyDelegate.IsBound(); }
				Test->TestTrue(TEXT("F3 input bound"), bBound);
				Test->TestFalse(TEXT("Debug panel closed initially"), Debug->IsPanelOpen());
				Debug->TogglePanel(); Test->TestTrue(TEXT("Debug panel toggles open"), Debug->IsPanelOpen());
				Test->TestFalse(TEXT("Viewing does not mark match"), Match->HasDebugBeenUsed());
				Test->TestFalse(TEXT("Unsupported speed rejected"), Debug->SetSpeed(10, Error));
				Test->TestFalse(TEXT("Invalid action does not mark match"), Match->HasDebugBeenUsed());
				URTSResourceComponent* Stock = URTSBalanceSubsystem::Get(World)->GetResources(0);
				Test->TestTrue(TEXT("Debug resource action"), Debug->AddResources(Error));
				Test->TestEqual(TEXT("Exact wood addition"), Stock->GetAmount(ERTSResource::Wood), 900);
				Test->TestEqual(TEXT("Exact food addition"), Stock->GetAmount(ERTSResource::Food), 800);
				Test->TestTrue(TEXT("Mutation marks debug match"), Match->HasDebugBeenUsed());
				const FRTSMatchStats Stats = Match->GetStats(0);
				for (FName Id : {FName("Peasant"), FName("Swordman"), FName("Archer")})
				{
					ARTSUnit* Spawned = Debug->SpawnUnit(Id, Error);
					if (!Spawned) { Test->AddError(FString::Printf(TEXT("Debug spawn %s failed: %s"), *Id.ToString(), *Error)); }
				}
				Test->TestNull(TEXT("Invalid unit rejected"), Debug->SpawnUnit(TEXT("Tower"), Error));
				Test->TestEqual(TEXT("Debug spawn does not fake training stats"), Match->GetStats(0).UnitsTrained, Stats.UnitsTrained);
				URTSDebugWidget* Widget = CreateWidget<URTSDebugWidget>(PC, URTSDebugWidget::StaticClass());
				UFunction* Fast = Widget->FindFunction(TEXT("Fast")); Test->TestNotNull(TEXT("Speed button function"), Fast);
				if (Fast) { Widget->ProcessEvent(Fast, nullptr); }
				Test->TestEqual(TEXT("Button sets double speed"), Debug->GetSpeed(), 2.f);
				Match->ToggleMainMenu();
				Test->TestFalse(TEXT("Menu blocks resource mutations"), Debug->AddResources(Error));
				Match->ToggleMainMenu();
				Test->TestTrue(TEXT("Nondestructive menu retains debug label"), Match->HasDebugBeenUsed());
				Match->ShowMainMenu();
				Test->TestEqual(TEXT("Reset menu restores speed"), Debug->GetSpeed(), 1.f);
				Test->TestFalse(TEXT("Reset menu clears debug label"), Match->HasDebugBeenUsed());
				Test->TestFalse(TEXT("Reset menu closes panel"), Debug->IsPanelOpen());
				if (!Match->StartMatch(ERTSDifficulty::Normal, Error)) { Test->AddError(Error); return true; }
				ARTSUnit* ManagedSpawn = Debug->SpawnUnit(TEXT("Swordman"), Error);
				if (!ManagedSpawn) { Test->AddError(TEXT("Managed debug spawn failed: ") + Error); }
				Test->TestTrue(TEXT("Managed match speed change"), Debug->SetSpeed(0.5f, Error));
				Match->Surrender();
				Test->TestEqual(TEXT("Results restore normal speed"), Debug->GetSpeed(), 1.f);
				Test->TestTrue(TEXT("Results retain debug label"), Match->HasDebugBeenUsed());
				Test->TestFalse(TEXT("Results block spawning"), Debug->SpawnUnit(TEXT("Swordman"), Error) != nullptr);
				return true;
			}
			URTSHitFeedbackComponent* Feedback = Unit->FindComponentByClass<URTSHitFeedbackComponent>();
			if (Stage == 1)
			{
				Original = Unit->GetMesh()->GetMaterial(0);
				Unit->GetHealth()->ResetHealth();
				Test->TestFalse(TEXT("Healing does not flash"), Feedback->IsFlashing());
				Unit->GetHealth()->ApplyDamage(1, Scenario.Hall);
				Test->TestTrue(TEXT("Real damage triggers flash"), Feedback->IsFlashing());
				Test->TestTrue(TEXT("Body material temporarily white"), Unit->GetMesh()->GetMaterial(0) != Original);
				UMaterialInterface* Flash = Unit->GetMesh()->GetMaterial(0);
				Test->TestTrue(TEXT("Flash is unlit, not shadow-darkened grey"), Flash && Flash->GetShadingModels().HasOnlyShadingModel(MSM_Unlit));
				Test->TestTrue(TEXT("Flash uses dedicated project material"), Flash && Flash->GetMaterial()->GetPathName() == TEXT("/Game/RTS/Materials/M_RTSHitFlash.M_RTSHitFlash"));
				UMaterialInstanceDynamic* White = Cast<UMaterialInstanceDynamic>(Flash);
				const FLinearColor WhiteColor = White ? White->K2_GetVectorParameterValue(TEXT("Color")) : FLinearColor::Black;
				Test->TestTrue(TEXT("Neutral white emissive has sufficient intensity"), WhiteColor.R >= 3.f && FMath::IsNearlyEqual(WhiteColor.R, WhiteColor.G) && FMath::IsNearlyEqual(WhiteColor.G, WhiteColor.B));
				for (UStaticMeshComponent* Mesh : TInlineComponentArray<UStaticMeshComponent*>(Unit))
				{ if (Mesh->GetFName() == TEXT("SelectionRing")) { Ring = Mesh; RingMaterial = Mesh->GetMaterial(0); } }
				Unit->GetHealth()->ApplyDamage(1, Scenario.Hall);
				At = Now; ++Stage;
			}
			else if (Stage == 2 && Now - At > 0.25)
			{
				Test->TestFalse(TEXT("Flash timer expired"), Feedback->IsFlashing());
				Test->TestTrue(TEXT("Original team material restored"), Unit->GetMesh()->GetMaterial(0) == Original);
				Test->TestTrue(TEXT("Selection ring unchanged"), !Ring || Ring->GetMaterial(0) == RingMaterial);
				Unit->GetHealth()->ApplyDamage(1, Scenario.Hall);
				Unit->SetGenericTeamId(FGenericTeamId(1)); ChangedTeam = Unit->GetMesh()->GetMaterial(0);
				At = Now; ++Stage;
			}
			else if (Stage == 3 && Now - At > 0.25)
			{
				Test->TestTrue(TEXT("Team change during flash not overwritten"), Unit->GetMesh()->GetMaterial(0) == ChangedTeam);
				URTSHitFeedbackComponent* BuildingFeedback = Scenario.Hall->FindComponentByClass<URTSHitFeedbackComponent>();
				Scenario.Hall->GetHealth()->ApplyDamage(1, Unit);
				Test->TestTrue(TEXT("Building real damage flashes"), BuildingFeedback && BuildingFeedback->IsFlashing());
				At = Now; ++Stage;
			}
			else if (Stage == 4 && Now - At > 0.25)
			{
				Test->TestFalse(TEXT("Building flash restores"), Scenario.Hall->FindComponentByClass<URTSHitFeedbackComponent>()->IsFlashing()); return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		bool bDebug;
		FRTSEconomyScenario Scenario;
		ARTSUnit* Unit = nullptr;
		UMaterialInterface* Original = nullptr;
		UMaterialInterface* ChangedTeam = nullptr;
		UMaterialInterface* RingMaterial = nullptr;
		UStaticMeshComponent* Ring = nullptr;
		int32 Stage = 0;
		double Started = FPlatformTime::Seconds(), At = 0;
	};
	void RunPresentation(FAutomationTestBase* Test, bool bDebug)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FCheckPresentation(Test, bDebug));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
}
bool FRTSHitFeedbackTest::RunTest(const FString&) { RunPresentation(this, false); return true; }
bool FRTSDebugToolsTest::RunTest(const FString&) { RunPresentation(this, true); return true; }
#endif
