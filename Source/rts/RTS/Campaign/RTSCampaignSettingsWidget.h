#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSCampaignSettingsWidget.generated.h"

class UBorder;
class UButton;
class USlider;
class USizeBox;
class UTextBlock;
class UVerticalBox;

/** Code-drawn gear: no font glyph or additional cooked texture dependency. */
UCLASS()
class URTSCampaignGearIcon : public UUserWidget
{
 GENERATED_BODY()
protected:
 virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override;
};

/** Separate modal settings. Save rules remain in ARTSCampaign, audio in its subsystem. */
UCLASS()
class URTSCampaignSettingsWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 void OpenSettings();
 UFUNCTION() void CloseSettings();
 bool IsOpen() const { return GetVisibility()==ESlateVisibility::Visible; }
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeTick(const FGeometry& Geometry,float DeltaTime) override;
 virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
 UButton* AddButton(UVerticalBox* Column,const TCHAR* Caption,FName Name,FName Function);
 USlider* AddVolume(UVerticalBox* Column,const TCHAR* Caption,FName Name,FName Function,TObjectPtr<UTextBlock>& ValueLabel);
 void RefreshStatus();
 void SyncAudio();
	/** Which manual slot the pending load would read; INDEX_NONE when no chooser is open. */
	int32 PendingSlot = -1;
	/** One save entry point: choose the destination slot in the list that follows. */
	UFUNCTION() void SaveCampaign();
	UFUNCTION() void SaveToSlot1();
	UFUNCTION() void SaveToSlot2();
	UFUNCTION() void SaveToSlot3();
	UFUNCTION() void CancelSaveSlot();
	/** Writes the current snapshot into a chosen manual slot and makes it the active one. */
	void CommitSaveSlot(int32 Index);
	void RequestSaveSlot(int32 Index);
	UFUNCTION() void ConfirmSave();
	UFUNCTION() void CancelSaveConfirmation();
	void RefreshSaveSlots();
	int32 PendingSaveSlot = -1;
	bool bConfirmingOverwrite = false;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SettingsPage;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SavePage;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SaveConfirmation;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SavePrompt;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> SaveStatus;
	UPROPERTY(Transient) TObjectPtr<UButton> ConfirmSaveButton;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> SaveSlotButtons;
	void RequestLoadSlot(int32 Index);
	/** One load entry point with the same chooser, still behind the confirmation below. */
	UFUNCTION() void RequestLoad();
	UFUNCTION() void RequestLoadSlot1();
	UFUNCTION() void RequestLoadSlot2();
	UFUNCTION() void RequestLoadSlot3();
	UFUNCTION() void ConfirmLoad();
	UFUNCTION() void CancelLoad();
	/** Roll back to the snapshot taken when the current in-game day began. */
	UFUNCTION() void ResetToday();
	UFUNCTION() void ConfirmResetToday();
	UFUNCTION() void CancelResetToday();
	UFUNCTION() void FocusHome();
 UFUNCTION() void MasterChanged(float Value);
 UFUNCTION() void MusicChanged(float Value);
 UFUNCTION() void EffectsChanged(float Value);
	UFUNCTION() void CameraSpeedChanged(float Value);
	UFUNCTION() void CycleResolution();
	UFUNCTION() void CycleWindowMode();
	UFUNCTION() void ToggleVSync();
	void RefreshDisplayStatus();
	UPROPERTY(Transient) TObjectPtr<USlider> CameraSlider;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CameraValue;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> DisplayStatus;
 UFUNCTION() void CyclePurple();
 UFUNCTION() void CycleYellow();
 UFUNCTION() void CycleRed();
 UFUNCTION() void CyclePink();
 UPROPERTY(Transient) TObjectPtr<USizeBox> PanelSize;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> LoadConfirmation;
 UPROPERTY(Transient) TObjectPtr<USlider> MasterSlider;
 UPROPERTY(Transient) TObjectPtr<USlider> MusicSlider;
 UPROPERTY(Transient) TObjectPtr<USlider> EffectsSlider;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> MasterValue;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> MusicValue;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> EffectsValue;
 bool bResumeOnClose=false;
	/** Slot pickers shown under the single 保存 / 读取 entries, plus the reset confirmation. */
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> SaveChooser;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> LoadChooser;
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> ResetConfirmation;
 bool bLoadPrevious=false;
};
