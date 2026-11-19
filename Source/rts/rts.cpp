// Copyright Epic Games, Inc. All Rights Reserved.

#include "rts.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "RTS/Core/RTSMatchGameMode.h"
#include "RTS/Core/RTSTypes.h"
#endif

class FRTSGameModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		FEditorDelegates::PreBeginPIE.AddRaw(this, &FRTSGameModule::PrepareRTSNavigation);
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		FEditorDelegates::PreBeginPIE.RemoveAll(this);
#endif
	}

private:
#if WITH_EDITOR
	void PrepareRTSNavigation(bool bSimulating)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World || !World->GetWorldSettings()->DefaultGameMode
			|| !World->GetWorldSettings()->DefaultGameMode->IsChildOf(ARTSMatchGameMode::StaticClass()))
		{
			return;
		}
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (Nav)
		{
			// Static NavMesh tiles are duplicated from editor to PIE. If the map has just
			// loaded, async generation may still be running: copying now gives PIE empty
			// tiles, which cannot regenerate in Static mode. Finish before duplication.
			for (ANavigationData* Data : Nav->NavDataSet)
			{
				if (Data)
				{
					Data->EnsureBuildCompletion();
				}
			}
			UE_LOG(LogRTS, Display, TEXT("RTS navigation build completed before PIE duplication."));
		}
	}
#endif
};

IMPLEMENT_PRIMARY_GAME_MODULE(FRTSGameModule, rts, "rts");
DEFINE_LOG_CATEGORY(Logrts)
