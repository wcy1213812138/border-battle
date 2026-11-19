#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Environment/RTSEnvironment.h"
#include "Match/RTSMatchSubsystem.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NavigationSystem.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSEnvironmentTest, "MiniRTS.Environment.PresentationIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckEnvironment : public IAutomationLatentCommand
	{
	public:
		explicit FCheckEnvironment(FAutomationTestBase* Value) : Test(Value) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			ARTSEnvironment* Environment = ARTSEnvironment::Get(World);
			if (!Environment) { Test->AddError(TEXT("Runtime environment unavailable")); return true; }
			if (Stage == 0)
			{
				Test->TestTrue(TEXT("Environment enabled in game"), Environment->IsPresentationEnabled());
				int32 ExpectedSlots=0;
				for(TActorIterator<AStaticMeshActor> It(World);It;++It)
				{
					auto* Mesh=It->GetStaticMeshComponent();if(!Mesh || !Mesh->GetStaticMesh())continue;
					const FString Path=Mesh->GetStaticMesh()->GetPathName();
					if(It->GetActorNameOrLabel()==TEXT("RTS Arena Ground") || Path==TEXT("/Engine/MapTemplates/SM_Template_Map_Floor.SM_Template_Map_Floor") || Path.StartsWith(TEXT("/Game/LevelPrototyping/Meshes/")))ExpectedSlots+=Mesh->GetNumMaterials();
				}
				Test->TestTrue(TEXT("Current map contains recognized style surfaces"),ExpectedSlots>0);
				Test->TestEqual(TEXT("Every recognized map material slot styled exactly once"),Environment->GetStyledSlotCount(),ExpectedSlots);
				Test->TestEqual(TEXT("Fixed decoration instance budget"), Environment->GetDecorationCount(), 649);
				Test->TestTrue(TEXT("Ensure is idempotent"), ARTSEnvironment::Ensure(World) == Environment);
				for (UInstancedStaticMeshComponent* Batch : TInlineComponentArray<UInstancedStaticMeshComponent*>(Environment))
				{
					Test->TestTrue(TEXT("Scenery cannot collide or affect nav"), Batch->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !Batch->CanEverAffectNavigation());
					Test->TestNotNull(TEXT("Scenery mesh loaded"), Batch->GetStaticMesh().Get());
					if (Batch->GetFName() == TEXT("SceneryApron")) { continue; }
					for (int32 i = 0; i < Batch->GetInstanceCount(); ++i)
					{
						FTransform Transform; Batch->GetInstanceTransform(i, Transform, true);
						Test->TestTrue(TEXT("All trees and rocks outside playable footprint"), FMath::Max(FMath::Abs(Transform.GetLocation().X), FMath::Abs(Transform.GetLocation().Y)) >= 2350);
					}
				}
				UStaticMeshComponent* Floor = nullptr;
				for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
				{ UStaticMeshComponent* Mesh = It->GetStaticMeshComponent(); if (Mesh && (It->GetActorLabel() == TEXT("RTS Arena Ground") || (Mesh->GetStaticMesh() && Mesh->GetStaticMesh()->GetName() == TEXT("SM_Template_Map_Floor")))) { Floor = Mesh; break; } }
				if (!Floor) { Test->AddError(TEXT("Original floor unavailable")); return true; }
				const FTransform FloorTransform = Floor->GetComponentTransform();
				const ECollisionEnabled::Type Collision = Floor->GetCollisionEnabled();
				UMaterialInterface* Styled = Floor->GetMaterial(0);
				Test->TestNotNull(TEXT("Floor uses runtime material instance"), Cast<UMaterialInstanceDynamic>(Styled));
				Environment->SetPresentationEnabled(false);
				UMaterialInterface* Original = Floor->GetMaterial(0);
				Test->TestTrue(TEXT("Disabling restores original floor material"), Original != Styled);
				Test->TestFalse(TEXT("Disabling restores lighting state"), Environment->IsLightingEnabled());
				FHitResult Before, After;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(RTSEnvironmentTest), false);
				const FVector From(-1700, 0, 1000), To(-1700, 0, -150);
				const bool bBefore = World->LineTraceSingleByChannel(Before, From, To, ECC_Visibility, Params);
				UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				FNavLocation NavBefore, NavAfter;
				const bool bNavBefore = Nav && Nav->ProjectPointToNavigation(FVector(-1700, 0, 10), NavBefore, FVector(50, 50, 100));
				Environment->SetPresentationEnabled(true);
				const bool bAfter = World->LineTraceSingleByChannel(After, From, To, ECC_Visibility, Params);
				Test->TestTrue(TEXT("Ground picking hit and height unchanged"), bBefore && bAfter && Before.GetComponent() == After.GetComponent() && Before.ImpactPoint.Equals(After.ImpactPoint, 0.01));
				Test->TestTrue(TEXT("Floor geometry and collision untouched"), Floor->GetComponentTransform().Equals(FloorTransform) && Floor->GetCollisionEnabled() == Collision);
				const bool bNavAfter = Nav && Nav->ProjectPointToNavigation(FVector(-1700, 0, 10), NavAfter, FVector(50, 50, 100));
				Test->TestTrue(TEXT("Nav projection unaffected"), bNavBefore && bNavAfter && NavBefore.Location.Equals(NavAfter.Location, 0.01));
				APostProcessVolume* Post = Environment->GetPostProcess();
				Test->TestNotNull(TEXT("Presentation post-process exists"), Post);
				if (Post) { Test->TestTrue(TEXT("Post-process no motion blur and mild bloom"), Post->Settings.MotionBlurAmount == 0 && Post->Settings.BloomIntensity <= 0.15f && Post->bUnbound); }
				for (TActorIterator<ADirectionalLight> It(World); It; ++It)
				{
					UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(It->GetLightComponent());
					const float Intensity = Sun->Intensity;
					Environment->SetLightingEnabled(false); const FRotator OriginalRotation = Sun->GetComponentRotation();
					Environment->SetLightingEnabled(true);
					Test->TestEqual(TEXT("Sun intensity preserved"), Sun->Intensity, Intensity);
					Environment->SetLightingEnabled(false);
					Test->TestTrue(TEXT("Light rotation restored"), Sun->GetComponentRotation().Equals(OriginalRotation, 0.01f));
					Environment->SetLightingEnabled(true); break;
				}
				// Runtime edits by another system must not be replaced by an old snapshot.
				Floor->SetMaterial(0, Original); Environment->SetPresentationEnabled(false);
				Test->TestTrue(TEXT("External material change respected on restore"), Floor->GetMaterial(0) == Original);
				Environment->SetPresentationEnabled(true);
				++Stage;
			}
			if (Stage == 1)
			{
				URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World); FString Error;
				if (!Match->StartMatch(ERTSDifficulty::Normal, Error))
				{ if (FPlatformTime::Seconds() - Started < 35) { return false; } Test->AddError(Error); return true; }
				Test->TestTrue(TEXT("Match retains runtime environment"), ARTSEnvironment::Get(World) == Environment && Environment->IsPresentationEnabled());
				Match->ShowMainMenu();
				Test->TestEqual(TEXT("Reset does not duplicate scenery"), Environment->GetDecorationCount(), 649);
				Match->EnterSandbox();
				if (!FParse::Param(FCommandLine::Get(), TEXT("RTSCaptureEnvironment"))) { return true; }
				ACameraActor* Camera = World->SpawnActor<ACameraActor>(FVector(-3200, -3200, 6200), FRotator(-54, 45, 0));
				World->GetFirstPlayerController()->SetViewTarget(Camera);
				CaptureAt = World->GetRealTimeSeconds(); ++Stage; return false;
			}
			if (Stage == 2 && World->GetRealTimeSeconds() - CaptureAt > 4)
			{
				FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Automation/EnvironmentVisual.png"), false, false);
				CaptureAt = World->GetRealTimeSeconds(); ++Stage;
			}
			return Stage == 3 && World->GetRealTimeSeconds() - CaptureAt > 2;
		}
	private:
		FAutomationTestBase* Test;
		int32 Stage = 0;
		double Started = FPlatformTime::Seconds(), CaptureAt = 0;
	};
}
bool FRTSEnvironmentTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckEnvironment(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
