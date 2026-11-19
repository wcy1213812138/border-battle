// MiniRTS - wires the RTS framework classes together.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RTSMatchGameMode.generated.h"

UCLASS()
class ARTSMatchGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARTSMatchGameMode();
protected:
	virtual void BeginPlay() override;
};
