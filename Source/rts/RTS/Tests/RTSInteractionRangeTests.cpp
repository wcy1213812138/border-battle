#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSEconomyScenario.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Units/RTSUnit.h"
#include "Economy/RTSGathererComponent.h"
#include "Economy/RTSResourceNode.h"
#include "Orders/RTSOrderLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/BoxComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSFoodReachTest, "MiniRTS.Economy.InteractionRange.Food", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSWoodReachTest, "MiniRTS.Economy.InteractionRange.Wood", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckWorkReach : public IAutomationLatentCommand
	{
	public:
		FCheckWorkReach(FAutomationTestBase* InTest, bool bInFood) : Test(InTest), bFood(bInFood) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			const double Now = World->GetTimeSeconds();
			if (!Unit)
			{
				FString Error;
				if (!RTSEconomyScenario::Create(World, Scenario, Error)) { Test->AddError(Error); return true; }
				Unit = Scenario.Workers[1]; Start = Unit->GetActorLocation();
				Direction = (Scenario.Food->GetActorLocation() - Start).GetSafeNormal2D();
				const float Distance = bFood ? 220.f : 175.f; // Old maximum centre distance was 130 cm.
				const FTransform Transform(Start + Direction * Distance + FVector(0, 0, 8));
				Node = World->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Node->Configure(bFood ? ERTSResource::Food : ERTSResource::Wood, 20); Node->FinishSpawning(Transform);
				Speed = Unit->GetCharacterMovement()->MaxWalkSpeed;
				Unit->GetCharacterMovement()->MaxWalkSpeed = 0; // Prove reach, not merely a closer arrival.
				At = SetupAt = Now; return false;
			}
			if (Now - SetupAt > 20) { Unit->GetCharacterMovement()->MaxWalkSpeed = Speed; Test->AddError(FString::Printf(TEXT("Interaction range stage %d timed out: %s"), Stage, *Unit->GetGatherer()->GetStatus())); return true; }
			if (Stage == 0 && Now - At > 1.5)
			{
				URTSOrderLibrary::IssueWorkOrder({Unit}, Node); ++Stage; At = Now;
			}
			else if (Stage == 1 && Unit->GetGatherer()->GetCarriedAmount() > 0)
			{
				Test->TestTrue(TEXT("Actually gathered outside old interaction range"), FVector::Dist2D(Unit->GetActorLocation(), Node->GetActorLocation()) > 160);
				Test->TestTrue(TEXT("No movement needed within enlarged reach"), FVector::Dist2D(Start, Unit->GetActorLocation()) < 5);
				SavedCargo = Unit->GetGatherer()->GetCarriedAmount();
				Wall = World->SpawnActor<AActor>();
				UBoxComponent* Box = NewObject<UBoxComponent>(Wall); Wall->SetRootComponent(Box); Wall->AddInstanceComponent(Box);
				Box->SetBoxExtent(FVector(40, 40, 140)); Box->SetCollisionProfileName(TEXT("BlockAll"));
				Box->SetCanEverAffectNavigation(true); Box->RegisterComponent();
				Wall->SetActorLocation((Start + Node->GetActorLocation()) * 0.5f);
				++Stage; At = Now;
			}
			else if (Stage == 2 && Now - At > 1.3)
			{
				Test->TestEqual(TEXT("Cannot gather through solid obstacle"), Unit->GetGatherer()->GetCarriedAmount(), SavedCargo);
				Wall->Destroy();
				// A fresh resource outside even the enlarged range must not be remotely harvested.
				const FTransform Transform(Start + Direction * 600 + FVector(0, 0, 8));
				FarNode = World->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				FarNode->Configure(bFood ? ERTSResource::Food : ERTSResource::Wood, 20); FarNode->FinishSpawning(Transform);
				URTSOrderLibrary::IssueWorkOrder({Unit}, FarNode); ++Stage; At = Now;
			}
			else if (Stage == 3 && Now - At > 1.5)
			{
				Test->TestEqual(TEXT("Far resource is not remotely harvested"), FarNode->GetRemaining(), 20);
				Test->TestEqual(TEXT("Far order preserves existing cargo"), Unit->GetGatherer()->GetCarriedAmount(), SavedCargo);
				Unit->GetCharacterMovement()->MaxWalkSpeed = Speed;
				return true;
			}
			return false;
		}
	private:
		FAutomationTestBase* Test;
		bool bFood;
		FRTSEconomyScenario Scenario;
		ARTSUnit* Unit = nullptr;
		ARTSResourceNode* Node = nullptr;
		ARTSResourceNode* FarNode = nullptr;
		AActor* Wall = nullptr;
		FVector Start = FVector::ZeroVector, Direction = FVector::ZeroVector;
		float Speed = 350;
		int32 Stage = 0, SavedCargo = 0;
		double Started = FPlatformTime::Seconds(), At = 0, SetupAt = 0;
	};
	void RunReachTest(FAutomationTestBase* Test, bool bFood)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		ADD_LATENT_AUTOMATION_COMMAND(FCheckWorkReach(Test, bFood));
		ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	}
}
bool FRTSFoodReachTest::RunTest(const FString&) { RunReachTest(this, true); return true; }
bool FRTSWoodReachTest::RunTest(const FString&) { RunReachTest(this, false); return true; }
#endif
