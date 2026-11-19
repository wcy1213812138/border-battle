// MiniRTS - campaign landmark that carries a HUD-drawn world label.
// UTextRenderComponent can only use offline UFonts (Latin/Cyrillic/Greek): Chinese text there turns into
// unreadable glyph soup, so campaign labels live in plain data and the HUD draws them with DroidSansFallback.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RTSWorldLabel.generated.h"

class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class ARTSWorldLabel : public AActor
{
	GENERATED_BODY()

public:
	ARTSWorldLabel();

	/** Call right after spawn; the HUD picks this up every frame. */
	void Setup(const FString& InLabel, const FLinearColor& InColor);
	FString GetLabel() const { return LabelText; }
	FLinearColor GetLabelColor() const { return LabelColor; }
	/** Height above the actor origin where the HUD draws the text. */
	float GetLabelHeight() const { return LabelHeight; }
	/** Presentation only, so it must never join physics or navigation. */
	bool IsVisualOnly() const;

private:
	UPROPERTY(VisibleAnywhere, Category = "RTS|Label")
	TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "RTS|Label")
	TObjectPtr<UStaticMeshComponent> Pole;

	FString LabelText;
	FLinearColor LabelColor = FLinearColor::White;
	float LabelHeight = 220.f;
};
