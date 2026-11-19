// MiniRTS - short-lived ground marker confirming a player command (green = move, red = attack).
// Purely cosmetic; spawned by the human controller, never by game logic.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSCommandMarker.generated.h"

class UStaticMeshComponent;

UCLASS(NotPlaceable)
class ARTSCommandMarker : public AActor
{
	GENERATED_BODY()

public:
	ARTSCommandMarker();

	/** Spawns a marker at Location. Diameter in cm. */
	static ARTSCommandMarker* SpawnAt(UWorld* World, const FVector& Location, const FLinearColor& Color, float Diameter = 80.0f);

protected:
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "RTS|Marker")
	TObjectPtr<UStaticMeshComponent> Disc;

	UPROPERTY(EditAnywhere, Category = "RTS|Marker", meta = (Units = "s"))
	float Duration = 0.45f;

private:
	float Elapsed = 0.0f;
	float StartScale = 0.8f;
};
