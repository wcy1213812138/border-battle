#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Tests/RTSEconomyScenario.h"
#include "UI/RTSResourceWidget.h"
#include "UI/RTSMatchWidget.h"
#include "UI/RTSUIStyle.h"
#include "Core/RTSHumanController.h"
#include "Selection/RTSSelectionComponent.h"
#include "Buildings/RTSBuilding.h"
#include "Economy/RTSProducerComponent.h"
#include "Economy/RTSResourceComponent.h"
#include "Match/RTSMatchSubsystem.h"
#include "Components/WrapBox.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/World.h"
#include "Editor.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRTSUIStyleTest, "MiniRTS.UI.ThemeAndContext", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
namespace
{
	class FCheckUI : public IAutomationLatentCommand
	{
	public:
		explicit FCheckUI(FAutomationTestBase* Value) : Test(Value) {}
		bool Update() override
		{
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{ if (FPlatformTime::Seconds() - Started < 30) { return false; } Test->AddError(TEXT("PIE unavailable")); return true; }
			ARTSHumanController* PC = Cast<ARTSHumanController>(World->GetFirstPlayerController());
			if (!PC) { Test->AddError(TEXT("Human controller unavailable")); return true; }
			if (Stage == 0)
			{
				FString Error;
				if (!RTSEconomyScenario::Create(World, Scenario, Error)) { Test->AddError(Error); return true; }
				Resources = CreateWidget<URTSResourceWidget>(PC, URTSResourceWidget::StaticClass()); Resources->AddToViewport();
				Menu = CreateWidget<URTSMatchWidget>(PC, URTSMatchWidget::StaticClass()); Menu->AddToViewport(20);
				++Stage; return false;
			}
			auto Refresh = [&]() { if (UFunction* Function = Resources->FindFunction(TEXT("Refresh"))) { Resources->ProcessEvent(Function, nullptr); } };
			UWrapBox* Build = Cast<UWrapBox>(Resources->GetWidgetFromName(TEXT("BuildActions")));
			UWrapBox* Train = Cast<UWrapBox>(Resources->GetWidgetFromName(TEXT("TrainActions")));
			UProgressBar* Progress = Cast<UProgressBar>(Resources->GetWidgetFromName(TEXT("ContextProgress")));
			UButton* CancelQueue = Cast<UButton>(Resources->GetWidgetFromName(TEXT("CancelQueueButton")));
			UButton* CancelPlacement = Cast<UButton>(Resources->GetWidgetFromName(TEXT("CancelPlacementButton")));
			if (!Build || !Train || !Progress || !CancelQueue || !CancelPlacement) { Test->AddError(TEXT("Named UI widgets unavailable")); return true; }
			URTSEconomyButton* Sample = NewObject<URTSEconomyButton>();
			const FButtonStyle& Style = Sample->GetStyle();
			Test->TestTrue(TEXT("Buttons use code-native rounded style"), Style.Normal.DrawAs == ESlateBrushDrawType::RoundedBox);
			Test->TestFalse(TEXT("Buttons do not capture keyboard focus"), Sample->GetIsFocusable());
			Test->TestTrue(TEXT("Hover and disabled visuals are distinct"), Style.Normal.TintColor != Style.Hovered.TintColor && Style.Disabled.TintColor != Style.Normal.TintColor);
			for (FVector2D View : {FVector2D(960, 640), FVector2D(1280, 720), FVector2D(1920, 1080)})
			{
				const FVector2D Modal = RTSUIStyle::ModalSize(View);
				Test->TestTrue(TEXT("Modal sizing leaves screen margins"), Modal.X <= View.X - 64 && Modal.Y <= View.Y - 80);
				Test->TestTrue(TEXT("Modal leaves top menu toggle clearance"), (View.Y - Modal.Y) * 0.5 >= 100);
				Test->TestTrue(TEXT("Command and unit panel width budget fits"), RTSUIStyle::CommandWidth(View.X) + 420 <= View.X);
			}
			PC->GetSelection()->SelectSingle(Scenario.Workers[0], false); PC->SelectBuilding(nullptr); Refresh();
			Test->TestTrue(TEXT("Workers expose build actions only"), Build->GetVisibility() == ESlateVisibility::Visible && Train->GetVisibility() == ESlateVisibility::Collapsed);
			Test->TestTrue(TEXT("Idle worker has no progress bar"), Progress->GetVisibility() == ESlateVisibility::Collapsed);
			PC->BeginBuildingPlacement(TEXT("Farm")); Refresh();
			Test->TestTrue(TEXT("Placement cancel appears during preview"), CancelPlacement->GetVisibility() == ESlateVisibility::Visible);
			PC->CancelBuildingPlacement(); Refresh();
			Test->TestTrue(TEXT("Placement cancel hides after cancellation"), CancelPlacement->GetVisibility() == ESlateVisibility::Collapsed);
			PC->SelectBuilding(Scenario.Hall); Refresh();
			Test->TestTrue(TEXT("Hall exposes training without build row"), Build->GetVisibility() == ESlateVisibility::Collapsed && Train->GetVisibility() == ESlateVisibility::Visible);
			Test->TestFalse(TEXT("Empty queue cancel disabled"), CancelQueue->GetIsEnabled());
			FString Error;
			Test->TestTrue(TEXT("Existing training action still pays and queues"), Scenario.Hall->GetProducer()->Enqueue(TEXT("Peasant"), Error)); Refresh();
			Test->TestTrue(TEXT("Queue enables cancel and progress"), CancelQueue->GetIsEnabled() && Progress->GetVisibility() == ESlateVisibility::Visible);
			Scenario.Hall->GetProducer()->CancelLast(); Refresh();
			Test->TestTrue(TEXT("Empty queue hides progress again"), Progress->GetVisibility() == ESlateVisibility::Collapsed);
			URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(World);
			UBorder* Modal = Cast<UBorder>(Menu->GetWidgetFromName(TEXT("MatchModal")));
			UBorder* Scrim = Cast<UBorder>(Menu->GetWidgetFromName(TEXT("MenuScrim")));
			Test->TestNotNull(TEXT("Styled menu exists"), Modal); Test->TestNotNull(TEXT("Modal scrim exists"), Scrim);
			if (Modal && Scrim)
			{ Test->TestTrue(TEXT("Modal is above input scrim"), CastChecked<UCanvasPanelSlot>(Modal->Slot)->GetZOrder() > CastChecked<UCanvasPanelSlot>(Scrim->Slot)->GetZOrder()); }
			Match->ToggleMainMenu();
			if (Modal && Scrim) { Test->TestTrue(TEXT("Menu and input scrim appear together"), Modal->GetVisibility() == ESlateVisibility::Visible && Scrim->GetVisibility() == ESlateVisibility::Visible); }
			Test->TestTrue(TEXT("Gameplay HUD hidden in menu"), Resources->GetVisibility() == ESlateVisibility::Collapsed);
			if (UFunction* Hard = Menu->FindFunction(TEXT("Hard"))) { Menu->ProcessEvent(Hard, nullptr); }
			Match->ToggleMainMenu();
			if (Modal && Scrim) { Test->TestTrue(TEXT("Closing menu hides scrim"), Modal->GetVisibility() == ESlateVisibility::Collapsed && Scrim->GetVisibility() == ESlateVisibility::Collapsed); }
			Refresh(); Test->TestTrue(TEXT("Gameplay HUD returns without reset"), Resources->GetVisibility() == ESlateVisibility::SelfHitTestInvisible);
			Resources->RemoveFromParent(); Menu->RemoveFromParent(); return true;
		}
	private:
		FAutomationTestBase* Test;
		FRTSEconomyScenario Scenario;
		URTSResourceWidget* Resources = nullptr;
		URTSMatchWidget* Menu = nullptr;
		int32 Stage = 0;
		double Started = FPlatformTime::Seconds();
	};
}
bool FRTSUIStyleTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/RTS/Maps/L_M0Test")));
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	ADD_LATENT_AUTOMATION_COMMAND(FCheckUI(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}
#endif
