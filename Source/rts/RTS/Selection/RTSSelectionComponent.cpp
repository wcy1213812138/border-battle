#include "Selection/RTSSelectionComponent.h"
#include "Units/RTSUnit.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Data/RTSUnitData.h"

URTSSelectionComponent::URTSSelectionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool URTSSelectionComponent::CanSelect(const ARTSUnit* Unit) const
{
	return IsValid(Unit) && Unit->IsAlive() && Unit->GetTeam() == OwningTeam;
}

void URTSSelectionComponent::Add(ARTSUnit* Unit)
{
	if (!CanSelect(Unit) || Selected.Contains(Unit))
	{
		return;
	}
	Selected.Add(Unit);
	Unit->SetSelected(true);
	Unit->OnUnitDied.AddUObject(this, &URTSSelectionComponent::HandleUnitDied);
}

void URTSSelectionComponent::Remove(ARTSUnit* Unit)
{
	if (Selected.Remove(Unit) > 0 && IsValid(Unit))
	{
		Unit->SetSelected(false);
		Unit->OnUnitDied.RemoveAll(this);
	}
}

void URTSSelectionComponent::ClearSelection()
{
	if (Selected.IsEmpty())
	{
		return;
	}
	for (const TWeakObjectPtr<ARTSUnit>& Weak : Selected)
	{
		if (ARTSUnit* Unit = Weak.Get())
		{
			Unit->SetSelected(false);
			Unit->OnUnitDied.RemoveAll(this);
		}
	}
	Selected.Reset();
	OnSelectionChanged.Broadcast();
}

bool URTSSelectionComponent::MatchesFilter(const ARTSUnit* Unit, ERTSSelectionFilter Filter) const
{
	if (!CanSelect(Unit)) { return false; }
	if (Filter == ERTSSelectionFilter::All) { return true; }
	const URTSUnitData* Data = Unit->GetUnitData();
	return Data && ((Filter == ERTSSelectionFilter::Workers && Data->bWorker)
		|| (Filter == ERTSSelectionFilter::Military && !Data->bWorker));
}

void URTSSelectionComponent::PruneToFilter(ERTSSelectionFilter Filter)
{
	if (Filter == ERTSSelectionFilter::All) { return; }
	const auto Previous = Selected;
	for (const auto& Weak : Previous) { if (!MatchesFilter(Weak.Get(), Filter)) { Remove(Weak.Get()); } }
}

void URTSSelectionComponent::SelectSingle(ARTSUnit* Unit, bool bAdditive)
{
	SelectSingleFiltered(Unit, bAdditive, ERTSSelectionFilter::All);
}

void URTSSelectionComponent::SelectSingleFiltered(ARTSUnit* Unit, bool bAdditive, ERTSSelectionFilter Filter)
{
	PruneToFilter(Filter);
	if (!MatchesFilter(Unit, Filter))
	{
		if (!bAdditive) { ClearSelection(); }
		OnSelectionChanged.Broadcast();
		return;
	}
	if (bAdditive)
	{
		// Shift-click toggles.
		if (Selected.Contains(Unit))
		{
			Remove(Unit);
		}
		else
		{
			Add(Unit);
		}
		OnSelectionChanged.Broadcast();
		return;
	}

	for (const TWeakObjectPtr<ARTSUnit>& Weak : Selected)
	{
		if (ARTSUnit* Other = Weak.Get(); Other && Other != Unit)
		{
			Other->SetSelected(false);
			Other->OnUnitDied.RemoveAll(this);
		}
	}
	const bool bWasSelected = Selected.Contains(Unit);
	Selected.Reset();
	if (bWasSelected)
	{
		Selected.Add(Unit);
	}
	else
	{
		Add(Unit);
	}
	OnSelectionChanged.Broadcast();
}

void URTSSelectionComponent::SelectInScreenRect(const FVector2D& CornerA, const FVector2D& CornerB, bool bAdditive, ERTSSelectionFilter Filter)
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC)
	{
		return;
	}
	PruneToFilter(Filter);

	const FBox2D Rect(FVector2D::Min(CornerA, CornerB), FVector2D::Max(CornerA, CornerB));

	TArray<ARTSUnit*> Inside;
	for (TActorIterator<ARTSUnit> It(GetWorld()); It; ++It)
	{
		ARTSUnit* Unit = *It;
		if (!MatchesFilter(Unit, Filter))
		{
			continue;
		}
		FVector2D Screen;
		if (PC->ProjectWorldLocationToScreen(Unit->GetActorLocation(), Screen, /*bPlayerViewportRelative*/ true) && Rect.IsInside(Screen))
		{
			Inside.Add(Unit);
		}
	}

	if (!bAdditive)
	{
		for (const TWeakObjectPtr<ARTSUnit>& Weak : Selected)
		{
			if (ARTSUnit* Unit = Weak.Get(); Unit && !Inside.Contains(Unit))
			{
				Unit->SetSelected(false);
				Unit->OnUnitDied.RemoveAll(this);
			}
		}
		Selected.RemoveAll([&Inside](const TWeakObjectPtr<ARTSUnit>& Weak) { return !Inside.Contains(Weak.Get()); });
	}
	for (ARTSUnit* Unit : Inside)
	{
		Add(Unit);
	}
	OnSelectionChanged.Broadcast();
}

TArray<ARTSUnit*> URTSSelectionComponent::GetSelectedUnits() const
{
	TArray<ARTSUnit*> Result;
	Result.Reserve(Selected.Num());
	for (const TWeakObjectPtr<ARTSUnit>& Weak : Selected)
	{
		if (ARTSUnit* Unit = Weak.Get(); Unit && Unit->IsAlive())
		{
			Result.Add(Unit);
		}
	}
	return Result;
}

void URTSSelectionComponent::HandleUnitDied(ARTSUnit* Unit)
{
	Remove(Unit);
	OnSelectionChanged.Broadcast();
}
