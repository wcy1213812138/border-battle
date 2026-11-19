// MiniRTS - a player's resource stockpile (wood, food). Lives on the PlayerState so
// both the human and the computer player own one.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Core/RTSTypes.h"
#include "RTSResourceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRTSResourcesChangedSignature);

UCLASS(ClassGroup = (RTS), meta = (BlueprintSpawnableComponent))
class URTSResourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URTSResourceComponent();

	UFUNCTION(BlueprintPure, Category = "RTS|Resources")
	int32 GetAmount(ERTSResource Type) const;

	UFUNCTION(BlueprintCallable, Category = "RTS|Resources")
	void Add(ERTSResource Type, int32 Amount);
	bool SetAmounts(const TMap<ERTSResource,int32>& Values);
	int32 Deposit(ERTSResource Type, int32 Amount, int32 Capacity);

	UFUNCTION(BlueprintPure, Category = "RTS|Resources")
	bool CanAfford(const TMap<ERTSResource, int32>& Cost) const;

	/** Deducts Cost only if every entry is affordable. Returns false and changes nothing otherwise. */
	UFUNCTION(BlueprintCallable, Category = "RTS|Resources")
	bool TrySpend(const TMap<ERTSResource, int32>& Cost);

	UPROPERTY(BlueprintAssignable, Category = "RTS|Resources")
	FRTSResourcesChangedSignature OnResourcesChanged;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "RTS|Resources")
	TMap<ERTSResource, int32> Amounts = { { ERTSResource::Wood, 100 }, { ERTSResource::Food, 100 } };
};
