#pragma once
#include "CoreMinimal.h"

/** Asset-pack infantry supported by the visual preview; not a production unlock list. */
namespace RTSInfantryCatalog
{
struct FEntry
{
	const TCHAR* Id;
	const TCHAR* Label;
	const TCHAR* Mesh;
	const TCHAR* AnimFolder;
	const TCHAR* AnimPrefix;
	const TCHAR* Style;
};
inline const TArray<FEntry>& Entries()
{
	static const TArray<FEntry> All = {
		{TEXT("Peasant"), TEXT("工人"), TEXT("SK_Peasant"), TEXT("Infantry"), TEXT("infantry"), TEXT("Worker")},
		{TEXT("Swordman"), TEXT("剑士"), TEXT("SK_Swordman"), TEXT("Infantry"), TEXT("infantry"), TEXT("Heavy")},
		{TEXT("Archer"), TEXT("弓箭手"), TEXT("SK_Archer"), TEXT("Archer"), TEXT("archer"), TEXT("Ranged")},
		{TEXT("Spearman"), TEXT("长矛兵"), TEXT("SK_Spearman"), TEXT("Spear"), TEXT("spear"), TEXT("Light")},
		{TEXT("Scout"), TEXT("斥候"), TEXT("SK_Scout"), TEXT("Archer"), TEXT("archer"), TEXT("Ranged")},
		{TEXT("Priest"), TEXT("牧师"), TEXT("SK_Priest"), TEXT("Staff"), TEXT("staff"), TEXT("Caster")},
		{TEXT("Paladin"), TEXT("圣骑士"), TEXT("SK_Paladin"), TEXT("TwoHanded"), TEXT("twohanded"), TEXT("Heavy")},
		{TEXT("Mage"), TEXT("法师"), TEXT("SK_Mage"), TEXT("Staff"), TEXT("staff"), TEXT("Caster")},
		{TEXT("Light_Infantry"), TEXT("轻步兵"), TEXT("SK_Light_Infantry"), TEXT("Infantry"), TEXT("infantry"), TEXT("Light")},
		{TEXT("HighPriest"), TEXT("高阶牧师"), TEXT("SK_HighPriest"), TEXT("Staff"), TEXT("staff"), TEXT("Caster")},
		{TEXT("King"), TEXT("国王"), TEXT("SK_King"), TEXT("Infantry"), TEXT("infantry"), TEXT("Heavy")},
		{TEXT("Heavy_Infantry"), TEXT("重步兵"), TEXT("SK_Heavy_Infantry"), TEXT("Infantry"), TEXT("infantry"), TEXT("Heavy")},
		{TEXT("HeavySwordman"), TEXT("重剑士"), TEXT("SK_HeavySwordman"), TEXT("TwoHanded"), TEXT("twohanded"), TEXT("Heavy")},
		{TEXT("Halberdier"), TEXT("戟兵"), TEXT("SK_Halberdier"), TEXT("Polearm"), TEXT("polearm"), TEXT("Heavy")},
		{TEXT("Crossbowman"), TEXT("弩兵"), TEXT("SK_Crossbowman"), TEXT("Crossbow"), TEXT("crossbow"), TEXT("Ranged")},
		{TEXT("Commander"), TEXT("指挥官"), TEXT("sk_Commander"), TEXT("Infantry"), TEXT("infantry"), TEXT("Heavy")}
	};
	return All;
}
inline const FEntry* Find(FName Id)
{
	return Entries().FindByPredicate([&](const FEntry& E) { return Id == E.Id; });
}
}
