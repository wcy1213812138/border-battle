#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSEnvironment.generated.h"

class UStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UDirectionalLightComponent;
class USkyLightComponent;
class APostProcessVolume;

USTRUCT()
struct FRTSEnvironmentSurface
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY() int32 Slot = 0;
	UPROPERTY() TObjectPtr<UMaterialInterface> Original;
	UPROPERTY() TObjectPtr<UMaterialInterface> Applied;
};

// Runtime presentation only: never changes terrain geometry, collision or navigation.
UCLASS()
class ARTSEnvironment : public AActor
{
	GENERATED_BODY()
public:
	ARTSEnvironment();
	static ARTSEnvironment* Get(const UObject* Context);
	static ARTSEnvironment* Ensure(UWorld* World);
	bool SetPresentationEnabled(bool bValue);
	void SetLightingEnabled(bool bValue);
	bool IsPresentationEnabled() const { return bEnabled; }
	bool IsLightingEnabled() const { return bLightingApplied; }
	int32 GetStyledSlotCount() const { return Surfaces.Num(); }
	int32 GetDecorationCount() const;
	APostProcessVolume* GetPostProcess() const { return PostProcess.Get(); }
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	bool CreateScenery();
	void ApplySurfaces();
	void RestoreSurfaces();
	void ApplyLighting();
	void RestoreLighting();
	UMaterialInstanceDynamic* Tint(UMaterialInterface* Parent, FLinearColor Color);
	UInstancedStaticMeshComponent* Batch(FName Name, UStaticMesh* Mesh, UMaterialInterface* Material);
	UPROPERTY() TArray<FRTSEnvironmentSurface> Surfaces;
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Batches;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Ground;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Stone;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Sandstone;
	UPROPERTY() TArray<TObjectPtr<UMaterialInstanceDynamic>> Palette;
	TWeakObjectPtr<APostProcessVolume> PostProcess;
	TWeakObjectPtr<UDirectionalLightComponent> Sun;
	TWeakObjectPtr<USkyLightComponent> Sky;
	FRotator OriginalSunRotation;
	FLinearColor OriginalSunColor;
	FLinearColor OriginalSkyColor;
	FLinearColor AppliedSunColor;
	FLinearColor AppliedSkyColor;
	bool bEnabled = false;
	bool bBuilt = false;
	bool bWantLighting = true;
	bool bLightingApplied = false;
};
