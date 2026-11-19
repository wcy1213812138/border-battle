// MiniRTS - per-player state: team and resources.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "RTSPlayerState.generated.h"

class URTSResourceComponent;

UCLASS()
class ARTSPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ARTSPlayerState();

	UFUNCTION(BlueprintPure, Category = "RTS")
	URTSResourceComponent* GetResources() const { return Resources; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS")
	TObjectPtr<URTSResourceComponent> Resources;
};
