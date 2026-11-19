#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RTSDebugSubsystem.generated.h"
class ARTSUnit;

/** Explicit development actions; normal player/AI economy never calls these. */
UCLASS()
class URTSDebugSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	static URTSDebugSubsystem* Get(const UObject* Context);
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
	void TogglePanel();
	bool IsPanelOpen() const { return bPanelOpen; }
	bool AddResources(FString& Error);
	ARTSUnit* SpawnUnit(FName Id, FString& Error);
	bool SetSpeed(float Value, FString& Error);
	float GetSpeed() const;
	void RestoreSpeed();
	void ResetSession();
private:
	bool CanMutate(FString& Error) const;
	bool bPanelOpen = false;
};
