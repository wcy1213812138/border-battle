// MiniRTS - human player input: box/click selection (LMB) and context command (RMB).
// Converts input into URTSOrderLibrary calls; never touches units' internals directly.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Selection/RTSSelectionComponent.h"
#include "RTSHumanController.generated.h"

class URTSSelectionComponent;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;
class ARTSUnit;
class ARTSBuilding;
class ARTSResourceNode;
class ARTSCampaign;
struct FRTSBalanceRow;

UCLASS()
class ARTSHumanController : public APlayerController
{
	GENERATED_BODY()

public:
	ARTSHumanController();

	UFUNCTION(BlueprintCallable, Category = "RTS|Input")
	void ToggleGamePause();
	UFUNCTION(BlueprintPure, Category = "RTS|Input")
	URTSSelectionComponent* GetSelection() const { return Selection; }

	/** True while the player is dragging a selection box (for the HUD to draw). */
	bool IsBoxSelecting() const { return bLeftDown && bDragging; }
	FVector2D GetBoxStart() const { return BoxStart; }
	FVector2D GetBoxEnd() const { return BoxEnd; }

	ARTSBuilding* GetSelectedBuilding() const;
	void SelectBuilding(ARTSBuilding* Building);
	ARTSResourceNode* GetSelectedResource() const;
	void SelectResource(ARTSResourceNode* Resource);
	void BeginBuildingPlacement(FName Id);
	void CancelBuildingPlacement();
	bool ConfirmBuildingPlacement(const FVector& Ground);
	bool IsPlacingBuilding() const { return !PlacementId.IsNone(); }
	// Ctrl+digits save groups; Alt+F1 selects all workers, Alt+F2 all military.
	void AssignControlGroup(int32 Slot);
	bool RecallControlGroup(int32 Slot, bool bAdditive);
	int32 GetControlGroupSize(int32 Slot) const { return ControlGroups.IsValidIndex(Slot) ? ControlGroups[Slot].Num() : 0; }
	// P1-02: cycle own completed production buildings (selection plus camera focus).
	bool SelectNextProductionBuilding();
	bool SelectIdleWorker();
	int32 SelectAllWorkers();
	int32 SelectAllMilitary();
	/** P1-03: attack-move to Goal with the current unit selection (also used by automation). */
	bool RequestAttackMoveAt(const FVector& Goal);
	/** Right-click ground while a completed own building is selected (P1-03). */
	bool SetSelectedBuildingRally(const FVector& Ground);
	/** Minimap command: projected ground destination; uses the same input gate and move queue. */
	bool RequestMinimapMoveAt(const FVector& Ground, bool bQueued);
	/** Shared visible-hit context command; nonworkers never silently consume a work click. */
	bool RequestContextCommandAt(AActor* Target, const FVector& Ground, bool bQueued);
	/** Visible world hit plus nearer Pawn hit, without changing combat collision profiles. */
	bool TraceContextTarget(const FVector& Start,const FVector& End,FHitResult& Hit) const;
	/** Hold T/G to filter a mouse gesture; both held deliberately selects none. */
	ERTSSelectionFilter GetMouseSelectionFilter() const;
	bool RequestMouseSelection(AActor* Target, bool bAdditive, ERTSSelectionFilter Filter);
	bool RequestBoxSelection(const FVector2D& A, const FVector2D& B, bool bAdditive, ERTSSelectionFilter Filter);

	/** Save validator limit for total alive friendly campaign buildings. */
	static constexpr int32 CampaignFriendlyBuildingLimit = 30;

	/** True when the whole axis-aligned footprint is inside the current campaign bounds. */
	static bool IsCampaignFootprintInsideMap(const FVector& Ground, float Footprint);

	/** Pure campaign gate shared by the build button and placement confirmation. */
	static bool CanStartCampaignBarracksBuild(int32 GuideStep, int32 AliveFriendlyBuildings, int32 Wood,
		int32 Food, const FRTSBalanceRow& Row, FString& OutError);

	/** Workers selected plus campaign guide/count/cost; generic match placement keeps its old rules. */
	bool CanStartBuildingPlacement(FName Id, FString& Error) const;
	const FString& GetEconomyMessage() const { return EconomyMessage; }
	void SetEconomyMessage(const FString& Message);
	FSimpleMulticastDelegate OnEconomyContextChanged;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;

	void OnSelectPressed(const FInputActionValue& Value);
	void OnSelectReleased(const FInputActionValue& Value);
	void OnCommandPressed(const FInputActionValue& Value);
	void OnCancelPressed(const FInputActionValue& Value);
	void OnPausePressed();
	void OnDebugPressed();
	bool CanIssueGameplayInput() const;

	bool GetMousePos(FVector2D& Out) const;
	bool IsShiftDown() const;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RTS|Input")
	TObjectPtr<URTSSelectionComponent> Selection;

	/** Pixels the mouse must travel before a click becomes a box drag. */
	UPROPERTY(EditAnywhere, Category = "RTS|Input")
	float DragThreshold = 6.0f;

private:
	UPROPERTY(Transient)
	TObjectPtr<UInputMappingContext> MappingContext;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> SelectAction;

	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CommandAction;
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> CancelAction;
	TWeakObjectPtr<ARTSBuilding> SelectedBuilding;
	TWeakObjectPtr<ARTSResourceNode> SelectedResource;
	ERTSSelectionFilter MouseSelectionFilter = ERTSSelectionFilter::All;
	FName PlacementId;
	FString EconomyMessage;
	bool bPlacementClick = false;
	bool CollectSelectedWorkers(TArray<ARTSUnit*>& OutWorkers) const;
	bool ValidateCampaignPlacement(FName Id, const FVector& Ground, const TArray<ARTSUnit*>& Workers, FString& Error) const;

	bool bLeftDown = false;
	bool bDragging = false;
	FVector2D BoxStart = FVector2D::ZeroVector;
	FVector2D BoxEnd = FVector2D::ZeroVector;
	// Control groups hold weak pointers so a dead or destroyed unit is dropped silently.
	TArray<TArray<TWeakObjectPtr<ARTSUnit>>> ControlGroups;
	int32 IdleCursor = 0;
	/** P1-03: X arms attack-move; the next left click on the ground commits it. */
	void OnAttackMoveKey();
	bool bAttackMovePending = false;
	// P1-02: cycling index for the production-building hotkey.
	int32 ProductionCursor = 0;
	void OnGroupSlot(int32 Slot);
	bool IsCtrlDown() const;
	void OnWorkerKey();
	void OnSelectAllKey();
	int32 SelectAllByRole(bool bWorkers);
	// P1-02: B cycles own completed production buildings; selection and camera move together.
	void OnProductionKey();
};
