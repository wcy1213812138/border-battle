#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "RTSDebugWidget.generated.h"
class UTextBlock;
class UButton;
class UVerticalBox;
UCLASS()
class URTSDebugWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaTime) override;
private:
	void AddButton(UVerticalBox* Parent, const TCHAR* Label, FName Function);
	void Spawn(FName Id);
	void Speed(float Value);
	UFUNCTION() void AddResources();
	UFUNCTION() void SpawnPeasant();
	UFUNCTION() void SpawnSwordman();
	UFUNCTION() void SpawnArcher();
	UFUNCTION() void Slow();
	UFUNCTION() void Normal();
	UFUNCTION() void Fast();
	UFUNCTION() void Close();
	UPROPERTY(Transient) TObjectPtr<class UBorder> Panel;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> StateText;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> MessageText;
	UPROPERTY(Transient) TObjectPtr<class USizeBox> PanelSize;
	UPROPERTY(Transient) TArray<TObjectPtr<UButton>> MutationButtons;
	float RefreshTimer = 0;
};
