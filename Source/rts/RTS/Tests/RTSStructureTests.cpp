#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSResourceNode.h"
#include "Combat/RTSHealthComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Match/RTSMatchSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/HUD.h"
#include "Engine/StaticMeshActor.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSStructuresTest, "MiniRTS.Presentation.StructuresAndResources", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckStructures : public IAutomationLatentCommand
	{
	public:
		explicit FCheckStructures(FAutomationTestBase* InTest) : Test(InTest) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			const double Now = World->GetRealTimeSeconds();
			if (Stage == 0)
			{
				URTSMatchSubsystem::Get(World)->EnterSandbox();
				const FName Ids[] = {TEXT("TownHall"), TEXT("Barracks"), TEXT("Farm"), TEXT("Tower")};
				for (int32 Team = 0; Team < 2; ++Team)
				{
					for (int32 i = 0; i < 4; ++i)
					{
						// Outside the gameplay area for an isolated art inspection; never saved.
						const FTransform Transform(FVector(-660 + i * 440, -Team * 440, 380));
						ARTSBuilding* Building = World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
						if (!Building) { Test->AddError(TEXT("Showcase building spawn failed")); return true; }
						Building->Configure(Ids[i], Team, true); Building->FinishSpawning(Transform); Buildings.Add(Building);
						UBoxComponent* Collision = Building->FindComponentByClass<UBoxComponent>();
						Test->TestTrue(TEXT("Original solid collision extent preserved"), Collision && Collision->GetUnscaledBoxExtent().Equals(ARTSBuilding::CollisionExtent(Ids[i], Building->GetFootprint())));
						for (UStaticMeshComponent* Mesh : TInlineComponentArray<UStaticMeshComponent*>(Building))
						{
							if (Mesh->GetFName() != TEXT("Body") && Mesh->GetFName() != TEXT("Roof")) { continue; }
							Test->TestTrue(TEXT("Project structure mesh assigned"), Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Game/RTS/Structures/")));
							Test->TestTrue(TEXT("Art cannot change collision/navigation"), Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Mesh->CanEverAffectNavigation());
							for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot) { Test->TestNotNull(TEXT("Every material slot assigned"), Mesh->GetMaterial(Slot)); }
							if (Mesh->GetFName() == TEXT("Roof"))
							{
								const int32 Slot = Mesh->GetMaterialIndex(TEXT("team"));
								UMaterialInstanceDynamic* Material = Slot >= 0 ? Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(Slot)) : nullptr;
								Test->TestNotNull(TEXT("Runtime team material assigned"), Material);
								if (Material) { const FLinearColor Color = Material->K2_GetVectorParameterValue(TEXT("Color")); Test->TestTrue(TEXT("Blue player / red enemy"), Team == 0 ? Color.B > Color.R * 2 : Color.R > Color.B * 2); }
							}
						}
					}
				}
				const FTransform BuildTransform(FVector(0, 500, 380));
				Construction = World->SpawnActorDeferred<ARTSBuilding>(ARTSBuilding::StaticClass(), BuildTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Construction->Configure(TEXT("TownHall"), 0, false); Construction->FinishSpawning(BuildTransform);
				UStaticMeshComponent* Body = Find(Construction, TEXT("Body"));
				UStaticMeshComponent* ConstructionRoof = Find(Construction, TEXT("Roof"));
				const float InitialHeight = Body->GetRelativeScale3D().Z;
				Test->TestFalse(TEXT("Roof waits for the wall stage"), ConstructionRoof->IsVisible());
				Construction->AddConstructionWork(6);
				Test->TestTrue(TEXT("Walls rise during construction"), !Construction->IsComplete() && Body->GetRelativeScale3D().Z > InitialHeight && Body->GetRelativeScale3D().Z < 1);
				Test->TestTrue(TEXT("Roof appears and grows in a later stage"), ConstructionRoof->IsVisible() && ConstructionRoof->GetRelativeScale3D().Z > 0 && ConstructionRoof->GetRelativeScale3D().Z < 1);
				Construction->AddConstructionWork(100);
				Test->TestTrue(TEXT("Completed building has full walls and roof"), Construction->IsComplete() && FMath::IsNearlyEqual(Body->GetRelativeScale3D().Z, 1.0) && FMath::IsNearlyEqual(ConstructionRoof->GetRelativeScale3D().Z, 1.0));
				for (int32 Type = 0; Type < 2; ++Type)
				{
					const FTransform Transform(FVector(-350 + Type * 220, -900, 470));
					ARTSResourceNode* Resource = World->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
					Resource->Configure(Type == 0 ? ERTSResource::Wood : ERTSResource::Food, 10); Resource->FinishSpawning(Transform);
					UStaticMeshComponent* Mesh = Resource->FindComponentByClass<UStaticMeshComponent>();
					Test->TestTrue(TEXT("Resource artwork grounded at capsule feet"), Mesh && Mesh->GetRelativeLocation().Equals(FVector(0, 0, -90)));
					Test->TestTrue(TEXT("Resource model loaded"), Mesh && Mesh->GetStaticMesh()->GetPathName().StartsWith(TEXT("/Game/RTS/Structures/")));
					Test->TestTrue(TEXT("Resource collision capsule preserved"), FMath::IsNearlyEqual(Resource->FindComponentByClass<UCapsuleComponent>()->GetUnscaledCapsuleRadius(), 45.f));
					if (Type == 0)
					{ Test->TestEqual(TEXT("Finite resource extraction"), Resource->Extract(20), 10); Test->TestTrue(TEXT("Exhaustion hides all art and disables collision"), Resource->IsHidden() && !Resource->GetActorEnableCollision()); }
					else { Resource->SetRenewable(true); Resource->Extract(20); Test->TestTrue(TEXT("Recoverable art remains visible when empty"), Resource->GetRemaining() == 0 && !Resource->IsHidden() && Resource->GetActorEnableCollision()); }
				}
				Roof = Find(Buildings[0], TEXT("Roof"));
				for (int32 Slot = 0; Slot < Roof->GetNumMaterials(); ++Slot) { Materials.Add(Roof->GetMaterial(Slot)); }
				Buildings[0]->GetHealth()->ApplyDamage(1, Buildings[4]);
				for (int32 Slot = 0; Slot < Roof->GetNumMaterials(); ++Slot) { Test->TestTrue(TEXT("All building slots flash"), Roof->GetMaterial(Slot) != Materials[Slot]); }
				At = Now; ++Stage; return false;
			}
			if (Stage == 1 && Now - At > .25)
			{
				for (int32 Slot = 0; Slot < Roof->GetNumMaterials(); ++Slot) { Test->TestTrue(TEXT("All roof materials restored"), Roof->GetMaterial(Slot) == Materials[Slot]); }
				Buildings[0]->GetHealth()->ApplyDamage(1, Buildings[4]); Buildings[0]->SetGenericTeamId(FGenericTeamId(1));
				ChangedTeam = Roof->GetMaterial(Roof->GetMaterialIndex(TEXT("team")));
				At = Now; ++Stage; return false;
			}
			if (Stage == 2 && Now - At > .25)
			{
				Test->TestTrue(TEXT("Team replacement during flash not reverted"), Roof->GetMaterial(Roof->GetMaterialIndex(TEXT("team"))) == ChangedTeam);
				Buildings[0]->SetGenericTeamId(FGenericTeamId(0)); Construction->Destroy();
				if (!FParse::Param(FCommandLine::Get(), TEXT("RTSCaptureStructures"))) { return true; }
				World->GetFirstPlayerController()->ClientSetHUD(AHUD::StaticClass());
				AStaticMeshActor* Platform = World->SpawnActor<AStaticMeshActor>(FVector(0, -300, 355), FRotator::ZeroRotator);
				Platform->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Platform->GetStaticMeshComponent()->SetCanEverAffectNavigation(false);
				Platform->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Platform->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
				Platform->GetStaticMeshComponent()->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RTS/Structures/MI_Structure_stone.MI_Structure_stone")));
				Platform->SetActorScale3D(FVector(19, 16, .5));
				const FTransform WoodTransform(FVector(-350, -900, 470));
				ARTSResourceNode* Wood = World->SpawnActorDeferred<ARTSResourceNode>(ARTSResourceNode::StaticClass(), WoodTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				Wood->Configure(ERTSResource::Wood, 500); Wood->FinishSpawning(WoodTransform);
				const FVector CameraPosition(1250, 1900, 2100);
				ACameraActor* Camera = World->SpawnActor<ACameraActor>(CameraPosition, (FVector(0, -300, 450) - CameraPosition).Rotation());
				Camera->GetCameraComponent()->SetFieldOfView(45);
				Camera->GetCameraComponent()->bConstrainAspectRatio = false;
				World->GetFirstPlayerController()->SetViewTarget(Camera); At = Now; ++Stage; return false;
			}
			if (Stage == 3 && Now - At > 4)
			{ FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Automation/StructuresVisual.png"), false, false); At = Now; ++Stage; }
			return Stage == 4 && Now - At > 2;
		}
	private:
		UStaticMeshComponent* Find(AActor* Actor, FName Name)
		{ for (UStaticMeshComponent* Mesh : TInlineComponentArray<UStaticMeshComponent*>(Actor)) { if (Mesh->GetFName() == Name) { return Mesh; } } return nullptr; }
		FAutomationTestBase* Test;
		TArray<ARTSBuilding*> Buildings;
		ARTSBuilding* Construction = nullptr;
		UStaticMeshComponent* Roof = nullptr;
		TArray<UMaterialInterface*> Materials;
		UMaterialInterface* ChangedTeam = nullptr;
		int32 Stage = 0;
		double Started = FPlatformTime::Seconds(), At = 0;
	};
}
bool FRTSStructuresTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckStructures(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand()); return true;
}
#endif
