#pragma once
#include "CoreMinimal.h"
#include "Units/RTSUnit.h"
#include "RTSPeasantShowcase.generated.h"

/** Opt-in visual prototype only; no progression or stat changes. */
UCLASS()
class ARTSPeasantShowcase : public ARTSUnit
{
	GENERATED_BODY()
public:
	void SetPreviewTier(int32 Value) { PreviewTier = FMath::Clamp(Value, 1, 5); }
	int32 GetPreviewTier() const { return PreviewTier; }
	void SetPreviewKind(FName Value) { PreviewKind = Value; }
	FName GetPreviewKind() const { return PreviewKind; }
	/** Runtime campaign use: rebuild visual-only attachments without touching unit physics. */
	void RefreshTier(int32 Value);
protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
private:
	int32 PreviewTier = 1;
	FName PreviewKind = TEXT("Peasant");
	UPROPERTY() TObjectPtr<USceneComponent> BackFrame;
	UPROPERTY() TObjectPtr<USceneComponent> CrownFrame;
	void UpdateAccessoryFrames();
	void BuildTierAppearance();
};
