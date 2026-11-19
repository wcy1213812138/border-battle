#include "Economy/RTSResourceComponent.h"

URTSResourceComponent::URTSResourceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 URTSResourceComponent::GetAmount(ERTSResource Type) const
{
	const int32* Found = Amounts.Find(Type);
	return Found ? *Found : 0;
}

void URTSResourceComponent::Add(ERTSResource Type, int32 Amount)
{
	if (Amount == 0)
	{
		return;
	}
	int32& Value = Amounts.FindOrAdd(Type);
	Value = static_cast<int32>(FMath::Clamp<int64>(static_cast<int64>(Value) + Amount, 0, MAX_int32));
	OnResourcesChanged.Broadcast();
}

bool URTSResourceComponent::CanAfford(const TMap<ERTSResource, int32>& Cost) const
{
	for (const TPair<ERTSResource, int32>& Entry : Cost)
	{
		if (Entry.Value < 0 || Entry.Key >= ERTSResource::Count || GetAmount(Entry.Key) < Entry.Value)
		{
			return false;
		}
	}
	return true;
}

bool URTSResourceComponent::TrySpend(const TMap<ERTSResource, int32>& Cost)
{
	if (!CanAfford(Cost))
	{
		return false;
	}
	for (const TPair<ERTSResource, int32>& Entry : Cost)
	{
		if (Entry.Value > 0)
		{
			Amounts.FindOrAdd(Entry.Key) -= Entry.Value;
		}
	}
	OnResourcesChanged.Broadcast();
	return true;
}

bool URTSResourceComponent::SetAmounts(const TMap<ERTSResource,int32>& Values)
{
	for (const auto& P : Values) if (P.Key >= ERTSResource::Count || P.Value < 0) return false;
	Amounts = Values; OnResourcesChanged.Broadcast(); return true;
}
int32 URTSResourceComponent::Deposit(ERTSResource Type, int32 Amount, int32 Capacity)
{
	if (Type >= ERTSResource::Count || Amount <= 0 || Capacity < 0) return 0;
	const int32 Accepted = FMath::Min(Amount, FMath::Max(0, Capacity - GetAmount(Type)));
	Add(Type, Accepted); return Accepted;
}
