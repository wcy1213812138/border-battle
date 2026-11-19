#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Match/RTSMatchSubsystem.h"
#include "RTSMatchWidget.generated.h"
class UTextBlock;
class UVerticalBox;
class UBorder;
class UHorizontalBox;
class UButton;
class USlider;
UCLASS()
class URTSMatchWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
	void Refresh();
	UButton* Button(UVerticalBox* Parent, const FString& Text, FName Function);
	UFUNCTION() void Menu();
	UFUNCTION() void ToggleMenu();
	UFUNCTION() void Easy();
	UFUNCTION() void Normal();
	UFUNCTION() void Hard();
	UFUNCTION() void Start();
	UFUNCTION() void Pause();
	UFUNCTION() void Resume();
	UFUNCTION() void Restart();
	UFUNCTION() void Surrender();
	UFUNCTION() void VolumeChanged(float Value);
	UFUNCTION() void QualityLow();
	UFUNCTION() void QualityMedium();
	UFUNCTION() void QualityHigh();
private:
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> Actions;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Header;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Body;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DifficultyText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> TopText;
	UPROPERTY(Transient) TObjectPtr<UBorder> Modal;
	UPROPERTY(Transient) TObjectPtr<USlider> Volume;
	UPROPERTY(Transient) TObjectPtr<UBorder> Scrim;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> DifficultyButtons;
	UPROPERTY(Transient) TObjectPtr<UButton> PauseButton;
	TWeakObjectPtr<URTSMatchSubsystem> BoundMatch;
	ERTSDifficulty SelectedDifficulty = ERTSDifficulty::Normal;
	ERTSMatchPhase LastPhase = ERTSMatchPhase::Sandbox;
	bool bBuilt = false;
};
