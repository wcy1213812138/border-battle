// MiniRTS - CSV -> DataTable balance catalog and per-team economy access.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataTable.h"
#include "RTSBalanceSubsystem.generated.h"

class URTSResourceComponent;
class URTSUnitData;
class ARTSUnit;
class ARTSBuilding;

USTRUCT(BlueprintType, meta = (DisplayName = "RTS平衡数值行"))
struct FRTSBalanceRow : public FTableRowBase
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, meta = (DisplayName = "类型", ToolTip = "Unit代表单位，Building代表建筑；CSV中保留英文标识。")) FString Kind;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "木材费用")) int32 WoodCost = 0;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "食物费用")) int32 FoodCost = 0;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "生命上限")) float MaxHealth = 100;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "移动速度")) float MoveSpeed = 350;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "攻击伤害")) float AttackDamage = 10;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "攻击射程")) float AttackRange = 60;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "攻击间隔")) float AttackInterval = 1.2f;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "攻击前摇")) float AttackHitDelay = 0.4f;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "远程攻击")) bool bRangedAttack = false;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "可采集和建造")) bool bWorker = false;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "弹体速度")) float ProjectileSpeed = 1200;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "携带容量")) int32 CarryCapacity = 10;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "采集间隔")) float GatherInterval = 0.5f;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "建造所需工作秒")) float BuildSeconds = 8;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "训练时间")) float TrainSeconds = 4;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "占地宽度")) float Footprint = 240;
	UPROPERTY(EditAnywhere, meta = (DisplayName = "可交付资源")) bool bDropOff = false;
};

UCLASS()
class URTSBalanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	static URTSBalanceSubsystem* Get(const UObject* Context);
	bool Reload(FString& Error);
	// Parse and validate in memory; a rejected candidate leaves the current table intact.
	bool ReloadFromCSV(const FString& CSV, FString& Error);
	const FRTSBalanceRow* Find(FName Id) const;
	URTSResourceComponent* GetResources(uint8 Team) const;
	URTSUnitData* CreateUnitData(FName Id, UObject* Outer);
	ARTSUnit* SpawnUnit(FName Id, uint8 Team, const FVector& FeetLocation);
	bool FindSpawnPoint(const FVector& Center, float Radius, FVector& OutFeet) const;
	/** Real-sized unit, rotated building perimeter, and a verified unobstructed exit step. */
	bool FindProductionExit(const ARTSBuilding* Building, float UnitRadius, float UnitHalfHeight, FVector& OutFeet, FVector& OutExit, int32 StartCandidate = 0, int32* FoundCandidate = nullptr) const;
	bool CanPlace(FName Id, const FVector& Ground, FString& Error) const;
	ARTSBuilding* PlaceBuilding(FName Id, uint8 Team, const FVector& Ground, FString& Error);
private:
	UPROPERTY(Transient) TObjectPtr<UDataTable> Table;
	UPROPERTY(Transient) mutable TMap<uint8, TObjectPtr<URTSResourceComponent>> OtherStockpiles;
};
