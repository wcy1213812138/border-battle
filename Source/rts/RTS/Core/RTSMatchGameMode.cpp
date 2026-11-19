#include "Core/RTSMatchGameMode.h"
#include "Core/RTSHumanController.h"
#include "Core/RTSPlayerState.h"
#include "Camera/RTSCameraPawn.h"
#include "UI/RTSHUD.h"
#include "Match/RTSMatchSubsystem.h"
#include "Engine/World.h"
#include "Environment/RTSEnvironment.h"

ARTSMatchGameMode::ARTSMatchGameMode()
{
	DefaultPawnClass = ARTSCameraPawn::StaticClass();
	PlayerControllerClass = ARTSHumanController::StaticClass();
	PlayerStateClass = ARTSPlayerState::StaticClass();
	HUDClass = ARTSHUD::StaticClass();
}

void ARTSMatchGameMode::BeginPlay()
{
	Super::BeginPlay();
	ARTSEnvironment::Ensure(GetWorld());
	// Editor PIE keeps the development sandbox; standalone starts at the main menu.
	if (GetWorld()->WorldType == EWorldType::Game)
	{ if (URTSMatchSubsystem* Match = URTSMatchSubsystem::Get(this)) { Match->ShowMainMenu(); } }
}
