// MiniRTS - shared lightweight types used across modules.

#pragma once

#include "CoreMinimal.h"
#include "RTSTypes.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogRTS, Log, All);

/** Team ids. Uses FGenericTeamId under the hood so engine AI perception can read it later. */
namespace RTSTeam
{
	constexpr uint8 Player  = 0;
	constexpr uint8 Enemy   = 1;
	constexpr uint8 Neutral = 255; // FGenericTeamId::NoTeam
}

/** Persistent idle combat policy; explicit orders retain priority. */
UENUM(BlueprintType)
enum class ERTSCombatStance : uint8
{
	Guard UMETA(DisplayName = "原地警戒"),
	Patrol UMETA(DisplayName = "巡逻"),
	Aggressive UMETA(DisplayName = "侵略")
};

/** Every command a unit can receive. Player input and AI both go through URTSOrderLibrary. */
UENUM(BlueprintType)
enum class ERTSOrderType : uint8
{
	None UMETA(DisplayName = "无命令"),
	Move UMETA(DisplayName = "移动"),
	Attack UMETA(DisplayName = "攻击"),
	Stop UMETA(DisplayName = "停止"),
	Gather UMETA(DisplayName = "采集"),
	Build UMETA(DisplayName = "建造"),
	ReturnResources UMETA(DisplayName = "交回资源"),
	// P1-03: march toward the destination but engage anything hostile met on the way, then resume.
	// Appended last so existing serialized enum values keep their numbers.
	AttackMove UMETA(DisplayName = "攻击移动")
};

USTRUCT(BlueprintType)
struct FRTSOrder
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "RTS")
	ERTSOrderType Type = ERTSOrderType::None;

	/** World-space destination for Move. */
	UPROPERTY(BlueprintReadWrite, Category = "RTS")
	FVector TargetLocation = FVector::ZeroVector;

	/** Target for Attack. Weak so a destroyed target never dangles. */
	UPROPERTY(BlueprintReadWrite, Category = "RTS")
	TWeakObjectPtr<AActor> TargetActor;
};

/** Resource kinds. Count is a sentinel for iteration. */
UENUM(BlueprintType)
enum class ERTSResource : uint8
{
	Wood UMETA(DisplayName = "木材"),
	Food UMETA(DisplayName = "食物"),
	IronOre UMETA(DisplayName = "铁矿"),
	RefinedIron UMETA(DisplayName = "精铁"),
	Amber UMETA(DisplayName = "琥珀"),
	Crystal UMETA(DisplayName = "水晶"),
	Gold UMETA(DisplayName = "金币"),
	Mana UMETA(DisplayName = "法力"),
	Count UMETA(Hidden)
};
