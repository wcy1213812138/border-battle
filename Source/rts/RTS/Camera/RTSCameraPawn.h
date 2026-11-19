// MiniRTS - top-down RTS camera: WASD/arrow pan, mouse-wheel zoom, optional edge scroll.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RTSCameraPawn.generated.h"

class USceneComponent;
class USpringArmComponent;
class UCameraComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

UCLASS()
class ARTSCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ARTSCameraPawn();

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
 virtual void BeginPlay() override;
 float GetMaximumZoomDistance() const { return MaxArmLength; }
	virtual void PawnClientRestart() override;
	void FlashAttackWarning();
	/** P1-15: player-tunable camera speed shared by every camera pawn. */
	static void SetPanSpeedScale(float Scale);
	static float GetPanSpeedScale();
	float GetAttackWarningStrength() const { return AttackWarningSeconds; }

protected:
	void OnPan(const FInputActionValue& Value);
	void OnZoom(const FInputActionValue& Value);
	void BuildInput();

	UPROPERTY(VisibleAnywhere, Category = "RTS|Camera")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "RTS|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "RTS|Camera")
	TObjectPtr<UCameraComponent> Camera;

	/** Pan speed (cm/s) at the default zoom; scales linearly with arm length. */
	UPROPERTY(EditAnywhere, Category = "RTS|Camera")
	float PanSpeed = 2000.0f;
	/** Multiplies PanSpeed; persisted in GGameUserSettingsIni, never baked into content. */
	static float PanSpeedScale;

	UPROPERTY(EditAnywhere, Category = "RTS|Camera")
	float ZoomStep = 200.0f;

	UPROPERTY(EditAnywhere, Category = "RTS|Camera")
	float MinArmLength = 600.0f;

	UPROPERTY(EditAnywhere, Category = "RTS|Camera")
	float MaxArmLength = 3500.0f;

	UPROPERTY(EditAnywhere, Category = "RTS|Camera")
	float DefaultArmLength = 2000.0f;

	UPROPERTY(EditAnywhere, Category = "RTS|Camera")
	float ZoomInterpSpeed = 10.0f;

	UPROPERTY(EditAnywhere, Category = "RTS|Camera")
	bool bEdgeScroll = true;

	/** Edge-scroll band width in pixels. */
	UPROPERTY(EditAnywhere, Category = "RTS|Camera")
	float EdgeSize = 12.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> PanAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> ZoomAction;

	FVector2D PendingPan = FVector2D::ZeroVector;
	float AttackWarningSeconds=0;
	bool bAttackTintActive=false;
	bool bPreviousTintOverride=false;
	FLinearColor PreviousSceneTint=FLinearColor::White;
	float PreviousPostProcessWeight=1;
	float TargetArmLength = 2000.0f;
};
