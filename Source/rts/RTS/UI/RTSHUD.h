// MiniRTS - HUD: owns the resource widget and draws the selection box and health bars.
// Read-only view of game state; gameplay never calls into the HUD.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "Core/RTSTypes.h"
#include "RTSHUD.generated.h"

class URTSResourceWidget;
class URTSHealthComponent;
class ARTSUnit;
struct FRTSMoveGroup;

/** Read-only presentation shared by the HUD and economy panel; never drives gameplay. */
struct FRTSUnitActivity
{
	FString Summary = TEXT("待命");
	FString Label = TEXT("待命");
	FLinearColor Color = FLinearColor(0.3f, 1.f, 0.8f);
	FVector Target = FVector::ZeroVector;
	bool bHasTarget = false;
};

UCLASS()
class ARTSHUD : public AHUD
{
	GENERATED_BODY()

public:
	ARTSHUD();

	static FRTSUnitActivity DescribeUnitActivity(const ARTSUnit* Unit);

	/**
	 * Display-only move guide.
	 * Multi-selection: a group move converges on the group's shared requested destination.
	 * Solo selection: the unit's live slot/detour target. Other orders return false.
	 */
	static bool ResolveMoveGuideTarget(const FRTSOrder& Order, const FRTSMoveGroup* Group, bool bMultipleSelected, FVector& OutTarget);

	/** Campaign world labels are HUD-drawn: the runtime CJK font is the only thing that can show Chinese. */
	FString GetCjkFontFile() const { return CjkFontFile; }
	bool IsCjkFontRuntime() const;
 void SetCampaignStockExclusion(const FBox2D& NormalizedRect) { CampaignStockExclusion=NormalizedRect; }

protected:
	virtual void BeginPlay() override;
	virtual void DrawHUD() override;

	void DrawSelectionBox();

	void DrawSelectedBuilding();
	void DrawSelectedUnitActivities();
	void DrawHealthBars();
	void DrawHealthBar(const FVector& WorldPos, const URTSHealthComponent* Health, bool bShowNumbers);

	/** World label drawn in screen space with the CJK-capable font; multi-line and centred. */
	void DrawWorldLabel(const FVector& Screen, const FString& Text, const FLinearColor& Color, float Scale = 1.1f);

	/**
	 * P1-06: distant landmark labels fade and shrink so a close battle is never buried under text.
	 * Returns the alpha to use and writes the matching text scale.
	 */
	float WorldLabelFade(const FVector& WorldPoint, float& OutScale) const;

	UPROPERTY(EditAnywhere, Category = "RTS|HUD")
	TSubclassOf<URTSResourceWidget> ResourceWidgetClass;

	UPROPERTY(EditAnywhere, Category = "RTS|HUD")
	FVector2D HealthBarSize = FVector2D(50.f, 6.f);

	/** World-space offset above the actor's bounds top. */
	UPROPERTY(EditAnywhere, Category = "RTS|HUD")
	float HealthBarHeightOffset = 20.f;

private:
	UPROPERTY(Transient)
	TObjectPtr<class UFont> ChineseFont;
	/** DroidSansFallback.ttf: the only bundled font with Chinese glyphs. */
	FString CjkFontFile;

	UPROPERTY(Transient)
	TObjectPtr<URTSResourceWidget> ResourceWidget;
	UPROPERTY(Transient)
	TObjectPtr<class URTSMatchWidget> MatchWidget;
	UPROPERTY(Transient)
	TObjectPtr<class URTSDebugWidget> DebugWidget;
 FBox2D CampaignStockExclusion=FBox2D(ForceInit);
};
