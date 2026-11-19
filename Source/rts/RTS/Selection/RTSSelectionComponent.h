// MiniRTS - owns the player's current selection. Lives on the PlayerController.
// Broadcasts OnSelectionChanged; UI listens, nothing here knows about UI.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RTSSelectionComponent.generated.h"

enum class ERTSSelectionFilter : uint8 { All, Workers, Military, None };

class ARTSUnit;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRTSSelectionChangedSignature);

UCLASS(ClassGroup = (RTS), meta = (BlueprintSpawnableComponent))
class URTSSelectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSSelectionComponent();

	/** Units this player may select. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Selection")
	uint8 OwningTeam = 0;

	UFUNCTION(BlueprintCallable, Category = "RTS|Selection")
	void SelectSingle(ARTSUnit* Unit, bool bAdditive);
	/** Mouse-only role filter; normal programmatic selection stays unrestricted. */
	void SelectSingleFiltered(ARTSUnit* Unit, bool bAdditive, ERTSSelectionFilter Filter);

	/** Selects every own, living unit whose screen position is inside the rectangle (pixels). */
	void SelectInScreenRect(const FVector2D& CornerA, const FVector2D& CornerB, bool bAdditive, ERTSSelectionFilter Filter = ERTSSelectionFilter::All);

	UFUNCTION(BlueprintCallable, Category = "RTS|Selection")
	void ClearSelection();

	UFUNCTION(BlueprintPure, Category = "RTS|Selection")
	TArray<ARTSUnit*> GetSelectedUnits() const;

	UFUNCTION(BlueprintPure, Category = "RTS|Selection")
	int32 GetSelectedCount() const { return Selected.Num(); }

	UPROPERTY(BlueprintAssignable, Category = "RTS|Selection")
	FRTSSelectionChangedSignature OnSelectionChanged;

private:
	bool CanSelect(const ARTSUnit* Unit) const;
	bool MatchesFilter(const ARTSUnit* Unit, ERTSSelectionFilter Filter) const;
	void PruneToFilter(ERTSSelectionFilter Filter);
	void Add(ARTSUnit* Unit);
	void Remove(ARTSUnit* Unit);
	void HandleUnitDied(ARTSUnit* Unit);

	TArray<TWeakObjectPtr<ARTSUnit>> Selected;
};
